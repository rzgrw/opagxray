#!/usr/bin/env python3
"""Probe-only adapter; never launches a process or invokes the regular reporter.

Public runner interface: seal(context), attest(context,capture,evidence,seal).
The reviewed build inventory and byte-exact upstream dependencies are pinned here.
"""
import builtins
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time
import types

schema='xray-overhead-probe-v1'
FORMAT='openxviberay-overhead-provenance-v1'
BUILD_FORMAT='openxviberay-overhead-approved-builds-v1'
ROOT=Path(__file__).resolve().parent
SNAPSHOT=ROOT/'_overhead_deps/v90c6e57e6'
COMMIT='90c6e57e697ce4fa16c306ab9a09f298c825f342'
DEPENDENCIES={
    'tools/frame_time_provenance.py':'e8af674e3af5dcf6b068daeaec03259d8909b445e9013f888a00a854786e4366',
    'tools/stage_coc_runtime.py':'cdc36dda629fa621c34bdb0d4a2b105509c63ccceb1a194b282d37829399118c',
    'tools/frame_time_report.py':'f0f6a8ee084154b10425767faa0d0e7bd0d8bd9a19fcbfe0d2ac17752ab23f50',
    'res/fsgame.ltx':'22de48b67b46718026af451e731c7d32f8b0deeb47738c9eb952d47304a4341e',
}
PARSER_SHA256='df5d9713e18dee39d784a801a3ddd453d4ae00fd01fa849f2a1d21a7e5efc926'
BUILD_SHA256='279ce48ecdbcac918db0a117921db6edace4a4592a96142e580d5933f9de8b61'
REVISIONS=dict(base='e3f273f04bf85b4b265004cbbc8a35f19796cd31',off='45680801007e8c5a3fab7555eb4e4ed17a35ffe3',on='45680801007e8c5a3fab7555eb4e4ed17a35ffe3')
CORE=['src/xrEngine/OverheadProbe.h','src/xrEngine/OverheadProbe.cpp']
CALLSITES=['src/xrEngine/x_ray.cpp','src/xrEngine/AgentBridge.cpp','src/xrEngine/FrameTimeSession.h',
           'src/xrEngine/FrameTimeSession.cpp','src/xrEngine/CMakeLists.txt']


def digest(data):return hashlib.sha256(data).hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()

def pinned_bytes(path,expected):
    path=Path(path)
    if path.is_symlink() or path.resolve()!=path.absolute():raise ValueError(f'noncanonical pinned dependency: {path}')
    raw=path.read_bytes()
    if digest(raw)!=expected:raise ValueError(f'pinned dependency mismatch: {path}')
    return raw

def dependencies():
    """Execute original bytes with only imports isolated, never ambient tools.

    The original module inserts its REPO in sys.path; give it a private path list.
    Its absolute stager import resolves to the pinned stager module, whose own
    __file__ binds filesystem_text() to the pinned res/fsgame.ltx. The regular
    reporter snapshot is integrity checked but cannot be imported by this adapter.
    No changes to sys.path, sys.modules or the ambient tools package are made.
    """
    raw={name:pinned_bytes(SNAPSHOT/name,sha) for name,sha in DEPENDENCIES.items()}
    stage=types.ModuleType('_probe_pinned_stager');stage.__file__=str(SNAPSHOT/'tools/stage_coc_runtime.py')
    exec(compile(raw['tools/stage_coc_runtime.py'],stage.__file__,'exec'),stage.__dict__)
    private_sys=types.ModuleType('sys');private_sys.__dict__.update(vars(sys));private_sys.path=list(sys.path)
    def isolated_import(name,globals=None,locals=None,fromlist=(),level=0):
        if name=='tools.stage_coc_runtime':return stage
        if name=='tools.frame_time_report':raise ImportError('regular telemetry parser forbidden for overhead probe')
        if name=='tools' or name.startswith('tools.'):raise ImportError(f'unpinned tools import: {name}')
        if name=='sys':return private_sys
        return builtins.__import__(name,globals,locals,fromlist,level)
    module=types.ModuleType('_probe_pinned_provenance');module.__file__=str(SNAPSHOT/'tools/frame_time_provenance.py')
    module.__dict__['__builtins__']={**vars(builtins),'__import__':isolated_import}
    exec(compile(raw['tools/frame_time_provenance.py'],module.__file__,'exec'),module.__dict__)
    return module

