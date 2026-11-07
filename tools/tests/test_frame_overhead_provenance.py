"""Small disposable fixtures only; no game launch or live process observation."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import frame_overhead_provenance as a
from tools.tests.test_frame_overhead import fixture


def sha(data):return hashlib.sha256(data).hexdigest()
def put(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
def macho(kind):
    h=bytearray(32);h[:4]=(0xFEEDFACF).to_bytes(4,'little');h[4:8]=(0x0100000C).to_bytes(4,'little');h[12:16]=kind.to_bytes(4,'little');return bytes(h)

class Rig:
    def __init__(self,root,role='base'):
        self.p=a.dependencies();self.root=root;self.role=role;self.runtime=root/'runtime';self.out=root/'out';self.out.mkdir(parents=True)
        source=root/'source';source.mkdir();self.source=source
        files={name:('source '+name).encode() for name in a.CORE}
        files['src/xrEngine/x_ray.cpp']=b'loop site'
        if role!='base':files['src/xrEngine/FrameTimeSession.cpp']=b'collector boundary'
        for name,data in files.items():put(source/name,data)
        values=dict(ROLE=role,REVISION=a.REVISIONS[role],CORE=sha(b''.join(files[n] for n in a.CORE)),CALLSITE=sha(b'patch'),
                    SOURCE=sha(''.join(f'{n}\0{sha(data)}\n' for n,data in sorted(files.items())).encode()))
        self.identity=root/'source-identity.json';self.identity.write_text(json.dumps(dict(values=values,files={n:sha(d) for n,d in files.items()},submodules='')))
        self.exe=root/'bin/xr_3da';self.engine=root/'bin/xrEngine.dylib'
        put(self.exe,macho(2));put(self.engine,macho(6)+' '.join(values.values()).encode())
        binaries={p.name:dict(path=str(p),resolved_path=str(p),size=p.stat().st_size,sha256=sha(p.read_bytes())) for p in [self.exe,self.engine]}
        anchor=dict(source=str(source),head=values['REVISION'],source_identity=values,identity_json=str(self.identity),
                    identity_sha256=sha(self.identity.read_bytes()),binary_dir=str(self.exe.parent),binaries=binaries)
        self.trust=dict(format=a.BUILD_FORMAT,arms={role:anchor})
        base=root/'base';patchdir=root/'patch';overlay=root/'overlay';patchdir.mkdir()
        db=b'db';shader=b'shader'
        put(base/'database/config/tiny.db',db);put(overlay/'gamedata/shaders/gl/stub.glsl',shader)
        fs=self.p.filesystem_text().encode()
        managed={'fsgame.ltx':dict(source='generated from res/fsgame.ltx',sha256=sha(fs),size=len(fs)),
                 'database/config/tiny.db':dict(source=str(base/'database/config/tiny.db'),sha256=sha(db),size=len(db)),
                 'gamedata/shaders/gl/stub.glsl':dict(source=str(overlay/'gamedata/shaders/gl/stub.glsl'),sha256=sha(shader),size=len(shader))}
        for rel,data in [('fsgame.ltx',fs),('database/config/tiny.db',db),('gamedata/shaders/gl/stub.glsl',shader),
                         ('appdata/user.ltx',b'renderer renderer_r3\n'),('appdata/savedgames/private.scop',b'private save')]:put(self.runtime/rel,data)
        put(self.runtime/'stage-manifest.json',json.dumps(dict(format=self.p.STAGE_FORMAT,base=str(base),patch=str(patchdir),overlay=str(overlay),files=managed)).encode())
        self.pid=424242;self.launcher=root/'launcher.json'
        self.launcher.write_text(json.dumps(dict(format=self.p.LAUNCHER_FORMAT,pid=self.pid,executable=str(self.exe),cwd=str(self.runtime),
            argv=[str(self.exe),'-fsltx','fsgame.ltx','-ltx','user.ltx','-agent_bridge','appdata/overhead.sock'])))
        self.process=self.p.FixtureProcess(self.pid,str(self.exe),[str(self.exe),str(self.engine),'/usr/lib/libSystem.B.dylib'])
        self.now=10.0
        self.adapter=a.ProbeAdapter(self.p,self.trust,self.process,clock=lambda:self.now)
        self.context=dict(schema=a.schema,pid=self.pid,launcher_record=str(self.launcher),source_identity=str(self.identity),
            private_save_sha256=sha(b'private save'),config=dict(private_runtime=True,runtime=str(self.runtime),binary=str(self.exe),out=str(self.out),
            source_identity=str(self.identity),role=role,run_index=1,nwarmup=1,nmeasured=2,scene='cordon',save='private',machine='Mac16.1',
            settle_seconds=0,expected=dict(level='l01_escape',resolution='1280x720')))
        text=fixture(role)
        for k,v in dict(revision=values['REVISION'],probe_core_sha256=values['CORE'],callsite_patch_sha256=values['CALLSITE'],
                        source_snapshot_sha256=values['SOURCE'],binary_sha256=binaries['xr_3da']['sha256'],user_ltx_sha256=sha(b'renderer renderer_r3\n')).items():
            import re
            text=re.sub(r'(?m)(# '+k+r'=)[^\s]+',lambda m:m.group(1)+v,text)
        self.capture=self.runtime/f'appdata/perf/overhead-{self.pid}.csv';self.evidence=self.out/'probe.csv'
        put(self.capture,text.encode());put(self.evidence,text.encode())
    def git(self,source,args,deadline):
        deadline.check()
        if args[0]=='rev-parse':return (a.REVISIONS[self.role]+'\n').encode()
        if args[0]=='diff':return b'patch'
        if args[0]=='submodule':return b''
        raise AssertionError(args)
    def seal(self):
        with patch.object(self.adapter,'git',side_effect=self.git):return self.adapter.seal(self.context)
    def attest(self,sealed):
        with patch.object(self.adapter,'git',side_effect=self.git):return self.adapter.attest(self.context,str(self.capture),str(self.evidence),sealed)

class AdapterTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory(prefix='op-',dir='/tmp');self.root=Path(self.tmp.name).resolve()
    def tearDown(self):self.tmp.cleanup()
    def test_expected_bound_bridge_socket_matches_actual_runtime_layout(self):
        # Real AF_UNIX socket, not a regular placeholder. The current upstream
        # pin rejects it; keep this regression red until the reviewed fix is pinned.
        import socket
        r=Rig(self.root/'socket')
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as bridge:
            bridge.bind(str(r.runtime/'appdata/overhead.sock'));bridge.listen(1)
            sealed=r.seal()
            self.assertEqual(sealed['result'],'verified',sealed)
            result=r.attest(sealed);self.assertEqual(result['result'],'verified',result)

    def test_base_without_session_and_all_roles_use_probe_parser(self):
        for role in ['base','off','on']:
            with self.subTest(role=role):
                r=Rig(self.root/role,role);original=r.capture.read_bytes()
                self.assertEqual((r.source/'src/xrEngine/FrameTimeSession.cpp').exists(),role!='base')
                r.p.evaluate_frame_time_measurement=lambda *args: self.fail('regular telemetry parser invoked')
                sealed=r.seal();self.assertEqual(sealed['result'],'verified',sealed)
                result=r.attest(sealed);self.assertEqual(result['result'],'verified',result)
                self.assertTrue(result['schema_valid']);self.assertFalse(result['live_runtime_attested']);self.assertFalse(result['campaign_accepted'])
                self.assertEqual(r.capture.read_bytes(),original);self.assertEqual(r.evidence.read_bytes(),original)
    def test_wrong_pid_or_mapped_arm_and_dead_attest_fail(self):
        for mutation in ['pid','image','exe','dead']:
            with self.subTest(mutation=mutation):
                r=Rig(self.root/mutation);sealed=r.seal();self.assertEqual(sealed['result'],'verified')
                if mutation=='pid':r.process.pid+=1
                if mutation=='image':
                    other=self.root/mutation/'other/xrEngine.dylib';put(other,r.engine.read_bytes());r.process.image_paths[1]=str(other)
                if mutation=='exe':r.process.executable_path=str(r.root/'other-executable')
                if mutation=='dead':r.process.is_alive=False
                self.assertNotEqual(r.attest(sealed)['result'],'verified')
    def test_tampered_identity_role_source_or_binary_rejected(self):
        for mutation in ['role','identity','source','binary']:
            with self.subTest(mutation=mutation):
                r=Rig(self.root/mutation)
                if mutation=='role':r.context['config']['role']='on'
                if mutation=='identity':r.identity.write_text(r.identity.read_text()+' ')
                if mutation=='source':(r.source/a.CORE[0]).write_bytes(b'changed')
                if mutation=='binary':r.engine.write_bytes(r.engine.read_bytes()+b'changed')
                result=r.seal();self.assertNotEqual(result['result'],'verified');self.assertTrue((r.out/'adapter-seal.json').exists())
    def test_missing_partial_expired_or_failed_seal_rejected(self):
        for mutation in ['missing','partial','expired','failed']:
            with self.subTest(mutation=mutation):
                r=Rig(self.root/mutation);sealed=r.seal()
                if mutation=='missing':sealed={}
                if mutation=='partial':sealed.pop('runtime_snapshot')
                if mutation=='expired':r.now+=601
                if mutation=='failed':sealed['result']='unknown'
                result=r.attest(sealed);self.assertNotEqual(result['result'],'verified');self.assertFalse(result['measurement_accepted'])
    def test_runtime_roots_settings_sources_save_and_capture_are_rechecked(self):
        for mutation in ['loose','settings','source','save','evidence','schema','counts']:
            with self.subTest(mutation=mutation):
                r=Rig(self.root/mutation);sealed=r.seal()
                if mutation=='loose':put(r.runtime/'gamedata/scripts/rogue.script',b'override')
                if mutation=='settings':(r.runtime/'appdata/user.ltx').write_bytes(b'changed')
                if mutation=='source':(r.root/'base/database/config/tiny.db').write_bytes(b'changed')
                if mutation=='save':(r.runtime/'appdata/savedgames/private.scop').write_bytes(b'changed')
                if mutation=='evidence':r.evidence.write_bytes(b'changed')
                if mutation in ['schema','counts']:
                    text=r.capture.read_text().replace('xray-overhead-probe-v1','xray-frame-time-v1') if mutation=='schema' else r.capture.read_text().replace('stamps=4','stamps=3')
                    r.capture.write_text(text);r.evidence.write_text(text)
                self.assertNotEqual(r.attest(sealed)['result'],'verified')
    def test_timeout_and_no_overwrite_persist_failures(self):
        r=Rig(self.root/'timeout');r.context['config']['provenance_timeout_seconds']=0
        result=r.seal();self.assertEqual(result['result'],'timeout');raw=(r.out/'adapter-seal.json').read_bytes()
        again=r.seal();self.assertNotEqual(again['result'],'verified');self.assertEqual((r.out/'adapter-seal.json').read_bytes(),raw)
    def test_swapped_arm_with_both_arms_in_trust_still_fails(self):
        off=Rig(self.root/'off','off');on=Rig(self.root/'on','on')
        off.trust['arms'].update(on.trust['arms']);off.context['config']['role']='on'
        self.assertNotEqual(off.seal()['result'],'verified')

    def test_valid_schema_cannot_forge_probe_identity(self):
        import re
        for field in ['probe_core_sha256','callsite_patch_sha256','source_snapshot_sha256','binary_sha256','user_ltx_sha256']:
            with self.subTest(field=field):
                r=Rig(self.root/field);sealed=r.seal()
                raw=re.sub(r'(?m)(# '+field+r'=)[^\s]+',lambda m:m.group(1)+'0'*64,r.capture.read_text())
                r.capture.write_text(raw);r.evidence.write_text(raw)
                result=r.attest(sealed);self.assertEqual(result['failures'][0]['code'],'probe_identity')

    def test_external_image_set_and_bytes_remain_bound(self):
        for change in ['changed','added','removed','unparseable']:
            with self.subTest(change=change):
                r=Rig(self.root/change);external=r.root/'external/libExternal.dylib';put(external,macho(6))
                r.process.image_paths.append(str(external));sealed=r.seal();self.assertEqual(sealed['result'],'verified')
                if change=='changed':external.write_bytes(external.read_bytes()+b'changed')
                if change=='added':
                    extra=r.root/'external/extra.dylib';put(extra,macho(6));r.process.image_paths.append(str(extra))
                if change=='removed':r.process.image_paths.remove(str(external))
                if change=='unparseable':external.write_bytes(b'not Mach-O')
                self.assertNotEqual(r.attest(sealed)['result'],'verified')

    def test_late_deadline_and_partial_output_are_never_success(self):
        r=Rig(self.root/'late');r.context['config']['provenance_timeout_seconds']=1
        original=r.adapter.inspect
        def slow(context,deadline):
            result=original(context,deadline);r.now+=2;return result
        with patch.object(r.adapter,'inspect',side_effect=slow):result=r.seal()
        self.assertEqual(result['result'],'timeout')
        self.assertFalse(json.loads((r.out/'adapter-seal.json').read_text())['source_identity_verified'])
        r=Rig(self.root/'partial');out=r.out/'adapter-seal.json';out.write_bytes(b'{')
        self.assertNotEqual(r.seal()['result'],'verified');self.assertEqual(out.read_bytes(),b'{')

    def test_timeout_during_result_serialization_is_persisted_as_failure(self):
        r=Rig(self.root/'serialization');r.context['config']['provenance_timeout_seconds']=1
        original=r.p._dump
        def slow_dump(doc):
            raw=original(doc)
            if doc['result']=='verified':r.now+=2
            return raw
        with patch.object(r.p,'_dump',side_effect=slow_dump):result=r.seal()
        self.assertEqual(result['result'],'timeout')
        self.assertEqual(json.loads((r.out/'adapter-seal.json').read_text())['result'],'timeout')

    def test_bootstrap_pin_failure_has_an_exclusive_failure_artifact(self):
        r=Rig(self.root/'bootstrap')
        with patch.object(a,'BUILD_SHA256','0'*64):result=a.seal(r.context)
        self.assertEqual(result['result'],'unknown');self.assertTrue((r.out/'adapter-seal.json').is_file())
        raw=(r.out/'adapter-seal.json').read_bytes()
        with patch.object(a,'BUILD_SHA256','0'*64):a.seal(r.context)
        self.assertEqual((r.out/'adapter-seal.json').read_bytes(),raw)

    def test_dependencies_are_exact_and_do_not_resolve_ambient_tools(self):
        import sys
        prior=list(sys.path);p=a.dependencies();self.assertEqual(sys.path,prior)
        self.assertEqual(sha(Path(p.__file__).read_bytes()),a.DEPENDENCIES['tools/frame_time_provenance.py'])
        with self.assertRaises(ImportError):p.evaluate_frame_time_measurement('# xray-frame-time-v1\n')


class RunnerResultTests(unittest.TestCase):
    def test_runner_refuses_missing_partial_expired_or_fixture_result(self):
        from tools.frame_overhead_runner import validate_provenance_result
        from tools.frame_overhead import ProbeError
        good=dict(format=a.FORMAT,probe_schema=a.schema,command='attest',result='verified',asset_verified=True,
                  source_identity_verified=True,loaded_images_verified=True,live_runtime_attested=True,
                  schema_valid=True,measurement_accepted=True,campaign_accepted=False,reporter_invoked=False,failures=[],
                  completed_monotonic=10,result_expires_monotonic=70,raw_capture_sha256='a'*64)
        validate_provenance_result(good,'attest',now=20)
        for key,value in [('result','unknown'),('loaded_images_verified',None),('live_runtime_attested',False),('schema_valid',False),
                          ('measurement_accepted',False),('failures',[{}]),('result_expires_monotonic',15),('completed_monotonic',float('nan'))]:
            with self.subTest(key=key),self.assertRaises(ProbeError):validate_provenance_result({**good,key:value},'attest',now=20)
        for partial in [{},None,{k:v for k,v in good.items() if k!='result_expires_monotonic'}]:
            with self.assertRaises(ProbeError):validate_provenance_result(partial,'attest',now=20)

if __name__=='__main__':unittest.main()
