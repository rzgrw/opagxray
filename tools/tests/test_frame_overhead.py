import unittest
from tools import frame_overhead as p


def fixture(role='base', run=1, wall=1000000):
    meta=dict(role=role, revision='e3f273f04bf85b4b265004cbbc8a35f19796cd31' if role=='base' else '45680801007e8c5a3fab7555eb4e4ed17a35ffe3',
        probe_core_sha256='a'*64, callsite_patch_sha256='b'*64, source_snapshot_sha256='c'*64,
        binary_sha256='d'*64,scene='cordon',save='private',level='l01_escape',vsync='off',frame_limit='501',
        resolution='1280x720',renderer='renderer_r3',user_ltx_sha256='e'*64,content_manifest='unknown',overlay='unknown',
        clock_is_steady='true',clock_period_ns='1.000000000',nwarmup='1',nmeasured='2',stamps='4',truncated='false',
        machine='Mac16.1',run_index=str(run),collector_rows='0',collector_capacity='0',collector_warmup='0',
        collector_invalid='0',collector_dropped='0',collector_truncated='false',collector_flags='0',collector_not_ready='0',
        collector_start_ns='0',collector_end_ns='0')
    if role=='on':
        meta.update(collector_rows='2',collector_capacity='3',collector_warmup='1',collector_start_ns=str(10+wall+1),collector_end_ns=str(10+3*wall))
    return '# xray-overhead-probe-v1\n'+''.join(f'# {k}={v}\n' for k,v in meta.items())+'loop_seq,start_ns,wall_ns\n'+''.join(f'{i},{10+i*wall},{wall}\n' for i in range(3))

class ProbeSchemaTests(unittest.TestCase):
    def test_retains_warmup_and_scores_exact_intervals(self):
        c=p.parse(fixture().replace('machine=Mac16.1','machine=Mac16,1'));self.assertEqual(len(c.rows),3);self.assertEqual(c.summary()['p95_ns'],1000000)
    def test_rejects_noncanonical_or_corrupt_inputs(self):
        for old,new in [('stamps=4','stamps=5'),('clock_is_steady=true','clock_is_steady=false'),
                        ('clock_period_ns=1.000000000','clock_period_ns=nan'),('truncated=false','truncated=true'),
                        ('0,10,1000000','0,10,0'),('1,1000010','1,1000011'),('2,2000010','3,2000010'),
                        ('role=base','role=bare'),('content_manifest=unknown','content_manifest='+'a'*64),
                        ('frame_limit=501','frame_limit=0'),('nwarmup=1','nwarmup=+1')]:
            with self.subTest(new=new),self.assertRaises(p.ProbeError):p.parse(fixture().replace(old,new))
        for extra in ['# role=base\n','# unknown=field\n','']:
            if extra:
                with self.assertRaises(p.ProbeError):p.parse(fixture().replace('loop_seq,start_ns,wall_ns',extra+'loop_seq,start_ns,wall_ns'))
        with self.assertRaises(p.ProbeError):p.parse(fixture()+'3,3000010,1000000\n')
    def test_collector_envelope_and_saturation_rejected(self):
        p.parse(fixture('on'))
        for old,new in [('collector_capacity=3','collector_capacity=2'),('collector_rows=2','collector_rows=1'),
                        ('collector_flags=0','collector_flags=16'),('collector_not_ready=0','collector_not_ready=1'),
                        ('collector_end_ns=3000010','collector_end_ns=3000011')]:
            with self.subTest(new=new),self.assertRaises(p.ProbeError):p.parse(fixture('on').replace(old,new))
    def test_long_frames_are_kept_nearest_rank(self):
        c=p.parse(fixture().replace('2,2000010,1000000','2,2000010,1000000000'))
        self.assertEqual(c.summary()['p95_ns'],1000000000)
        self.assertEqual(c.summary()['p50_ns'],1000000)
    def test_five_cycle_order_and_paired_deltas(self):
        order=p.schedule();self.assertEqual(order[:6],[(1,'base'),(1,'off'),(1,'on'),(2,'off'),(2,'on'),(2,'base')])
        captures=[p.parse(fixture(role,run,1000000+{'base':0,'off':20000,'on':50000}[role])) for run,role in order]
        result=p.aggregate(captures)
        self.assertEqual(result['off_minus_base']['p50_ns'],20000)
        self.assertEqual(result['on_minus_off']['p95_ns'],30000)
        self.assertTrue(result['thresholds_pass'])
        self.assertFalse(result['campaign_accepted']) # independent provenance/exit/scene gate absent
        self.assertEqual(len(result['runs']),15)
    def test_nearest_rank_and_paired_median_are_not_conflated(self):
        text=fixture().split('loop_seq,start_ns,wall_ns')[0].replace('nmeasured=2','nmeasured=20').replace('stamps=4','stamps=22')
        start=10;rows=[]
        for i,wall in enumerate([7]+[j*1000000 for j in range(1,21)]):
            rows.append(f'{i},{start},{wall}');start+=wall
        result=p.parse(text+'loop_seq,start_ns,wall_ns\n'+'\n'.join(rows)+'\n').summary()
        self.assertEqual((result['p50_ns'],result['p95_ns']),(10000000,19000000))
        captures=[]
        for run in range(1,6):
            off=([2,3,4,5,1][run-1])*1000000
            captures.extend([p.parse(fixture('base',run,run*1000000)),p.parse(fixture('off',run,off)),p.parse(fixture('on',run,off+50000))])
        result=p.aggregate(captures)
        self.assertEqual(result['off_minus_base']['p50_ns'],1000000)
        self.assertEqual(result['gate_deltas']['off_minus_base']['p50_ns'],0)
        self.assertTrue(result['thresholds_pass'])

    def test_missing_duplicate_and_mismatched_runs_fail_closed(self):
        self.assertEqual(p.aggregate([p.parse(fixture())])['status'],'inconclusive')
        with self.assertRaises(p.ProbeError):p.aggregate([p.parse(fixture()),p.parse(fixture())])
        with self.assertRaises(p.ProbeError):p.aggregate([p.parse(fixture()),p.parse(fixture('off').replace('machine=Mac16.1','machine=Mac16.2'))])