# Load the approved probe parser by its explicit sibling path, including when the
# runner loads this adapter by filename. Its dataclasses require a module entry.
def probe_parser():
    name='_overhead_strict_probe_parser'
    path=ROOT/'frame_overhead.py'
    raw=pinned_bytes(path,PARSER_SHA256)
    module=types.ModuleType(name);module.__file__=str(path);sys.modules[name]=module
    exec(compile(raw,str(path),'exec'),module.__dict__)
    return module


class ProbeAdapter:
    """Operation-local inspector. Public wrappers always use pinned trust + LiveProcess.

    Dependency/process/clock injection is only an explicit Python fixture seam;
    no CLI, runner config or environment variable can enable fixture observation.
    """
    def __init__(self,provenance,trust,process,clock=time.monotonic):
        self.p=provenance;self.trust=trust;self.process=process;self.clock=clock

    def check(self,condition,code,detail='',kind='mismatch'):
        if not condition:raise self.p.GateError(kind,code,'',detail)

    def regular_bytes(self,path,deadline):
        path=Path(path)
        self.check(path.is_absolute() and path.resolve()==path,'ambiguous_path',str(path))
        sha,size=self.p.hash_regular(path,deadline)
        raw=path.read_bytes()
        self.check(len(raw)==size and digest(raw)==sha,'stat_race',str(path),kind='unknown')
        deadline.check();return raw

    def json_file(self,path,deadline):
        raw=self.regular_bytes(path,deadline)
        try:doc=json.loads(raw)
        except (ValueError,UnicodeError) as exc:raise self.p.GateError('unknown','json',str(path),str(exc)) from exc
        self.check(isinstance(doc,dict),'json_object',str(path));return doc,raw

    def git(self,source,args,deadline):
        deadline.check()
        try:return subprocess.check_output(['git','-C',str(source),*args],stderr=subprocess.PIPE,timeout=deadline.remaining())
        except subprocess.TimeoutExpired as exc:raise self.p.GateError('timeout','timeout',str(source),'git deadline') from exc
        except (OSError,subprocess.CalledProcessError) as exc:raise self.p.GateError('unknown','source_git',str(source),str(exc)) from exc

    def source_and_binaries(self,context,deadline):
        cfg=context['config'];role=cfg['role']
        self.check(self.trust.get('format')==BUILD_FORMAT and role in REVISIONS and role in self.trust['arms'],'arm')
        arm=self.trust['arms'][role];values=arm['source_identity'];source=Path(arm['source'])
        self.check(values['ROLE']==role and values['REVISION']==REVISIONS[role]==arm['head'],'arm_revision')
        self.check(source.resolve()==source and source.is_dir(),'source_root')
        self.check(context['source_identity']==cfg['source_identity']==arm['identity_json'],'identity_path')
        identity,raw=self.json_file(context['source_identity'],deadline)
        self.check(digest(raw)==arm['identity_sha256'],'identity_bytes')
        self.check(set(identity)=={'values','files','submodules'} and identity['values']==values,'identity_values')
        self.check(self.git(source,['rev-parse','--verify','HEAD'],deadline).decode().strip()==values['REVISION'],'source_head')
        actual={};core=[]
        for name,expected in sorted(identity['files'].items()):
            self.p.check_relative(name)
            data=self.regular_bytes(source/name,deadline);actual[name]=digest(data)
            self.check(actual[name]==expected,'source_file',name)
        for name in CORE:
            self.check(name in actual,'source_inventory',name)
            core.append(self.regular_bytes(source/name,deadline))
        snapshot=digest(''.join(f'{name}\0{sha}\n' for name,sha in actual.items()).encode())
        self.check(snapshot==values['SOURCE'] and digest(b''.join(core))==values['CORE'],'source_snapshot')
        callsite=self.git(source,['diff','--no-ext-diff','--binary','HEAD','--',*CALLSITES],deadline)
        self.check(digest(callsite)==values['CALLSITE'],'callsite_patch')
        submodules=self.git(source,['submodule','status','--recursive'],deadline).decode()
        self.check(submodules==identity['submodules'] and all(line.startswith(' ') for line in submodules.splitlines()),'submodules')
        dirty=self.git(source,['submodule','foreach','--quiet','--recursive','git status --porcelain --untracked-files=no'],deadline)
        self.check(not dirty.strip(),'submodule_dirty')
        expected_binaries=arm['binaries']
        self.check('xr_3da' in expected_binaries and 'xrEngine.dylib' in expected_binaries,'binary_inventory')
        for name,item in expected_binaries.items():
            path=Path(item['resolved_path']);self.check(path.name==name and path.parent==Path(arm['binary_dir']),'binary_path')
            data=self.regular_bytes(path,deadline)
            self.check(digest(data)==item['sha256'] and len(data)==item['size'],'binary_hash',name)
            if name=='xrEngine.dylib':
                self.check(all(values[key].encode() in data for key in ['REVISION','CORE','CALLSITE','SOURCE']),'embedded_identity')
        self.check(cfg['binary']==expected_binaries['xr_3da']['resolved_path'],'launch_binary')
        return dict(values=values,identity_sha256=digest(raw),binaries=expected_binaries,source=str(source))

    def inspect(self,context,deadline):
        cfg=context['config']
        self.check(context['schema']==schema and cfg.get('private_runtime') is True,'context')
        self.check(type(context['pid']) is int and context['pid']>0,'pid')
        bound=self.source_and_binaries(context,deadline)
        # This is the sole replacement in the original inspector. Assets, scan
        # roots, launcher/filesystem binding and mapped-image reads are unchanged.
        self.p.source_build=lambda config: 'probe:'+digest(canonical(bound['values']))
        runtime=self.p.inspect_runtime(Path(cfg['runtime']),Path(context['launcher_record']),config='Release',
                                       process=self.process,deadline=deadline,require_process=True)
        self.check(runtime['launcher_pid']==context['pid'],'launcher_pid')
        self.check(runtime['launcher_executable']==cfg['binary'],'launcher_binary')
        images=runtime['images'];mapped={item['path']:item for item in images}
        self.check(len(mapped)==len(images),'duplicate_mapped_image')
        binaries=bound['binaries'];bin_dir=Path(binaries['xr_3da']['resolved_path']).parent
        for item in images:
            path=Path(item['path']);name=path.name
            if name in binaries:
                expected=binaries[name]
                self.check(item['path']==expected['resolved_path'] and item['sha256']==expected['sha256'] and item['size']==expected['size'],'mapped_arm_binary',item['path'])
            elif path.parent==bin_dir:self.check(False,'unlisted_build_image',item['path'])
        for name in ['xr_3da','xrEngine.dylib']:
            self.check(binaries[name]['resolved_path'] in mapped,'missing_mapped_binary',name)
        console=Path(runtime['runtime'])/runtime['console_script']
        user_hash=digest(self.regular_bytes(console,deadline))
        self.p.check_relative(cfg['save']+'.scop')
        self.check('/' not in cfg['save'],'save_name')
        save=Path(runtime['runtime'])/'appdata/savedgames'/f"{cfg['save']}.scop"
        save_hash=digest(self.regular_bytes(save,deadline))
        self.check(save_hash==context['private_save_sha256'],'save_hash')
        return dict(runtime_snapshot=runtime,probe_binding=bound,user_ltx_sha256=user_hash,private_save_sha256=save_hash)

    def failure(self,command,error):
        return dict(format=FORMAT,probe_schema=schema,command=command,result=error.kind,asset_verified=False,
                    source_identity_verified=False,loaded_images_verified=False,schema_valid=False,
                    measurement_accepted=False,campaign_accepted=False,live_runtime_attested=False,
                    reporter_invoked=False,failures=[dict(code=error.code,path=error.path,detail=error.detail)])

    def perform(self,command,context,operation):
        p=self.p
        try:
            out=Path(context['config']['out'])/f'adapter-{command}.json'
            self.check(out.parent.is_dir() and out.parent.resolve()==out.parent,'output_directory')
            self.check(not out.exists() and not out.is_symlink(),'output_exists',kind='unknown')
            seconds=context['config'].get('provenance_timeout_seconds',180)
            self.check(type(seconds) in (int,float) and math.isfinite(seconds) and seconds>=0,'timeout_seconds')
            deadline=p.Deadline(seconds,self.clock);deadline.check()
            doc=operation(deadline)
            deadline.check()
        except p.GateError as error:doc=self.failure(command,error)
        except (KeyError,TypeError,ValueError,OSError,AttributeError) as error:
            doc=self.failure(command,p.GateError('unknown','incomplete_or_unreadable','',str(error)))
        doc.update(dependency_commit=COMMIT,dependency_sha256=DEPENDENCIES['tools/frame_time_provenance.py'],
                   probe_parser_sha256=PARSER_SHA256,completed_monotonic=self.clock())
        doc['result_expires_monotonic']=(doc['sealed_monotonic']+doc['max_age_seconds'] if command=='seal' and doc['result']=='verified'
                                         else doc['completed_monotonic']+60)
        payload=p._dump(doc)
        if doc['result']=='verified':
            try:deadline.check()
            except p.GateError as error:
                doc=self.failure(command,error)
                doc.update(dependency_commit=COMMIT,dependency_sha256=DEPENDENCIES['tools/frame_time_provenance.py'],probe_parser_sha256=PARSER_SHA256)
                payload=p._dump(doc)
        try:
            p._write_new(out,payload)
        except (p.GateError,OSError,UnboundLocalError) as error:
            # An existing or unavailable output is never replaced, even by an
            # error sidecar. Return a failure, never the unwritten verified doc.
            return self.failure(command,p.GateError('unknown','output_unavailable','',str(error)))
        return doc

    def seal(self,context):
        def operation(deadline):
            cfg=context['config'];settle=cfg['settle_seconds'];age=cfg.get('provenance_max_age_seconds',600)
            self.check(type(settle) in (int,float) and math.isfinite(settle) and settle>=0,'settle_seconds')
            self.check(type(age) in (int,float) and math.isfinite(age) and age>settle,'seal_max_age')
            checked=self.inspect(context,deadline);now=self.clock()
            return dict(format=FORMAT,probe_schema=schema,command='seal',result='verified',asset_verified=True,
                        source_identity_verified=True,loaded_images_verified=True,measurement_accepted=False,
                        schema_valid=False,campaign_accepted=False,reporter_invoked=False,failures=[],
                        live_runtime_attested=checked['runtime_snapshot']['process_observation']=='live',
                        sealed_monotonic=now,settle_seconds=settle,max_age_seconds=age,
                        context_sha256=digest(canonical(context)),snapshot_sha256=digest(canonical(checked)),**checked)
        return self.perform('seal',context,operation)

    def attest(self,context,capture,evidence,sealed):
        def operation(deadline):
            stored,seal_raw=self.json_file(Path(context['config']['out'])/'adapter-seal.json',deadline)
            self.check(stored==sealed,'seal_changed')
            self.check(sealed.get('format')==FORMAT and sealed.get('probe_schema')==schema and sealed.get('command')=='seal','seal_format')
            for key in ['asset_verified','source_identity_verified','loaded_images_verified']:
                self.check(sealed.get(key) is True,'seal_not_verified')
            self.check(sealed.get('result')=='verified' and sealed.get('failures')==[],'seal_not_verified')
            self.check(sealed.get('context_sha256')==digest(canonical(context)),'seal_context')
            age=self.clock()-sealed['sealed_monotonic']
            self.check(math.isfinite(age) and sealed['settle_seconds']<=age<=sealed['max_age_seconds'],'seal_expired_or_unsettled')
            keys=['runtime_snapshot','probe_binding','user_ltx_sha256','private_save_sha256']
            old={k:sealed[k] for k in keys}
            self.check(digest(canonical(old))==sealed['snapshot_sha256'],'partial_seal')
            current=self.inspect(context,deadline)
            self.check(set(old['runtime_snapshot'])==set(current['runtime_snapshot']),'partial_runtime_seal')
            changes=self.p._compare(old['runtime_snapshot'],current['runtime_snapshot'])
            for key in ['probe_binding','user_ltx_sha256','private_save_sha256']:
                self.check(old[key]==current[key],'bookend_'+key)
            for key in ['os_images','configured_loads','process_observation']:
                self.check(old['runtime_snapshot'][key]==current['runtime_snapshot'][key],'bookend_'+key)
            cfg=context['config'];expected_capture=Path(cfg['runtime'])/'appdata/perf'/f"overhead-{context['pid']}.csv"
            self.check(Path(capture)==expected_capture and Path(evidence)==Path(cfg['out'])/'probe.csv','capture_path')
            raw_hash,size,evidence_hash,text=self.p._bind_capture(Path(capture),Path(evidence),deadline,self.p.CHUNK)
            parser=probe_parser()
            try:measurement=parser.parse(text)
            except parser.ProbeError as error:raise self.p.GateError('mismatch','probe_schema',capture,str(error)) from error
            m=measurement.metadata;values=current['probe_binding']['values']
            expected=dict(role=cfg['role'],revision=values['REVISION'],probe_core_sha256=values['CORE'],callsite_patch_sha256=values['CALLSITE'],
                          source_snapshot_sha256=values['SOURCE'],binary_sha256=current['probe_binding']['binaries']['xr_3da']['sha256'],
                          user_ltx_sha256=current['user_ltx_sha256'],scene=cfg['scene'],save=cfg['save'],level=cfg['expected']['level'],
                          resolution=cfg['expected']['resolution'],machine=cfg['machine'],run_index=cfg['run_index'],
                          nwarmup=cfg.get('nwarmup',120),nmeasured=cfg.get('nmeasured',4096))
            self.check(all(m[key]==value for key,value in expected.items()),'probe_identity')
            # No wrapper may mutate capture/evidence between parser and result.
            again=self.p._bind_capture(Path(capture),Path(evidence),deadline,self.p.CHUNK)
            self.check(again[:3]==(raw_hash,size,evidence_hash),'capture_changed')
            self.check(self.regular_bytes(Path(cfg['out'])/'adapter-seal.json',deadline)==seal_raw,'seal_changed')
            self.check(self.process.alive(context['pid']),'process_exited_before_attest',kind='unknown')
            deadline.check()
            live=current['runtime_snapshot']['process_observation']=='live'
            return dict(format=FORMAT,probe_schema=schema,command='attest',result='verified',asset_verified=True,
                        source_identity_verified=True,loaded_images_verified=True,schema_valid=True,
                        measurement_accepted=live,campaign_accepted=False,live_runtime_attested=live,
                        reporter_invoked=False,failures=[],raw_capture_sha256=raw_hash,evidence_sha256=evidence_hash,
                        capture_size=size,seal_sha256=digest(seal_raw),unbound_changes=changes,
                        probe_summary=measurement.summary(),in_band_asset_fields='unknown',in_band_acceptance=False,**current,
                        limits=['Local bookend attestation; mutations reverted between bookends are not observable',
                                'PID binding has no kernel process-start token; PID reuse remains a limit',
                                'OS image paths are observed but not content hashed',
                                'No full-window functional/traffic/exit gate or campaign acceptance',
                                'Conditional common-probe wall measurements under unchanged integer 501 limiter'])
        return self.perform('attest',context,operation)


