#!/usr/bin/env python3
"""Opt-in preparation/runner for one private probe process.

Use --adapter tools/frame_overhead_provenance.py for the pinned probe adapter.
A schema-specific adapter and live verified results are mandatory before capture. Running this without --run only prints the predeclared schedule.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import select
import subprocess
import time

try:
    from .agentctl import Bridge
    from .frame_overhead import ProbeError, parse, schedule, require
except ImportError:
    from agentctl import Bridge
    from frame_overhead import ProbeError, parse, schedule, require

SCHEMA='xray-overhead-probe-v1'

def digest(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def hardware_model():
    return subprocess.check_output(['/usr/sbin/sysctl','-n','hw.model'],text=True).strip()

def validate_adapter(adapter):
    require(adapter is not None and getattr(adapter,'schema',None)==SCHEMA and
            callable(getattr(adapter,'seal',None)) and callable(getattr(adapter,'attest',None)),
            'probe-specific provenance adapter required before launch; regular telemetry parser is incompatible')

def validate_provenance_result(result,command,now=None):
    require(isinstance(result,dict),'missing provenance result')
    expected=dict(format='openxviberay-overhead-provenance-v1',probe_schema=SCHEMA,command=command,result='verified')
    require(all(result.get(k)==v for k,v in expected.items()),'incompatible or failed probe provenance result')
    for key in ('asset_verified','source_identity_verified','loaded_images_verified','live_runtime_attested'):
        require(result.get(key) is True,f'provenance did not verify {key}')
    require(result.get('failures')==[] and result.get('campaign_accepted') is False and result.get('reporter_invoked') is False,
            'partial or incompatible provenance result')
    start=result.get('completed_monotonic');expires=result.get('result_expires_monotonic')
    require(type(start) in (int,float) and type(expires) in (int,float) and math.isfinite(start) and math.isfinite(expires),
            'missing provenance expiry')
    current=time.monotonic() if now is None else now
    require(start<=current<=expires,'expired provenance result')
    if command=='attest':
        require(result.get('schema_valid') is True and result.get('measurement_accepted') is True,'probe measurement was not validated')
        raw_hash=result.get('raw_capture_sha256')
        require(isinstance(raw_hash,str) and bool(re.fullmatch('[0-9a-f]{64}',raw_hash)),'missing raw capture binding')

def fields(text):
    return dict(re.findall(r'([a-z_]+)=([^\s]+)',text))

def ready(state,view,expected):
    s=fields(state);v=fields(view)
    if any(s.get(k)!=value for k,value in dict(scene='game',paused='0',loadscr='0',precache='0',level=expected['level']).items()):return False
    if any(v.get(k)!=value for k,value in dict(ready='1',active='1',paused='0',precache='0',vsync='0',limit='501',resolution=expected['resolution'],renderer='renderer_r3').items()):return False
    try:
        for field,key in [('pos','position'),('dir','direction')]:
            actual=[float(x) for x in v[field].split(',')];target=expected[key]
            tolerance=expected.get('tolerance',.001)
            if len(actual)!=3 or len(target)!=3 or not all(math.isfinite(x) for x in actual+target):return False
            if not math.isfinite(tolerance) or tolerance<0:return False
            if any(abs(a-b)>tolerance for a,b in zip(actual,target)):return False
    except (KeyError,ValueError,TypeError):return False
    return True

def connect(path):
    # The existing Bridge constructor leaves its socket open when connect fails.
    # Retain the instance so this experimental retry path can close it explicitly.
    bridge=Bridge.__new__(Bridge)
    try:Bridge.__init__(bridge,path)
    except (OSError,ConnectionError):
        if hasattr(bridge,'sock'):bridge.sock.close()
        raise
    return bridge

def request(bridge,verb,payload=''):
    ok,out=bridge.request(verb,payload)
    require(ok,f'{verb}: {out}');return out

def capture_window(bridge,payload,release,wait):
    request(bridge,'overhead.arm',payload)
    release() # engine is blocked in the excluded control loop until this byte
    result=wait() # blocking completion pipe only; no game/input/socket/readback
    require(result==b'1','capture did not complete successfully')

def write_json(path,value):
    with Path(path).open('x') as f:json.dump(value,f,indent=2);f.write('\n')

def run_one(config,adapter):
    validate_adapter(adapter) # must precede all launch/runtime side effects
    require(config.get('private_runtime') is True,'explicit private-runtime declaration required')
    runtime=Path(config['runtime']);binary=Path(config['binary']);out=Path(config['out'])
    require(runtime.is_absolute() and runtime.resolve()==runtime and runtime.is_dir(),'invalid private runtime')
    require(binary.is_absolute() and binary.resolve()==binary and binary.is_file(),'invalid binary')
    identity=json.loads(Path(config['source_identity']).read_text())['values']
    role=config['role'];run=config['run_index'];warmup=config.get('nwarmup',120);measured=config.get('nmeasured',4096)
    require(role in ('base','off','on') and identity['ROLE']==role,'wrong build role')
    require(type(run) is int and 1<=run<=5,'invalid run index')
    require(type(warmup) is int and type(measured) is int and warmup>=0 and measured>0 and warmup+measured<131072,'invalid counts')
    for name in ('save','scene'):
        require(bool(re.fullmatch('[A-Za-z0-9_.-]{1,95}',config[name])),f'invalid {name}')
    require(config['machine']==hardware_model(),'machine must equal actual sysctl hw.model')
    expected=config['expected'];require(bool(re.fullmatch('[A-Za-z0-9_.-]{1,95}',expected['level'])),'invalid level')
    require(math.isfinite(config['settle_seconds']) and config['settle_seconds']>=0,'invalid settle interval')
    require((runtime/'appdata/savedgames'/f"{config['save']}.scop").is_file(),'explicit private save missing')
    out.mkdir(parents=True,exist_ok=False)
    binary_hash=digest(binary);user_hash=digest(runtime/'appdata/user.ltx')
    save_path=runtime/'appdata/savedgames'/f"{config['save']}.scop";save_hash=digest(save_path)
    completion_read,completion_write=os.pipe();start_read,start_write=os.pipe()
    socket='appdata/overhead.sock'
    require(not (runtime/socket).exists(),'private bridge socket already exists')
    env=os.environ.copy();env['XR_OVERHEAD_COMPLETION_FD']=str(completion_write);env['XR_OVERHEAD_START_FD']=str(start_read)
    argv=[str(binary),'-fsltx','fsgame.ltx','-ltx','user.ltx','-agent_bridge',socket]
    process=None;bridge=None;result={};window_started=False;window_completed=False
    try:
        with (out/'process.log').open('xb') as log:
            process=subprocess.Popen(argv,cwd=runtime,env=env,pass_fds=(completion_write,start_read),stdout=log,stderr=subprocess.STDOUT)
        os.close(completion_write);completion_write=-1;os.close(start_read);start_read=-1
        launcher=dict(format='openxviberay-launcher-record-v1',pid=process.pid,executable=str(binary),argv=argv,cwd=str(runtime))
        write_json(out/'launcher-record.json',launcher)
        context=dict(config=config,launcher_record=str(out/'launcher-record.json'),pid=process.pid,
                     source_identity=str(Path(config['source_identity']).resolve()),schema=SCHEMA,private_save_sha256=save_hash)
        deadline=time.monotonic()+config.get('ready_timeout_seconds',180)
        while bridge is None:
            require(process.poll() is None,'process exited before bridge')
            require(time.monotonic()<deadline,'bridge timeout')
            try:bridge=connect(str(runtime/socket))
            except (OSError,ConnectionError):time.sleep(.2)
        request(bridge,'cmd',f"load {config['save']}")
        observations=[]
        while True:
            state=request(bridge,'state');view=request(bridge,'overhead.ready')
            observations.append(dict(state=state,view=view))
            if ready(state,view,expected):break
            require(time.monotonic()<deadline,'scene/camera readiness timeout')
            time.sleep(.2)
        write_json(out/'readiness.json',observations)
        write_json(out/'runner-context.json',context)
        seal=adapter.seal(context)
        write_json(out/'seal.json',seal)
        validate_provenance_result(seal,'seal')
        time.sleep(config['settle_seconds'])
        state=request(bridge,'state');view=request(bridge,'overhead.ready')
        require(ready(state,view,expected),'scene changed after seal/settle')
        write_json(out/'pre-arm.json',dict(state=state,view=view))
        validate_provenance_result(seal,'seal')
        payload=f"{warmup} {measured} {run} {config['scene']} {config['save']} {expected['level']} {binary_hash} {user_hash} {config['machine']}"
        def release():
            nonlocal window_started
            require(os.write(start_write,b'G')==1,'start fence failed');window_started=True
        def wait():
            nonlocal window_completed
            readable,_,_=select.select([completion_read],[],[],config.get('capture_timeout_seconds',180))
            require(bool(readable),'completion timeout; retain owned live process for operator recovery')
            value=os.read(completion_read,1);window_completed=True;return value
        capture_window(bridge,payload,release,wait)
        require(process.poll() is None,'process exited before attest')
        capture=runtime/'appdata/perf'/f'overhead-{process.pid}.csv'
        raw=capture.read_bytes();parsed=parse(raw.decode('utf-8'));meta=parsed.metadata
        for key,value in dict(role=role,revision=identity['REVISION'],probe_core_sha256=identity['CORE'],callsite_patch_sha256=identity['CALLSITE'],
                              source_snapshot_sha256=identity['SOURCE'],binary_sha256=binary_hash,user_ltx_sha256=user_hash,
                              nwarmup=warmup,nmeasured=measured,run_index=run,scene=config['scene'],save=config['save'],level=expected['level'],
                              machine=config['machine'],resolution=expected['resolution']).items():
            require(meta[key]==value,f'capture identity mismatch: {key}')
        evidence=out/'probe.csv'
        with evidence.open('xb') as f:f.write(raw)
        sidecar=adapter.attest(context,str(capture),str(evidence),seal)
        write_json(out/'provenance.json',sidecar)
        validate_provenance_result(sidecar,'attest')
        require(sidecar['raw_capture_sha256']==hashlib.sha256(raw).hexdigest(),'probe raw capture binding failed')
        require(capture.read_bytes()==raw and evidence.read_bytes()==raw,'attestation changed raw CSV')
        require(digest(save_path)==save_hash,'private save changed')
        state=request(bridge,'state');view=request(bridge,'overhead.ready')
        require(ready(state,view,expected),'scene changed during capture')
        write_json(out/'post-capture.json',dict(state=state,view=view))
        result=dict(schema_valid=True,asset_verified=True,campaign_accepted=False,summary=parsed.summary(),
                    remaining_gate='Full-window functional/traffic/log and campaign review; bookends do not prove absence of transient contamination.')
    finally:
        # Never inject quit into an unfinished stamp window, even on timeout.
        # Leave that owned PID and launcher record for explicit coordinator recovery.
        if process is not None and bridge is not None and (not window_started or window_completed):
            if process.poll() is None:
                request(bridge,'cmd','quit')
                process.wait(timeout=30)
            result['exit_code']=process.returncode
        if bridge is not None:bridge.sock.close()
        for fd in (completion_read,completion_write,start_read,start_write):
            if fd>=0:os.close(fd)
        if process is not None:
            result['pid']=process.pid;result['process_alive']=process.poll() is None
            write_json(out/'run-result.json',result)
    require(result.get('exit_code')==0,'nonzero or missing normal exit')
    return result

def load_adapter(path):
    require(path is not None,'--adapter required for --run; use tools/frame_overhead_provenance.py')
    spec=importlib.util.spec_from_file_location('probe_provenance_adapter',path)
    require(spec is not None and spec.loader is not None,'cannot load adapter')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);validate_adapter(module);return module

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path);p.add_argument('--adapter',type=Path);a=p.parse_args()
    try:
        if a.run:print(json.dumps(run_one(json.loads(a.run.read_text()),load_adapter(a.adapter)),indent=2))
        else:print(json.dumps(dict(format='openxviberay-overhead-schedule-v1',slots=[dict(run_index=r,role=role) for r,role in schedule()],
                                  optional_unscored_shader_warm_launches=0,asset_gate='explicit probe adapter and live verified results required'),indent=2))
    except (ProbeError,OSError,ValueError) as e:p.exit(2,str(e)+'\n')