class RunnerTests(unittest.TestCase):
    def test_ready_checks_camera_level_settings(self):
        from tools.frame_overhead_runner import ready
        state='scene=game paused=0 loadscr=0 precache=0 level=l01_escape'
        view='ready=1 active=1 paused=0 precache=0 pos=1,2,3 dir=0,0,1 vsync=0 limit=501 resolution=1280x720 renderer=renderer_r3'
        expected=dict(level='l01_escape',position=[1,2,3],direction=[0,0,1],resolution='1280x720')
        self.assertTrue(ready(state,view,expected))
        for wrong in [view.replace('pos=1,2,3','pos=2,2,3'),view.replace('active=1','active=0'),view.replace('limit=501','limit=60')]:
            self.assertFalse(ready(state,wrong,expected))
    def test_capture_has_no_requests_between_start_and_completion(self):
        from tools.frame_overhead_runner import capture_window
        events=[]
        class Bridge:
            def request(self,verb,payload):events.append(verb);return True,'armed'
        def release():events.append('release')
        def wait():events.append('complete');return b'1'
        capture_window(Bridge(),'args',release,wait)
        self.assertEqual(events,['overhead.arm','release','complete'])
    def test_missing_adapter_refused_before_launch(self):
        from tools.frame_overhead_runner import validate_adapter
        with self.assertRaises(p.ProbeError):validate_adapter(None)
        with self.assertRaises(p.ProbeError):validate_adapter(type('Regular',(),{'schema':'xray-frame-time-v1'})())

    def test_real_pipe_lifecycle_attests_before_quit_settings_rewrite(self):
        # A tiny fixture process, never xr_3da: exercises the real Unix bridge,
        # inherited start/completion pipes, CSV parser and normal quit ordering.
        import hashlib
        import json
        import os
        from pathlib import Path
        import tempfile
        from tools.frame_overhead_runner import run_one
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();runtime=root/'runtime';(runtime/'appdata/savedgames').mkdir(parents=True)
            (runtime/'appdata/savedgames/private.scop').write_bytes(b'fixture save')
            (runtime/'appdata/user.ltx').write_bytes(b'before quit')
            binary=root/'fixture-engine';identity=root/'identity.json'
            identity.write_text(json.dumps({'values':dict(ROLE='base',REVISION='e3f273f04bf85b4b265004cbbc8a35f19796cd31',CORE='a'*64,CALLSITE='b'*64,SOURCE='c'*64)}))
            script='''#!/usr/bin/env python3
import os,socket,pathlib,hashlib
s=socket.socket(socket.AF_UNIX);s.bind('appdata/overhead.sock');s.listen(1);c,_=s.accept();f=c.makefile('rb')
while True:
 line=f.readline().decode().strip()
 if not line:break
 rid,verb,*parts=line.split(' ',2);payload=parts[0] if parts else ''
 response='ok'
 if verb=='state':response='scene=game paused=0 loadscr=0 precache=0 level=l01_escape'
 if verb=='overhead.ready':response='ready=1 active=1 paused=0 precache=0 pos=1,2,3 dir=0,0,1 vsync=0 limit=501 resolution=1280x720 renderer=renderer_r3'
 if verb=='overhead.arm':response='armed'
 c.sendall((rid+' ok '+response+'\\n').encode())
 if verb=='overhead.arm':
  assert os.read(int(os.environ['XR_OVERHEAD_START_FD']),1)==b'G'
  args=payload.split();raw=FIXTURE.replace('binary_sha256='+('d'*64),'binary_sha256='+args[6]).replace('user_ltx_sha256='+('e'*64),'user_ltx_sha256='+args[7])
  pathlib.Path('appdata/perf').mkdir();pathlib.Path('appdata/perf/overhead-'+str(os.getpid())+'.csv').write_text(raw)
  os.write(int(os.environ['XR_OVERHEAD_COMPLETION_FD']),b'1')
 if verb=='cmd' and payload=='quit':
  pathlib.Path('appdata/user.ltx').write_bytes(b'after quit');break
'''.replace('FIXTURE',repr(fixture()))
            binary.write_text(script);binary.chmod(0o700)
            class Adapter:
                schema='xray-overhead-probe-v1'
                def seal(self,context):
                    os.kill(context['pid'],0)
                    import time
                    return dict(format='openxviberay-overhead-provenance-v1',probe_schema=self.schema,command='seal',result='verified',asset_verified=True,
                                source_identity_verified=True,loaded_images_verified=True,live_runtime_attested=True,failures=[],campaign_accepted=False,reporter_invoked=False,
                                completed_monotonic=time.monotonic(),result_expires_monotonic=time.monotonic()+60)
                def attest(self,context,capture,evidence,seal):
                    os.kill(context['pid'],0)
                    self_outer.assertEqual((runtime/'appdata/user.ltx').read_bytes(),b'before quit')
                    import time
                    return dict(format='openxviberay-overhead-provenance-v1',command='attest',result='verified',probe_schema=self.schema,asset_verified=True,
                                schema_valid=True,measurement_accepted=True,live_runtime_attested=True,failures=[],campaign_accepted=False,reporter_invoked=False,
                                completed_monotonic=time.monotonic(),result_expires_monotonic=time.monotonic()+60,
                                source_identity_verified=True,loaded_images_verified=True,raw_capture_sha256=hashlib.sha256(Path(capture).read_bytes()).hexdigest())
            self_outer=self
            config=dict(private_runtime=True,runtime=str(runtime),binary=str(binary),out=str(root/'evidence'),source_identity=str(identity),
                        role='base',run_index=1,nwarmup=1,nmeasured=2,save='private',scene='cordon',machine='Mac16.1',settle_seconds=0,
                        expected=dict(level='l01_escape',position=[1,2,3],direction=[0,0,1],resolution='1280x720'))
            import warnings
            from unittest.mock import patch
            with warnings.catch_warnings(record=True) as caught, patch('tools.frame_overhead_runner.hardware_model',return_value='Mac16.1'):
                warnings.simplefilter('always',ResourceWarning)
                result=run_one(config,Adapter())
            self.assertFalse([w for w in caught if issubclass(w.category,ResourceWarning)])
            self.assertEqual(result['exit_code'],0);self.assertFalse(result['process_alive']);self.assertFalse(result['campaign_accepted'])
            self.assertEqual((runtime/'appdata/user.ltx').read_bytes(),b'after quit')

if __name__=='__main__':unittest.main()