def public_adapter():
    p=dependencies()
    trust=json.loads(pinned_bytes(ROOT/'overhead_approved_builds_v1.json',BUILD_SHA256))
    return ProbeAdapter(p,trust,p.LiveProcess())

def _public(command,context,*args):
    try:return getattr(public_adapter(),command)(context,*args)
    except (OSError,ValueError,ImportError) as error:
        # Bootstrap failures cannot invoke an unverified dependency to write their
        # artifact. Use a minimal exclusive stdlib writer instead.
        doc=dict(format=FORMAT,probe_schema=schema,command=command,result='unknown',asset_verified=False,
                 source_identity_verified=False,loaded_images_verified=False,measurement_accepted=False,
                 schema_valid=False,campaign_accepted=False,live_runtime_attested=False,reporter_invoked=False,
                 failures=[dict(code='dependency_or_trust',path='',detail=str(error))])
        try:
            path=Path(context['config']['out'])/f'adapter-{command}.json'
            if path.parent.is_dir() and path.parent.resolve()==path.parent:
                with path.open('x') as f:json.dump(doc,f,indent=2);f.write('\n')
        except (OSError,KeyError,TypeError) as write_error:
            doc['artifact_error']=str(write_error)
        return doc

def seal(context):return _public('seal',context)
def attest(context,capture,evidence,sealed):return _public('attest',context,capture,evidence,sealed)
