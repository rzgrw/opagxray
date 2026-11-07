"""Fixture-only seal and attest tests. No game launch and no live attestation."""

import contextlib
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import frame_time_provenance as provenance
from tools.frame_time_provenance import (
    CHUNK, FRAME_SCHEMA, Deadline, GateError, LiveProcess, _agent_bridge_token,
    _bridge_socket, _contained, _directories_overlap, _ordinary_external_socket, aggregate,
    evaluate_frame_time_measurement, images_from_vmmap_text, inspect_runtime,
    is_os_image, main, parse_macho, source_build, _process_owns_bridge_socket,
)
from tools.frame_time_provenance import attest, seal
from tools.stage_coc_runtime import filesystem_text
from tools.tests.test_frame_time_report import capture_text, record

REPO = Path(__file__).resolve().parents[2]
DESIGN = REPO / 'docs/superpowers/specs/2026-11-07-frame-time-provenance-design.md'
OS_IMAGE = '/usr/lib/opagxray-provenance-os-image.dylib'
LOOSE_TREES = ('scripts', 'meshes', 'textures', 'sounds', 'anims', 'ai', 'spawns', 'levels')


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _dylib_command(cmd: int, name: str, current: int = 0x10000, compat: int = 0x10000) -> bytes:
    raw_name = name.encode() + b'\0'
    cmdsize = (24 + len(raw_name) + 7) & ~7
    blob = bytearray(cmdsize)
    blob[0:4] = cmd.to_bytes(4, 'little')
    blob[4:8] = cmdsize.to_bytes(4, 'little')
    blob[8:12] = (24).to_bytes(4, 'little')
    blob[16:20] = current.to_bytes(4, 'little')
    blob[20:24] = compat.to_bytes(4, 'little')
    blob[24:24 + len(raw_name)] = raw_name
    return bytes(blob)


def macho(filetype: int, *, identity: str | None = None, loads: tuple[str, ...] = ()) -> bytes:
    commands = b''
    if identity is not None:
        commands += _dylib_command(0xD, identity)
    for name in loads:
        commands += _dylib_command(0xC, name)
    header = bytearray(32)
    header[0:4] = (0xFEEDFACF).to_bytes(4, 'little')
    header[4:8] = (0x0100000C).to_bytes(4, 'little')
    header[12:16] = filetype.to_bytes(4, 'little')
    header[16:20] = ((1 if identity is not None else 0) + len(loads)).to_bytes(4, 'little')
    header[20:24] = len(commands).to_bytes(4, 'little')
    return bytes(header) + commands


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _failure(doc: dict) -> str:
    return doc['failures'][0]['code']


def _case_alias(path: Path) -> Path | None:
    """Different spelling of one directory, when this volume folds case.

    None means the swapped spelling is not an existing same-inode directory
    whose realpath keeps that spelling. Case-sensitive volumes take that path.
    """
    real = Path(os.path.realpath(path))
    alias = real.with_name(real.name.swapcase())
    if alias.name == real.name or not alias.exists() or not os.path.samefile(real, alias):
        return None
    if os.path.realpath(alias) != str(alias):
        return None
    return alias


def _listen(runtime: Path, rel: str) -> socket.socket:
    """Bind the engine's relative path from the runtime cwd and keep the fd open."""
    path = runtime.joinpath(*rel.split('/'))
    path.parent.mkdir(parents=True, exist_ok=True)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    previous = os.getcwd()
    os.chdir(runtime)
    try:
        sock.bind(rel)
        sock.listen(1)
    except Exception:
        sock.close()
        raise
    finally:
        os.chdir(previous)
    return sock


class Runtime:
    """Hand-built staged tree. Sources live outside the runtime."""

    def __init__(self, root: Path, db: bytes = b'db-bytes', shader: bytes = b'shader-bytes'):
        self.root = root
        self.runtime = root / 'runtime'
        self.base = root / 'base'
        self.patch = root / 'patch'
        self.overlay = root / 'overlay'
        self.exe = root / 'xr_3da'
        self.dylib = root / 'libfix.dylib'
        self.launcher = root / 'launcher.json'
        self.db = db
        self.shader = shader
        self.user = b'renderer renderer_r3\n'
        _write(self.base / 'database/config/tiny.db', db)
        _write(self.overlay / 'gamedata/shaders/gl/stub.glsl', shader)
        _write(self.exe, macho(2, loads=('libfix.dylib',)))
        _write(self.dylib, macho(6, identity='libfix.dylib'))
        self.patch.mkdir()
        self._install_tree()
        self.process = None
        self._write_launcher(424242)

    def _install_tree(self) -> None:
        _write(self.runtime / 'database/config/tiny.db', self.db)
        _write(self.runtime / 'gamedata/shaders/gl/stub.glsl', self.shader)
        for name in ('maps', 'resource', 'sound', 'patch'):
            (self.runtime / 'database' / name).mkdir(parents=True, exist_ok=True)
        (self.runtime / 'gamedata/configs').mkdir(parents=True, exist_ok=True)
        for name in LOOSE_TREES:
            (self.runtime / 'gamedata' / name).mkdir(parents=True, exist_ok=True)
        fs = filesystem_text().encode()
        _write(self.runtime / 'fsgame.ltx', fs)
        _write(self.runtime / 'appdata/user.ltx', self.user)
        db_source = self.base / 'database/config/tiny.db'
        shader_source = self.overlay / 'gamedata/shaders/gl/stub.glsl'
        files = {
            'database/config/tiny.db': {
                'source': str(db_source), 'sha256': _sha(self.db), 'size': len(self.db),
            },
            'gamedata/shaders/gl/stub.glsl': {
                'source': str(shader_source), 'sha256': _sha(self.shader), 'size': len(self.shader),
            },
            'fsgame.ltx': {
                'source': 'generated from res/fsgame.ltx', 'sha256': _sha(fs), 'size': len(fs),
            },
        }
        manifest = {
            'format': 'openxviberay-coc-runtime-v1',
            'base': str(self.base),
            'patch': str(self.patch),
            'overlay': str(self.overlay),
            'files': files,
        }
        _write(self.runtime / 'stage-manifest.json',
               (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode())

    def _write_launcher(self, pid: int, argv: list[str] | None = None,
                        external_communication_directory: str | None = None,
                        executable: str | None = None) -> None:
        exe = os.path.realpath(executable or self.exe)
        images = [exe, os.path.realpath(self.dylib), OS_IMAGE]
        self.process = type(self).process_type(pid, exe, images)
        doc = {
            'format': 'openxviberay-launcher-record-v1',
            'pid': pid,
            'executable': exe,
            'argv': argv or ['xr_3da'],
            'cwd': os.path.realpath(self.runtime),
        }
        if external_communication_directory is not None:
            doc['external_communication_directory'] = external_communication_directory
        _write(self.launcher, (json.dumps(doc, indent=2, sort_keys=True) + '\n').encode())

    class process_type:
        observation = 'fixture'

        def __init__(self, pid, executable, images, alive=True):
            self.pid = pid
            self.executable_path = executable
            self.image_paths = images
            self.is_alive = alive

        def alive(self, pid):
            return self.is_alive and pid == self.pid

        def executable(self, pid):
            return self.executable_path if self.alive(pid) else None

        def images(self, pid, deadline=None):
            del deadline
            if not self.alive(pid) or self.image_paths is None:
                return None
            return list(self.image_paths)

    def do_seal(self, out: Path | None = None, **kwargs):
        out = out or (self.root / 'seal.json')
        code = seal(self.runtime, self.launcher, 'Release', 1.5, kwargs.pop('timeout_seconds', 30),
                    out, process=kwargs.pop('process', self.process), **kwargs)
        doc = json.loads(out.read_text()) if out.exists() else None
        return code, doc, out

    def do_attest(self, seal_path: Path, text: str, out: Path, evidence_text: str | None = None, **kwargs):
        capture = out.with_suffix('.capture.csv')
        evidence = out.with_suffix('.evidence.csv')
        capture.write_text(text)
        evidence.write_text(text if evidence_text is None else evidence_text)
        before = capture.read_bytes()
        code = attest(self.runtime, self.launcher, seal_path, capture, evidence,
                      kwargs.pop('timeout_seconds', 30), out,
                      process=kwargs.pop('process', self.process), **kwargs)
        doc = json.loads(out.read_text()) if out.exists() else None
        return code, doc, capture, before


def _csv(identity: str, **metadata) -> str:
    changes = {
        'content_manifest': 'unknown',
        'overlay': 'unknown',
        'source_build': identity,
        'renderer': 'OpenGL',
        'render_resolution': '1280x720',
        'drawable_resolution': '1280x720',
        'vsync': 'off',
        'frame_limit': '501',
        'sdl_version': 'fixture-sdl2',
        'allocator_build': 'fixture-release-standard',
    }
    changes.update(metadata)
    return capture_text(metadata_changes=changes)


class ProvenanceFixtureTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.world = Runtime(Path(self._tmp.name))

    def tearDown(self):
        self._tmp.cleanup()

    def test_happy_path_keeps_reporter_acceptance_false(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        self.assertEqual(sealed['result'], 'verified')
        self.assertTrue(sealed['asset_verified'])
        self.assertIsNone(sealed['measurement_accepted'])
        self.assertFalse(sealed['live_runtime_attested'])
        self.assertEqual(sealed['process_observation'], 'fixture')
        self.assertIsNone(sealed['bridge_socket'])
        self.assertEqual(sealed['settle_seconds'], 1.5)
        self.assertEqual(sealed['source_build'], source_build('Release'))
        self.assertEqual(sealed['configured_loads'], ['libfix.dylib'])
        os_real = os.path.realpath(OS_IMAGE)
        self.assertIn(os_real, sealed['os_images'])
        self.assertNotIn(os_real, [item['path'] for item in sealed['images']])
        dylib = next(item for item in sealed['images'] if item['path'] == os.path.realpath(self.world.dylib))
        self.assertEqual(dylib['current_version'], '1.0.0')
        self.assertEqual(dylib['compat_version'], '1.0.0')

        text = _csv(sealed['source_build'])
        out = self.world.root / 'attest.json'
        status, doc, capture, before = self.world.do_attest(seal_path, text, out)
        self.assertEqual(status, 0, doc)
        self.assertTrue(doc['asset_verified'])
        self.assertTrue(doc['files_matched'])
        self.assertTrue(doc['measurement_accepted'])
        self.assertFalse(doc['live_runtime_attested'])
        self.assertEqual(doc['process_observation'], 'fixture')
        self.assertTrue(doc['reporter_invoked'])
        self.assertFalse(doc['reporter_acceptance'])
        self.assertEqual(doc['capture_sha256'], doc['evidence_sha256'])
        self.assertEqual(doc['capture_sha256'], _sha(before))
        self.assertEqual(capture.read_bytes(), before)
        self.assertEqual(doc['reasons'], [
            'unknown_provenance:content_manifest', 'unknown_provenance:overlay',
        ])
        report = subprocess.run(
            [sys.executable, str(REPO / 'tools/frame_time_report.py'), str(capture), '--acceptance'],
            cwd=REPO, capture_output=True, text=True, check=False)
        self.assertEqual(report.returncode, 1, report.stderr)
        again, second, _ = self.world.do_seal(self.world.root / 'seal-again.json')
        self.assertEqual(again, 0, second)
        self.assertEqual(second['managed_aggregate'], sealed['managed_aggregate'])

    def test_probe_csv_does_not_call_the_frame_time_reporter(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        probe = (
            '# xray-overhead-probe-v1\n'
            '# revision=e3f273f04bf85b4b265004cbbc8a35f19796cd31\n'
            '# probe_core_sha256=' + ('ab' * 32) + '\n'
            'loop_seq,start_ns,wall_ns\n'
            '1,0,100\n'
        )
        with mock.patch('tools.frame_time_report.parse_capture', side_effect=AssertionError('reporter')):
            status, doc, _capture, _before = self.world.do_attest(
                seal_path, probe, self.world.root / 'probe.json')
        self.assertEqual(status, 2, doc)
        self.assertEqual(_failure(doc), 'unexpected_schema')
        self.assertFalse(doc['reporter_invoked'])
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['measurement_accepted'])
        self.assertTrue(doc['files_matched'])
        self.assertFalse(doc['live_runtime_attested'])
        self.assertNotEqual(doc.get('source_build'), 'e3f273f04bf85b4b265004cbbc8a35f19796cd31')
        direct = evaluate_frame_time_measurement(probe)
        self.assertFalse(direct['reporter_invoked'])
        self.assertEqual(direct['schema'], '# xray-overhead-probe-v1')
        self.assertNotEqual(direct['schema'], FRAME_SCHEMA)

    def test_flipped_staged_byte_names_the_path(self):
        code, _sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0)
        target = self.world.runtime / 'database/config/tiny.db'
        target.write_bytes(self.world.db[:-1] + bytes([self.world.db[-1] ^ 0x01]))
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(source_build('Release')), self.world.root / 'flip.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'hash_mismatch')
        self.assertTrue(doc['failures'][0]['path'].endswith('database/config/tiny.db'))
        self.assertFalse(doc['asset_verified'])

    def test_loose_override_and_empty_alias_directory(self):
        code, _sealed, _path = self.world.do_seal()
        self.assertEqual(code, 0)
        for name in LOOSE_TREES:
            with self.subTest(name=name):
                root = Path(tempfile.mkdtemp(dir=self.world.root))
                other = Runtime(root)
                _write(other.runtime / 'gamedata' / name / 'loose.bin', b'loose')
                status, doc, _out = other.do_seal(root / 'seal.json')
                self.assertEqual(status, 1, doc)
                self.assertEqual(_failure(doc), 'unexpected_path')

    def test_unexpected_shader_gamedata_and_archive_entries(self):
        cases = {
            'shader': 'gamedata/shaders/loose.glsl',
            'shader-dir': 'gamedata/shaders/notes/readme.txt',
            'gamedata-file': 'gamedata/readme.txt',
            'gamedata-child': 'gamedata/maps/extra.bin',
            'patch-db': 'database/patch/evil.db',
            'patch-xdb': 'database/patch/evil.xdb',
            'appdata-db': 'appdata/evil.db',
            'config-db': 'gamedata/configs/evil.db',
            'dotfile': 'database/config/.hidden',
        }
        for name, rel in cases.items():
            with self.subTest(name=name):
                root = Path(tempfile.mkdtemp(dir=self.world.root))
                other = Runtime(root)
                _write(other.runtime / rel, b'extra')
                status, doc, _out = other.do_seal(root / 'seal.json')
                self.assertEqual(status, 1, doc)
                self.assertEqual(_failure(doc), 'unexpected_path')

    def test_dropped_template_roots_are_not_sealed_content(self):
        for name in ('mp', 'levels', 'resources', 'localization', 'patches'):
            _write(self.world.runtime / name / 'local.bin', b'local')
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 0, doc)
        sealed_paths = [item['path'] for item in doc['managed_files']]
        for name in ('mp', 'levels', 'resources', 'localization', 'patches'):
            self.assertNotIn(f'{name}/local.bin', sealed_paths)
        self.assertEqual(doc['not_effective_roots'], ['mp', 'levels', 'resources', 'localization', 'patches'])

    def test_settings_closure_follows_cfg_load_and_includes_only(self):
        _write(self.world.runtime / 'appdata/custom.ltx',
               b'cfg_load extra\n#include "skip-me.ltx"\n')
        _write(self.world.runtime / 'appdata/extra.ltx',
               b'cfg_load nested\n#include "also-skip.ltx"\n')
        _write(self.world.runtime / 'appdata/nested.ltx', b'bind 1\n')
        _write(self.world.runtime / 'appdata/skip-me.ltx', b'skip\n')
        _write(self.world.runtime / 'appdata/also-skip.ltx', b'skip\n')
        _write(self.world.runtime / 'appdata/from-include.ltx', b'from include\n')
        _write(self.world.runtime / 'appdata/bundle/main.ltx', b'#include "*.ltx"\n')
        _write(self.world.runtime / 'appdata/bundle/sibling.ltx', b'#include "pulled.ltx"\n')
        _write(self.world.runtime / 'appdata/bundle/pulled.ltx', b'value 1\n')
        _write(self.world.runtime / 'gamedata/configs/a.ltx',
               b'#include "b.ltx"\n#include "../../appdata/from-include.ltx"\n'
               b'#include "../../appdata/bundle/main.ltx"\n#include "archive-only.ltx"\n')
        _write(self.world.runtime / 'gamedata/configs/b.ltx', b'#include "c.ltx"\n')
        _write(self.world.runtime / 'gamedata/configs/c.ltx', b'#include "*.ltx"\n')
        self.world._write_launcher(424242, ['xr_3da', '-ltx', 'custom.ltx'])
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 0, doc)
        self.assertEqual(doc['console_script'], 'appdata/custom.ltx')
        self.assertEqual(set(doc['settings_paths']), {
            'appdata/custom.ltx', 'appdata/extra.ltx', 'appdata/nested.ltx',
            'gamedata/configs/a.ltx', 'gamedata/configs/b.ltx', 'gamedata/configs/c.ltx',
            'appdata/from-include.ltx', 'appdata/bundle/main.ltx',
            'appdata/bundle/sibling.ltx', 'appdata/bundle/pulled.ltx',
        })

    def test_include_that_leaves_the_runtime_is_unexpected(self):
        _write(self.world.runtime / 'gamedata/configs/bad.ltx', b'#include "../../../outside.ltx"\n')
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'unexpected_path')

    def test_other_include_glob_is_unknown(self):
        _write(self.world.runtime / 'gamedata/configs/bad.ltx', b'#include "*.script"\n')
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'include_glob')

    def test_user_ltx_hash_change_rejects_attest(self):
        _write(self.world.runtime / 'gamedata/configs/ok.ltx', b'section\n')
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        user = self.world.runtime / 'appdata/user.ltx'
        user.write_bytes(user.read_bytes() + b'rs_v_sync on\n')
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'user.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'settings')
        user.write_bytes(self.world.user)
        config = self.world.runtime / 'gamedata/configs/ok.ltx'
        config.write_bytes(b'section\nchanged\n')
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'config.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'settings')

    def test_unknown_frame_limit_keeps_assets_and_rejects_measurement(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        text = _csv(sealed['source_build'], frame_limit='unknown')
        status, doc, _capture, _before = self.world.do_attest(seal_path, text, self.world.root / 'limit.json')
        self.assertEqual(status, 0, doc)
        self.assertTrue(doc['asset_verified'])
        self.assertFalse(doc['measurement_accepted'])
        self.assertIn('unknown_provenance:frame_limit', doc['reasons'])

    def test_symlink_fifo_and_ambiguous_paths(self):
        link = self.world.runtime / 'gamedata/configs/link.ltx'
        link.symlink_to(self.world.runtime / 'appdata/user.ltx')
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'symlink')
        link.unlink()
        linked_dir = self.world.runtime / 'gamedata/configs/linked'
        linked_dir.symlink_to(self.world.runtime / 'appdata', target_is_directory=True)
        code, doc, _out = self.world.do_seal(self.world.root / 'dir-link.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'symlink')
        linked_dir.unlink()
        os.mkfifo(self.world.runtime / 'gamedata/configs/pipe')
        code, doc, _out = self.world.do_seal(self.world.root / 'fifo.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'nonregular')
        os.remove(self.world.runtime / 'gamedata/configs/pipe')
        _write(self.world.runtime / 'gamedata/configs/bad\nname.ltx', b'x\n')
        code, doc, _out = self.world.do_seal(self.world.root / 'newline.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'ambiguous_path')
        os.remove(self.world.runtime / 'gamedata/configs/bad\nname.ltx')
        _write(self.world.runtime / 'gamedata/configs/foo.', b'x\n')
        code, doc, _out = self.world.do_seal(self.world.root / 'dot.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'ambiguous_path')

    def test_manifest_parent_segment_is_ambiguous(self):
        manifest_path = self.world.runtime / 'stage-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['files']['database/config/../../outside.db'] = {
            'source': str(self.world.base / 'database/config/tiny.db'),
            'sha256': _sha(b'x'), 'size': 1,
        }
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'ambiguous_path')

    def test_canonical_symlink_parent_is_ambiguous(self):
        root = Path(os.path.realpath(self.world.root))
        real = root / 'canon'
        real.mkdir()
        (real / 'file').write_bytes(b'a')
        (root / 'link').symlink_to(real, target_is_directory=True)
        with self.assertRaises(GateError) as caught:
            _contained(root, 'link/file')
        self.assertEqual(caught.exception.code, 'ambiguous_path')

    def test_stat_race_is_unknown(self):
        seen = []

        def after(path: Path):
            seen.append(path.name)
            if path.name == 'tiny.db' and 'runtime' in path.parts and path.suffix != '.new':
                replacement = path.with_name('tiny-replaced.db')
                replacement.write_bytes(b'replaced')
                os.replace(replacement, path)

        code, doc, _out = self.world.do_seal(after_lstat=after)
        self.assertEqual(code, 2, doc)
        self.assertEqual(doc['result'], 'unknown')
        self.assertEqual(_failure(doc), 'stat_race')
        self.assertNotEqual(doc['result'], 'verified')
        self.assertFalse(doc['asset_verified'])
        self.assertTrue(seen)

    def test_fsgame_manifest_and_source_failures(self):
        fs = self.world.runtime / 'fsgame.ltx'
        fs.write_bytes(fs.read_bytes().replace(b'database', b'databasX', 1))
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'fsgame_bytes')
        fs.write_bytes(filesystem_text().encode())
        manifest_path = self.world.runtime / 'stage-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['files']['database/config/missing.db'] = {
            'source': str(self.world.base / 'database/config/tiny.db'),
            'sha256': _sha(self.world.db), 'size': len(self.world.db),
        }
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        code, doc, _out = self.world.do_seal(self.world.root / 'missing.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'membership')
        del manifest['files']['database/config/missing.db']
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        (self.world.overlay / 'gamedata/shaders/gl/stub.glsl').unlink()
        code, doc, _out = self.world.do_seal(self.world.root / 'source.json')
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'source_missing')

    def test_manifest_whitespace_is_a_byte_mismatch(self):
        code, _sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0)
        manifest_path = self.world.runtime / 'stage-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest_path.write_text(json.dumps(manifest, indent=4, sort_keys=True) + '\n')
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(source_build('Release')), self.world.root / 'space.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'manifest_bytes')

    def test_process_identity_and_macho_failures(self):
        self.world.process.executable_path = os.path.realpath(self.world.dylib)
        code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'process_identity')
        self.assertNotEqual(doc['result'], 'verified')
        self.world.process.executable_path = os.path.realpath(self.world.exe)
        self.world.process.image_paths = None
        code, doc, _out = self.world.do_seal(self.world.root / 'no-images.json')
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'process_unverified')
        bad = self.world.root / 'bad.bin'
        _write(bad, b'this is not a macho file')
        self.world.process.image_paths = [
            os.path.realpath(self.world.exe), os.path.realpath(bad), OS_IMAGE]
        code, doc, _out = self.world.do_seal(self.world.root / 'bad-macho.json')
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'macho')

    def test_image_bytes_and_live_set_are_rechecked(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        text = _csv(sealed['source_build'])
        exe = Path(self.world.process.executable_path)
        original = exe.read_bytes()
        exe.write_bytes(original + b'\x00')
        status, doc, _capture, _before = self.world.do_attest(seal_path, text, self.world.root / 'exe.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'image_hash')
        self.assertEqual(sealed['source_build'], source_build('Release'))
        exe.write_bytes(original)
        extra = self.world.root / 'extra.dylib'
        _write(extra, macho(6, identity='extra.dylib'))
        self.world.process.image_paths = list(self.world.process.image_paths) + [os.path.realpath(extra)]
        status, doc, _capture, _before = self.world.do_attest(seal_path, text, self.world.root / 'set.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'image_set')
        self.world.process.image_paths = self.world.process.image_paths[:-1]
        self.world.process.is_alive = False
        self.world.dylib.write_bytes(self.world.dylib.read_bytes() + b'\x00')
        status, doc, _capture, _before = self.world.do_attest(seal_path, text, self.world.root / 'dead.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'image_hash')

    def test_dead_process_rehashes_the_sealed_set_only(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        self.world.process.is_alive = False
        extra = self.world.root / 'later.dylib'
        _write(extra, macho(6, identity='later.dylib'))
        self.world.process.image_paths = list(self.world.process.image_paths) + [os.path.realpath(extra)]
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'dead-ok.json')
        self.assertEqual(status, 0, doc)
        self.assertTrue(doc['measurement_accepted'])
        self.assertNotIn(os.path.realpath(extra), json.dumps(doc))

    def test_timeouts_write_an_unverified_sidecar(self):
        calls = []
        code, doc, _out = self.world.do_seal(timeout_seconds=0, after_lstat=lambda path: calls.append(path))
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])
        self.assertEqual(calls, [])

        clock_box = {'now': 0.0}

        def clock():
            return clock_box['now']

        real_read = os.read

        def wrapped(fd, size):
            data = real_read(fd, size)
            if size == CHUNK and len(data) == CHUNK:
                clock_box['now'] = 1000.0
            return data

        big = Runtime(Path(tempfile.mkdtemp(dir=self.world.root)), db=b'B' * (CHUNK + 32))
        with mock.patch('tools.frame_time_provenance.os.read', wrapped):
            code, doc, _out = big.do_seal(timeout_seconds=5, clock=clock)
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])
        self.assertNotEqual(doc['result'], 'verified')

    def test_same_runtime_fsltx_is_accepted_and_redirects_fail_closed(self):
        fs = os.path.realpath(self.world.runtime / 'fsgame.ltx')
        self.world._write_launcher(424242, ['xr_3da', '-fsltx', fs])
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        self.assertTrue(sealed['asset_verified'])
        self.assertFalse(sealed['live_runtime_attested'])
        self.assertEqual(sealed['fsltx'], fs)
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'fsltx-attest.json')
        self.assertEqual(status, 0, doc)
        self.assertTrue(doc['measurement_accepted'])
        self.world._write_launcher(424242, ['xr_3da', '-fsltx fsgame.ltx'])
        code, sealed, _out = self.world.do_seal(self.world.root / 'fsltx-relative.json')
        self.assertEqual(code, 0, sealed)
        self.assertEqual(sealed['fsltx'], fs)
        self.world._write_launcher(424242, ['xr_3da', '-fsltx=/other/fsgame.ltx'])
        code, sealed, _out = self.world.do_seal(self.world.root / 'fsltx-equals.json')
        self.assertEqual(code, 0, sealed)
        self.assertIsNone(sealed['fsltx'])

        other = self.world.root / 'other'
        other.mkdir()
        (other / 'fsgame.ltx').write_text('$app_data_root$=true|false|/other/\n')
        foreign = str(other / 'fsgame.ltx')
        self.world._write_launcher(424242, ['xr_3da', '-fsltx', foreign])
        code, doc, _out = self.world.do_seal(self.world.root / 'fsltx-foreign.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(doc['result'], 'mismatch')
        self.assertEqual(_failure(doc), 'filesystem_redirect')
        self.assertFalse(doc['asset_verified'])
        self.assertNotIn('managed_files', doc)
        self.world._write_launcher(424242, ['xr_3da', f'-fsltx {foreign}'])
        code, doc, _out = self.world.do_seal(self.world.root / 'fsltx-one-element.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'filesystem_redirect')
        self.world._write_launcher(424242, ['xr_3da', '-overlaypath', str(other)])
        code, doc, out = self.world.do_seal(self.world.root / 'overlay.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'filesystem_redirect')
        self.assertFalse(doc['asset_verified'])
        status, attest_doc, _capture, _before = self.world.do_attest(
            out, _csv(source_build('Release')), self.world.root / 'overlay-attest.json')
        self.assertEqual(status, 2, attest_doc)
        self.assertEqual(_failure(attest_doc), 'seal_not_verified')
        self.world._write_launcher(424242, ['xr_3da', '-fsltx'])
        code, doc, _out = self.world.do_seal(self.world.root / 'fsltx-missing.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'filesystem_redirect')

    def test_failed_vmmap_is_not_a_verified_live_observation(self):
        exe = os.path.realpath(self.world.exe)
        dylib = os.path.realpath(self.world.dylib)
        os_image = os.path.realpath(OS_IMAGE)

        def failed(args, **kwargs):
            self.assertEqual(args[0], '/usr/bin/vmmap')
            self.assertGreater(kwargs['timeout'], 0)
            self.assertLessEqual(kwargs['timeout'], 5)
            return subprocess.CompletedProcess(args, 1, stdout=f'Path: {exe}\n', stderr='unable')

        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=exe), \
                mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=failed):
            code, doc, _out = self.world.do_seal(process=LiveProcess(), timeout_seconds=5)
        self.assertEqual(code, 2, doc)
        self.assertEqual(doc['result'], 'unknown')
        self.assertEqual(_failure(doc), 'process_unverified')
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['live_runtime_attested'])
        self.assertNotIn('images', doc)

        def header(args, **_kwargs):
            return subprocess.CompletedProcess(args, 0, stdout=f'Path: {exe}\n', stderr='')

        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=exe), \
                mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=header):
            code, doc, _out = self.world.do_seal(
                self.world.root / 'header.json', process=LiveProcess())
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'process_unverified')
        self.assertFalse(doc['live_runtime_attested'])

        regions = (
            f'Path: {exe}\n'
            f'__TEXT 1000-2000 [4K] r-x/r-x SM=COW {exe}\n'
            f'__DATA 3000-4000 [4K] rw-/rw- SM=PRV {dylib}\n'
            f'__TEXT 5000-6000 [4K] r-x/r-x SM=COW {os_image}\n'
        )

        def mapped(args, **kwargs):
            self.assertGreater(kwargs['timeout'], 0)
            self.assertLessEqual(kwargs['timeout'], 5)
            return subprocess.CompletedProcess(args, 0, stdout=regions, stderr='')

        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=exe), \
                mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=mapped):
            found = LiveProcess().images(424242, deadline=Deadline(5))
        self.assertEqual(found, [exe, dylib, os_image])
        code, sealed, _out = self.world.do_seal(self.world.root / 'fixture-images.json')
        self.assertEqual(code, 0, sealed)
        hashed = [item['path'] for item in sealed['images']]
        self.assertEqual(hashed, [exe, dylib])
        self.assertIn(os_image, sealed['os_images'])
        self.assertFalse(sealed['live_runtime_attested'])
        self.assertEqual(sealed['process_observation'], 'fixture')

        def expired(args, **kwargs):
            raise subprocess.TimeoutExpired(args, kwargs.get('timeout'))

        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=exe), \
                mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=expired):
            code, doc, _out = self.world.do_seal(
                self.world.root / 'vmmap-timeout.json', process=LiveProcess(), timeout_seconds=5)
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])

    def test_launcher_identity_binds_pid_argv_and_record_bytes(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        raw = self.world.launcher.read_bytes()
        self.assertEqual(sealed['launcher_pid'], 424242)
        self.assertEqual(sealed['launcher_argv'], ['xr_3da'])
        self.assertEqual(sealed['launcher_cwd'], os.path.realpath(self.world.runtime))
        self.assertEqual(sealed['launcher_sha256'], hashlib.sha256(raw).hexdigest())
        text = _csv(sealed['source_build'])
        self.world._write_launcher(424243)
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, text, self.world.root / 'pid.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'launcher_identity')
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['measurement_accepted'])
        self.world._write_launcher(424242, ['xr_3da', '-dbg'])
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, text, self.world.root / 'argv.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'launcher_identity')
        self.assertIn('argv', doc['failures'][0]['detail'])
        rewritten = json.loads(raw)
        self.world.launcher.write_text(json.dumps(rewritten, indent=4) + '\n')
        self.world.process = self.world.process_type(
            424242, os.path.realpath(self.world.exe),
            [os.path.realpath(self.world.exe), os.path.realpath(self.world.dylib), OS_IMAGE])
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, text, self.world.root / 'digest.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'launcher_identity')
        self.assertIn('bytes', doc['failures'][0]['detail'])

    def test_same_size_inplace_mutation_is_stat_race(self):
        target = self.world.runtime / 'database/config/tiny.db'
        devino = (target.stat().st_dev, target.stat().st_ino)
        original = os.read
        changed = []

        def read(fd, size):
            data = original(fd, size)
            info = os.fstat(fd)
            if data and (info.st_dev, info.st_ino) == devino and not changed:
                changed.append(True)
                target.write_bytes(b'X' * len(self.world.db))
            return data

        with mock.patch('tools.frame_time_provenance.os.read', side_effect=read):
            code, doc, _out = self.world.do_seal()
        self.assertEqual(code, 2, doc)
        self.assertEqual(doc['result'], 'unknown')
        self.assertEqual(_failure(doc), 'stat_race')
        self.assertFalse(doc['asset_verified'])
        self.assertNotEqual(doc['result'], 'verified')
        self.assertNotIn('managed_files', doc)
        self.assertTrue(changed)
        self.assertEqual(len(target.read_bytes()), len(self.world.db))
        self.assertNotEqual(hashlib.sha256(target.read_bytes()).hexdigest(), _sha(self.world.db))

    def test_deadline_covers_source_build_and_is_not_verified(self):
        clock = {'now': 0.0}
        real = source_build

        def slow(config):
            identity = real(config)
            clock['now'] = 1000.0
            return identity

        with mock.patch('tools.frame_time_provenance.source_build', side_effect=slow):
            code, doc, _out = self.world.do_seal(timeout_seconds=5, clock=lambda: clock['now'])
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])
        self.assertNotEqual(doc['result'], 'verified')

        def git_timeout(args, **kwargs):
            self.assertEqual(args[0], 'git')
            self.assertGreater(kwargs['timeout'], 0)
            self.assertLessEqual(kwargs['timeout'], 5)
            raise subprocess.TimeoutExpired(args, kwargs['timeout'])

        with mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=git_timeout):
            code, doc, _out = self.world.do_seal(self.world.root / 'git-timeout.json', timeout_seconds=5)
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])

        clock['now'] = 0.0
        real_read = Path.read_bytes

        def read_bytes(path_self):
            data = real_read(path_self)
            if path_self.name == 'FrameTimeCollector.h':
                clock['now'] = 1000.0
            return data

        with mock.patch.object(Path, 'read_bytes', read_bytes):
            code, doc, _out = self.world.do_seal(
                self.world.root / 'source-read-timeout.json', timeout_seconds=5, clock=lambda: clock['now'])
        self.assertEqual(code, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertFalse(doc['asset_verified'])

    def test_attest_deadline_covers_report_evaluation(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        clock = {'now': 0.0}
        real = evaluate_frame_time_measurement

        def slow(text):
            result = real(text)
            clock['now'] = 1000.0
            return result

        with mock.patch('tools.frame_time_provenance.evaluate_frame_time_measurement', side_effect=slow):
            status, doc, _capture, _before = self.world.do_attest(
                seal_path, _csv(sealed['source_build']), self.world.root / 'attest-deadline.json',
                timeout_seconds=5, clock=lambda: clock['now'])
        self.assertEqual(status, 3, doc)
        self.assertEqual(doc['result'], 'timeout')
        self.assertEqual(_failure(doc), 'timeout')
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['measurement_accepted'])
        self.assertFalse(doc['live_runtime_attested'])
        self.assertTrue((self.world.root / 'attest-deadline.json').is_file())

    def test_vmmap_keeps_spaced_paths_and_rejects_hyphen_headers(self):
        exe = self.world.root / 'a-b' / 'xr_3da'
        lib = self.world.root / 'extra images' / 'libfix.dylib'
        exe.parent.mkdir()
        lib.parent.mkdir()
        self.world.exe.rename(exe)
        self.world.dylib.rename(lib)
        self.world.exe = exe
        self.world.dylib = lib
        self.world._write_launcher(424242)
        self.assertIsNone(images_from_vmmap_text(f'Path: {exe}\n', str(exe)))

        original_run = subprocess.run

        def run_vmmap(stdout, returncode=0):
            def run(args, **kwargs):
                if args[0] == '/usr/bin/vmmap':
                    return subprocess.CompletedProcess(args, returncode, stdout=stdout, stderr='')
                return original_run(args, **kwargs)
            return run

        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=str(exe)), \
                mock.patch('tools.frame_time_provenance.subprocess.run',
                           side_effect=run_vmmap(f'Path: {exe}\n')):
            code, doc, _out = self.world.do_seal(process=LiveProcess())
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'process_unverified')
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['live_runtime_attested'])

        regions = (
            f'Path: {exe}\n'
            f'__TEXT 1000-2000 [4K] r-x/r-x SM=COW {exe}\n'
            f'__DATA 3000-4000 [4K] rw-/rw- SM=PRV {lib}\n'
        )
        self.assertEqual(images_from_vmmap_text(regions, str(exe)), [str(exe), str(lib)])
        broken = f'__TEXT 1000-2000 {lib}\n__TEXT 3000-4000 [4K] r-x/r-x SM=COW {exe}\n'
        self.assertIsNone(images_from_vmmap_text(broken, str(exe)))
        with mock.patch.object(LiveProcess, 'alive', return_value=True), \
                mock.patch.object(LiveProcess, 'executable', return_value=str(exe)), \
                mock.patch('tools.frame_time_provenance.subprocess.run', side_effect=run_vmmap(regions)):
            code, sealed, seal_path = self.world.do_seal(
                self.world.root / 'space-seal.json', process=LiveProcess())
            self.assertEqual(code, 0, sealed)
            hashed = {item['path'] for item in sealed['images']}
            self.assertIn(os.path.realpath(exe), hashed)
            self.assertIn(os.path.realpath(lib), hashed)
            lib.write_bytes(b'CHANGED mapped library')
            status, doc, _capture, _before = self.world.do_attest(
                seal_path, _csv(sealed['source_build']), self.world.root / 'space-attest.json',
                process=LiveProcess())
        self.assertNotEqual(status, 0, doc)
        self.assertFalse(doc['measurement_accepted'])
        self.assertFalse(doc['asset_verified'])

    def test_missing_fsgame_membership_writes_a_sidecar(self):
        manifest_path = self.world.runtime / 'stage-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        del manifest['files']['fsgame.ltx']
        manifest_path.write_text(json.dumps(manifest))
        code, doc, out = self.world.do_seal()
        self.assertEqual(code, 1, doc)
        self.assertEqual(doc['result'], 'mismatch')
        self.assertEqual(_failure(doc), 'membership')
        self.assertFalse(doc['asset_verified'])
        self.assertTrue(out.is_file())
        self.assertIn('fsgame.ltx', doc['failures'][0]['detail'])

    def test_real_stager_fixture_keeps_patch_precedence(self):
        from tools import stage_coc_runtime as stager
        # stage() rejects a destination whose lexical ancestor is a symlink.
        # /var is a symlink to /private/var, so the fixture output is the resolved path.
        root = Path(self.world.root).resolve()
        fake_repo = root / 'fixture-repo'
        (fake_repo / 'res/gamedata/shaders/gl').mkdir(parents=True)
        (fake_repo / 'res/fsgame.ltx').write_bytes((stager.REPOSITORY / 'res/fsgame.ltx').read_bytes())
        (fake_repo / 'res/gamedata/shaders/gl/test.glsl').write_bytes(b'target shader fixture')
        for part in ('maps', 'resource', 'sound'):
            _write(self.world.base / 'database' / part / 'fixture.db', part.encode())
        _write(self.world.patch / 'database/config/scripts.db', b'patch scripts.db')
        _write(self.world.patch / 'database/config/configs.db', b'patch configs.db')
        _write(self.world.base / 'database/config/scripts.db', b'base scripts replaced by patch')
        runtime = root / 'real-staged-runtime'
        with mock.patch.object(stager, 'REPOSITORY', fake_repo):
            stager.stage(self.world.base, self.world.patch, runtime)
        self.world.runtime = runtime
        self.world._write_launcher(424242)
        code, sealed, _out = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        self.assertTrue(sealed['asset_verified'])
        self.assertFalse(sealed['live_runtime_attested'])
        self.assertEqual(sealed['process_observation'], 'fixture')
        record = json.loads((runtime / 'stage-manifest.json').read_text())
        source = Path(record['files']['database/config/scripts.db']['source'])
        self.assertEqual(source.resolve(), (self.world.patch / 'database/config/scripts.db').resolve())

    def test_measurement_rejects_invalid_empty_and_visibility_cases(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        identity = sealed['source_build']
        cases = {
            'truncated': _csv(identity, truncated='true', invalid_loops='1', capacity='8'),
            'invalid': _csv(identity, invalid_loops='1'),
            'empty': capture_text(records=[], metadata_changes={
                'content_manifest': 'unknown', 'overlay': 'unknown', 'source_build': identity,
                'renderer': 'OpenGL', 'frame_limit': '501',
            }),
            'menu': capture_text(records=[record(0, 0, 100, flags=2)], metadata_changes={
                'content_manifest': 'unknown', 'overlay': 'unknown', 'source_build': identity,
                'renderer': 'OpenGL', 'frame_limit': '501',
            }),
            'visibility': capture_text(
                records=[record(0, 0, 100, flags=0), record(1, 100, 100, flags=64)],
                metadata_changes={
                    'content_manifest': 'unknown', 'overlay': 'unknown', 'source_build': identity,
                    'renderer': 'OpenGL', 'frame_limit': '501',
                }),
            'clock': _csv(identity, clock_is_steady='false'),
        }
        expected = {
            'truncated': 'truncated',
            'invalid': 'invalid_loops',
            'empty': 'empty_capture',
            'menu': 'empty_selection',
            'visibility': 'window_visibility_unknown',
            'clock': 'schema',
        }
        for name, text in cases.items():
            with self.subTest(name=name):
                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / f'{name}.json')
                self.assertEqual(status, 0, doc)
                self.assertTrue(doc['asset_verified'])
                self.assertFalse(doc['measurement_accepted'])
                self.assertIn(expected[name], doc['reasons'])

    def test_pasted_checksum_is_an_asset_mismatch(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        text = _csv(sealed['source_build'], content_manifest=sealed['manifest_sha256'])
        status, doc, capture, before = self.world.do_attest(seal_path, text, self.world.root / 'paste.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'asset_field')
        self.assertTrue(doc['files_matched'])
        self.assertFalse(doc['asset_verified'])
        self.assertEqual(capture.read_bytes(), before)
        self.assertEqual((self.world.runtime / 'database/config/tiny.db').read_bytes(), self.world.db)

    def test_source_build_mismatch_and_evidence_tamper(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        wrong = 'git:deadbeef,telemetry_sha256:' + ('ab' * 32) + ',config:Release'
        status, doc, capture, before = self.world.do_attest(
            seal_path, _csv(wrong), self.world.root / 'source.json')
        self.assertEqual(status, 1, doc)
        self.assertEqual(_failure(doc), 'source_build')
        self.assertTrue(doc['files_matched'])
        self.assertEqual(capture.read_bytes(), before)
        status, doc, capture, before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'good.json')
        self.assertEqual(status, 0, doc)
        evidence = self.world.root / 'good.evidence.csv'
        evidence.write_text(evidence.read_text() + '# tampered\n')
        out = self.world.root / 'tamper.json'
        code = attest(self.world.runtime, self.world.launcher, seal_path, capture, evidence, 30, out,
                      process=self.world.process)
        tampered = json.loads(out.read_text())
        self.assertEqual(code, 1, tampered)
        self.assertEqual(_failure(tampered), 'evidence')
        self.assertTrue(tampered['files_matched'])
        self.assertEqual(capture.read_bytes(), before)

    def test_corrupt_seal_and_existing_output_are_not_rewritten(self):
        seal_path = self.world.root / 'broken-seal.json'
        seal_path.write_bytes(b'{')
        out = self.world.root / 'attest.json'
        code = attest(self.world.runtime, self.world.launcher, seal_path,
                      self.world.root / 'missing-capture.csv', self.world.root / 'missing-evidence.csv',
                      30, out, process=self.world.process)
        doc = json.loads(out.read_text())
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'seal_corrupt')
        self.assertEqual(seal_path.read_bytes(), b'{')
        existing = self.world.root / 'existing.json'
        existing.write_bytes(b'keep')
        code = seal(self.world.runtime, self.world.launcher, 'Release', 0, 30, existing,
                    process=self.world.process)
        self.assertEqual(code, 2)
        self.assertEqual(existing.read_bytes(), b'keep')

    def test_unbound_log_change_stays_verified(self):
        code, sealed, seal_path = self.world.do_seal()
        self.assertEqual(code, 0, sealed)
        _write(self.world.runtime / 'appdata/logs/note.txt', b'log\n')
        status, doc, _capture, _before = self.world.do_attest(
            seal_path, _csv(sealed['source_build']), self.world.root / 'logs.json')
        self.assertEqual(status, 0, doc)
        self.assertTrue(doc['asset_verified'])
        self.assertEqual(doc['unbound_changes'], ['logs'])

    def test_cli_does_not_hash_a_dead_process_or_accept_binary_flag(self):
        pid = _unused_pid()
        self.world._write_launcher(pid)
        decoy = _sha(self.world.exe.read_bytes())
        out = self.world.root / 'cli-seal.json'
        proc = subprocess.run(
            [sys.executable, str(REPO / 'tools/frame_time_provenance.py'), 'seal',
             '--runtime', str(self.world.runtime), '--launcher-record', str(self.world.launcher),
             '--config', 'Release', '--settle-seconds', '0', '--timeout-seconds', '30',
             '--out', str(out)],
            cwd=REPO, capture_output=True, text=True, check=False)
        self.assertEqual(proc.returncode, 2, proc.stderr)
        doc = json.loads(out.read_text())
        self.assertEqual(_failure(doc), 'process_unverified')
        self.assertNotIn(decoy, out.read_text())
        with self.assertRaises(SystemExit):
            with contextlib.redirect_stderr(io.StringIO()):
                main(['seal', '--binary', str(self.world.exe), '--runtime', str(self.world.runtime),
                      '--launcher-record', str(self.world.launcher), '--config', 'Release',
                      '--settle-seconds', '0', '--out', str(self.world.root / 'nope.json')])

    def test_missing_launcher_does_not_hash_name_matched_binaries(self):
        names = ('xr_3da', 'xrCore.dylib', 'xrEngine.dylib', 'xrRender_GL.dylib', 'xrGame.dylib')
        hashes = []
        for name in names:
            path = self.world.runtime / name
            _write(path, macho(2, loads=(name,)))
            hashes.append(_sha(path.read_bytes()))
        out = self.world.root / 'no-launcher.json'
        code = seal(self.world.runtime, self.world.root / 'missing-launcher.json', 'Release', 0, 30, out,
                    process=LiveProcess())
        doc = json.loads(out.read_text())
        self.assertEqual(code, 2, doc)
        self.assertEqual(_failure(doc), 'launcher')
        body = out.read_text()
        for digest in hashes:
            self.assertNotIn(digest, body)

    def test_symlink_runtime_root_is_refused(self):
        link = self.world.root / 'linked-runtime'
        link.symlink_to(self.world.runtime, target_is_directory=True)
        out = self.world.root / 'linked.json'
        code = seal(link, self.world.launcher, 'Release', 0, 30, out, process=self.world.process)
        doc = json.loads(out.read_text())
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'symlink')

    def test_bridge_socket_seals_the_launcher_endpoint(self):
        rel = 'appdata/prov.sock'
        sock = _listen(self.world.runtime, rel)
        try:
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', rel])
            code, sealed, seal_path = self.world.do_seal()
            self.assertEqual(code, 0, sealed)
            info = os.lstat(self.world.runtime.joinpath(*rel.split('/')))
            self.assertEqual(sealed['bridge_socket'], {
                'path': rel,
                'dev': str(info.st_dev),
                'ino': str(info.st_ino),
                'association': 'launcher-record',
            })
            self.assertFalse(sealed['live_runtime_attested'])
            self.assertNotIn(rel, [item['path'] for item in sealed['managed_files']])
            self.assertNotIn(rel, sealed['settings_paths'])
            text = _csv(sealed['source_build'])
            status, doc, _capture, _before = self.world.do_attest(
                seal_path, text, self.world.root / 'bridge-attest.json')
            self.assertEqual(status, 0, doc)
            self.assertTrue(doc['asset_verified'])
            self.assertTrue(doc['measurement_accepted'])
            self.assertFalse(doc['live_runtime_attested'])
        finally:
            sock.close()
        os.unlink(self.world.runtime.joinpath(*rel.split('/')))

        default = 'appdata/agent_bridge.sock'
        default_sock = _listen(self.world.runtime, default)
        try:
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', '-ltx', 'user.ltx'])
            code, sealed, _out = self.world.do_seal(self.world.root / 'default-bridge.json')
            self.assertEqual(code, 0, sealed)
            self.assertEqual(sealed['bridge_socket']['path'], default)
            self.assertEqual(sealed['bridge_socket']['association'], 'launcher-record')
            self.assertFalse(sealed['live_runtime_attested'])
        finally:
            default_sock.close()
        os.unlink(self.world.runtime.joinpath(*default.split('/')))

        again = _listen(self.world.runtime, rel)
        try:
            lexical = str(os.path.realpath(self.world.runtime.joinpath(*rel.split('/'))))
            via_var = lexical
            if lexical.startswith('/private/var/'):
                via_var = '/var/' + lexical[len('/private/var/'):]
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', via_var])
            code, sealed, _out = self.world.do_seal(self.world.root / 'absolute-bridge.json')
            self.assertEqual(code, 0, sealed)
            self.assertEqual(sealed['bridge_socket']['path'], rel)
            self.assertEqual(sealed['bridge_socket']['dev'], str(os.lstat(lexical).st_dev))
            self.assertEqual(sealed['bridge_socket']['ino'], str(os.lstat(lexical).st_ino))
        finally:
            again.close()

    def test_bridge_socket_rejects_substitutes(self):
        rel = 'appdata/prov.sock'
        runtime = self.world.runtime
        target = _listen(runtime, 'appdata/real.sock')
        try:
            link = runtime / 'appdata/prov.sock'
            link.symlink_to('real.sock')
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', rel])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-link.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'symlink')
            self.assertEqual(doc['failures'][0]['path'], rel)
            link.unlink()
        finally:
            target.close()
        os.unlink(runtime / 'appdata/real.sock')

        os.mkfifo(runtime / rel)
        code, doc, _out = self.world.do_seal(self.world.root / 'sock-fifo.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'nonregular')
        self.assertIn('not a socket', doc['failures'][0]['detail'])
        os.remove(runtime / rel)

        _write(runtime / rel, b'regular-bytes')
        code, doc, _out = self.world.do_seal(self.world.root / 'sock-file.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'nonregular')
        self.assertEqual(doc['failures'][0]['path'], rel)
        self.assertIn('not a socket', doc['failures'][0]['detail'])
        os.remove(runtime / rel)

        endpoint = _listen(runtime, rel)
        extra = _listen(runtime, 'appdata/extra.sock')
        try:
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-extra.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'nonregular')
            self.assertEqual(doc['failures'][0]['path'], 'appdata/extra.sock')
            self.assertEqual(doc['failures'][0]['detail'], 'not a regular file')
        finally:
            endpoint.close()
            extra.close()
        os.unlink(runtime / rel)
        os.unlink(runtime / 'appdata/extra.sock')

        parked = _listen(runtime, rel)
        try:
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', 'appdata/../../prov.sock'])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-escape.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'ambiguous_path')
            self.assertFalse(doc['asset_verified'])
            outside = self.world.root / 'outside.sock'
            foreign = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            foreign.bind(str(outside))
            foreign.listen(1)
            try:
                self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', str(outside)])
                code, doc, _out = self.world.do_seal(self.world.root / 'sock-outside.json')
                self.assertEqual(code, 1, doc)
                self.assertEqual(_failure(doc), 'unexpected_path')
                self.assertFalse(doc['asset_verified'])
            finally:
                foreign.close()
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge=appdata/prov.sock'])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-glued.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'unexpected_path')
            self.world._write_launcher(424242, ['xr_3da'])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-unflagged.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'nonregular')
            self.assertEqual(doc['failures'][0]['path'], rel)
            self.assertEqual(doc['failures'][0]['detail'], 'not a regular file')
            self.world._write_launcher(424242, ['-agent_bridge', rel])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-argv0.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'nonregular')
            self.assertEqual(doc['failures'][0]['detail'], 'not a regular file')
        finally:
            parked.close()
        os.unlink(runtime / rel)

        self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', rel])
        code, doc, _out = self.world.do_seal(self.world.root / 'sock-missing.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'nonregular')
        self.assertIn('missing', doc['failures'][0]['detail'])

        hidden = _listen(runtime, 'appdata/.bridge.sock')
        try:
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', 'appdata/.bridge.sock'])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-dot.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'unexpected_path')
            self.assertEqual(doc['failures'][0]['detail'], 'dotfile')
        finally:
            hidden.close()

        elsewhere = self.world.root / 'elsewhere'
        elsewhere.mkdir()
        jumped = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        jumped.bind(str(elsewhere / 'sock'))
        jumped.listen(1)
        try:
            (runtime / 'appdata/jump').symlink_to(elsewhere, target_is_directory=True)
            self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', 'appdata/jump/sock'])
            code, doc, _out = self.world.do_seal(self.world.root / 'sock-ancestor.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'symlink')
            self.assertEqual(doc['failures'][0]['path'], 'appdata/jump')
        finally:
            jumped.close()

    def test_bridge_socket_inode_is_launcher_identity(self):
        rel = 'appdata/prov.sock'
        sock = _listen(self.world.runtime, rel)
        self.world._write_launcher(424242, ['xr_3da', '-agent_bridge', rel])
        try:
            code, sealed, seal_path = self.world.do_seal()
            self.assertEqual(code, 0, sealed)
            text = _csv(sealed['source_build'])
        finally:
            sock.close()
        path = self.world.runtime.joinpath(*rel.split('/'))
        os.unlink(path)
        replacement = _listen(self.world.runtime, rel)
        try:
            status, doc, _capture, _before = self.world.do_attest(
                seal_path, text, self.world.root / 'bridge-replaced.json')
            self.assertEqual(status, 1, doc)
            self.assertEqual(_failure(doc), 'launcher_identity')
            self.assertIn('bridge socket', doc['failures'][0]['detail'])
            self.assertFalse(doc['asset_verified'])
            self.assertFalse(doc['measurement_accepted'])
        finally:
            replacement.close()

    def test_live_observation_requires_the_bound_fd(self):
        rel = 'appdata/prov.sock'
        sock = _listen(self.world.runtime, rel)
        pid = _unused_pid()
        exe = os.path.realpath(self.world.exe)
        images = [exe, os.path.realpath(self.world.dylib), os.path.realpath(OS_IMAGE)]

        class Claim:
            observation = 'live'

            def alive(self, seen):
                return seen == pid

            def executable(self, seen):
                return exe if seen == pid else None

            def images(self, seen, deadline=None):
                del deadline
                return list(images) if seen == pid else None

        try:
            self.world._write_launcher(pid, ['xr_3da', '-agent_bridge', rel])
            code, doc, _out = self.world.do_seal(self.world.root / 'live-unowned.json', process=Claim())
            self.assertEqual(code, 2, doc)
            self.assertEqual(_failure(doc), 'process_unverified')
            self.assertFalse(doc['asset_verified'])
            self.assertFalse(doc['live_runtime_attested'])
        finally:
            sock.close()

    def test_stale_fd_does_not_own_the_replacement_inode(self):
        rel = 'appdata/prov.sock'
        first = _listen(self.world.runtime, rel)
        leaf = self.world.runtime.joinpath(*rel.split('/'))
        original = os.lstat(leaf).st_ino
        os.unlink(leaf)
        second = _listen(self.world.runtime, rel)
        replacement = os.lstat(leaf).st_ino
        second.close()
        self.assertNotEqual(original, replacement)
        self.world._write_launcher(os.getpid(), ['xr_3da', '-agent_bridge', rel])

        class SimulatedLive(self.world.process_type):
            """Image list is simulated. This is not a vmmap of the live pid."""

            observation = 'live'
            images_are_simulated = True

        view = SimulatedLive(
            os.getpid(), self.world.process.executable_path, list(self.world.process.image_paths))
        self.assertTrue(view.images_are_simulated)
        self.assertIs(SimulatedLive.images, self.world.process_type.images)
        try:
            self.assertTrue(_process_owns_bridge_socket(os.getpid(), [rel]))
            record = _bridge_socket(
                self.world.runtime.resolve(), json.loads(self.world.launcher.read_text()),
                view, require_live_fd=True)
            self.assertEqual(record['association'], 'path-only')
            self.assertNotEqual(record['association'], 'live-fd')
            self.assertEqual(record['ino'], str(replacement))
            self.assertNotEqual(record['ino'], str(original))
            code, doc, _out = self.world.do_seal(self.world.root / 'stale-fd.json', process=view)
            self.assertEqual(code, 2, doc)
            self.assertEqual(_failure(doc), 'process_unverified')
            self.assertIn('current socket inode', doc['failures'][0]['detail'])
            self.assertFalse(doc['asset_verified'])
            self.assertFalse(doc['live_runtime_attested'])
        finally:
            first.close()

    def test_current_socket_is_not_live_attested_without_inode_correlation(self):
        rel = 'appdata/prov.sock'
        sock = _listen(self.world.runtime, rel)
        self.world._write_launcher(os.getpid(), ['xr_3da', '-agent_bridge', rel])

        class SimulatedLive(self.world.process_type):
            """Image list is simulated. This is not a vmmap of the live pid."""

            observation = 'live'
            images_are_simulated = True

        view = SimulatedLive(
            os.getpid(), self.world.process.executable_path, list(self.world.process.image_paths))
        try:
            self.assertTrue(view.images_are_simulated)
            self.assertTrue(_process_owns_bridge_socket(os.getpid(), [rel]))
            code, doc, _out = self.world.do_seal(self.world.root / 'current-fd.json', process=view)
            self.assertEqual(code, 2, doc)
            self.assertEqual(_failure(doc), 'process_unverified')
            self.assertFalse(doc['live_runtime_attested'])
            self.assertFalse(doc['asset_verified'])
        finally:
            sock.close()


    def _bind_external(self, directory: Path, leaf: str = 'b.sock'):
        path = directory / leaf
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.bind(str(path))
        sock.listen(1)
        return sock, path

    def test_external_directory_seals_a_real_bound_socket(self):
        with tempfile.TemporaryDirectory(prefix='oxv-', dir='/private/tmp') as name:
            directory = Path(name)
            directory.chmod(0o700)
            self.assertEqual(os.path.realpath(directory), str(directory))
            sock, endpoint = self._bind_external(directory)
            try:
                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', str(endpoint)],
                    external_communication_directory=str(directory))
                self.assertEqual(
                    _ordinary_external_socket(
                        ['-agent_bridge', 'xr_3da', '-agent_bridge', str(endpoint)], str(directory)),
                    str(endpoint))
                code, sealed, seal_path = self.world.do_seal()
                self.assertEqual(code, 0, sealed)
                info = os.lstat(endpoint)
                parent = os.lstat(directory)
                self.assertEqual(sealed['bridge_socket'], {
                    'path': str(endpoint),
                    'dev': str(info.st_dev),
                    'ino': str(info.st_ino),
                    'association': 'external-observation',
                    'directory': {
                        'path': str(directory),
                        'dev': str(parent.st_dev),
                        'ino': str(parent.st_ino),
                        'mode': '0700',
                        'uid': str(parent.st_uid),
                    },
                })
                self.assertNotEqual(sealed['bridge_socket']['association'], 'live-fd')
                self.assertFalse(sealed['live_runtime_attested'])
                self.assertEqual(sealed['process_observation'], 'fixture')
                self.assertNotIn(str(endpoint), [item['path'] for item in sealed['managed_files']])
                echoed = inspect_runtime(
                    self.world.runtime, self.world.launcher, config='Release',
                    process=self.world.process, deadline=Deadline(30),
                    external_communication_directory=str(directory))
                self.assertEqual(echoed['bridge_socket']['association'], 'external-observation')
                omitted = inspect_runtime(
                    self.world.runtime, self.world.launcher, config='Release',
                    process=self.world.process, deadline=Deadline(30))
                self.assertEqual(omitted['bridge_socket']['path'], str(endpoint))
                text = _csv(sealed['source_build'])
                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / 'external-attest.json')
                self.assertEqual(status, 0, doc)
                self.assertTrue(doc['measurement_accepted'])
                self.assertFalse(doc['live_runtime_attested'])
            finally:
                sock.close()

    def test_external_directory_rejects_argv_overlap_symlink_mode_and_owner(self):
        with tempfile.TemporaryDirectory(prefix='oxv-', dir='/private/tmp') as name:
            directory = Path(name)
            directory.chmod(0o700)
            endpoint = directory / 'b.sock'
            cases = [
                (['xr_3da', '-agent_bridge', str(directory / 'other.sock'),
                  '-agent_bridge', str(endpoint)], 'single ordinary'),
                (['xr_3da', '-agent_bridge=' + str(endpoint)], 'single ordinary'),
                (['xr_3da', '-agent_bridge', str(directory / 'a b.sock')], 'whitespace'),
                (['xr_3da', '-agent_bridge', str(directory / ('a' * 120))], '103 bytes'),
                (['xr_3da', '-agent_bridge', 'appdata/b.sock'], 'socket leaf'),
                (['xr_3da'], 'single ordinary'),
            ]
            for index, (argv, detail) in enumerate(cases):
                self.world._write_launcher(
                    424242, argv, external_communication_directory=str(directory))
                code, doc, _out = self.world.do_seal(self.world.root / f'argv-{index}.json')
                self.assertEqual(code, 1, doc)
                self.assertEqual(_failure(doc), 'external_directory')
                self.assertIn(detail, doc['failures'][0]['detail'])
                self.assertFalse(doc['asset_verified'])
                self.assertFalse(doc['live_runtime_attested'])

            runtime = os.path.realpath(self.world.runtime)
            base = os.path.realpath(self.world.base)
            appdata = os.path.realpath(self.world.runtime / 'appdata')
            hold = self.world.runtime / 'hold'
            hold.mkdir()
            # The runtime contains appdata, so that ancestor hit is the settings closure.
            # `hold` is inside the runtime and outside the settings and alias trees.
            for index, (declared, detail) in enumerate((
                (runtime, 'overlaps a settings closure'),
                (base, 'overlaps a manifest root'),
                (appdata, 'overlaps a settings closure'),
                (os.path.realpath(hold), 'overlaps the runtime'),
            )):
                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', declared + '/b.sock'],
                    external_communication_directory=declared)
                code, doc, _out = self.world.do_seal(self.world.root / f'overlap-{index}.json')
                self.assertEqual(code, 1, doc)
                self.assertEqual(_failure(doc), 'external_directory')
                self.assertIn(detail, doc['failures'][0]['detail'])

            app = directory / 'App'
            app.mkdir()
            app.chmod(0o700)
            exe = app / 'xr_3da'
            exe.write_bytes(self.world.exe.read_bytes())
            self.world._write_launcher(
                424242, ['xr_3da', '-agent_bridge', str(app / 'b.sock')],
                external_communication_directory=str(app), executable=str(exe))
            code, doc, _out = self.world.do_seal(self.world.root / 'appdir.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'external_directory')
            self.assertIn('application directory', doc['failures'][0]['detail'])

            link = directory / 'link'
            link.symlink_to(directory, target_is_directory=True)
            self.world._write_launcher(
                424242, ['xr_3da', '-agent_bridge', str(link / 'b.sock')],
                external_communication_directory=str(link))
            code, doc, _out = self.world.do_seal(self.world.root / 'symlink-dir.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'external_directory')
            self.assertIn('symlink', doc['failures'][0]['detail'])

            via_tmp = Path('/tmp') / directory.name
            self.assertNotEqual(os.path.realpath(via_tmp), str(via_tmp))
            self.world._write_launcher(
                424242, ['xr_3da', '-agent_bridge', str(via_tmp / 'b.sock')],
                external_communication_directory=str(via_tmp))
            code, doc, _out = self.world.do_seal(self.world.root / 'alias-dir.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'external_directory')
            self.assertIn('not canonical', doc['failures'][0]['detail'])

            os.chmod(directory, 0o755)
            self.world._write_launcher(
                424242, ['xr_3da', '-agent_bridge', str(endpoint)],
                external_communication_directory=str(directory))
            code, doc, _out = self.world.do_seal(self.world.root / 'mode.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'external_directory')
            self.assertIn('0700', doc['failures'][0]['detail'])
            os.chmod(directory, 0o700)
            with mock.patch('tools.frame_time_provenance.os.geteuid', return_value=os.geteuid() + 1):
                code, doc, _out = self.world.do_seal(self.world.root / 'owner.json')
            self.assertEqual(code, 1, doc)
            self.assertEqual(_failure(doc), 'external_directory')
            self.assertIn('current uid', doc['failures'][0]['detail'])
            self.assertFalse(doc['live_runtime_attested'])

    def test_external_directory_keeps_in_tree_sockets_and_detects_replacement(self):
        with tempfile.TemporaryDirectory(prefix='oxv-', dir='/private/tmp') as name:
            directory = Path(name)
            directory.chmod(0o700)
            sock, endpoint = self._bind_external(directory)
            extra = _listen(self.world.runtime, 'appdata/extra.sock')
            try:
                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', str(endpoint)],
                    external_communication_directory=str(directory))
                code, doc, _out = self.world.do_seal(self.world.root / 'extra.json')
                self.assertEqual(code, 1, doc)
                self.assertEqual(_failure(doc), 'nonregular')
                self.assertEqual(doc['failures'][0]['path'], 'appdata/extra.sock')
                self.assertFalse(doc['asset_verified'])
            finally:
                extra.close()
                sock.close()
            os.unlink(self.world.runtime / 'appdata/extra.sock')
            os.unlink(endpoint)

            sock, endpoint = self._bind_external(directory)
            try:
                code, sealed, seal_path = self.world.do_seal()
                self.assertEqual(code, 0, sealed)
                self.assertEqual(sealed['bridge_socket']['association'], 'external-observation')
                text = _csv(sealed['source_build'])
            finally:
                sock.close()
            os.unlink(endpoint)
            replacement = self._bind_external(directory)[0]
            try:
                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / 'external-replaced.json')
                self.assertEqual(status, 1, doc)
                self.assertEqual(_failure(doc), 'launcher_identity')
                self.assertIn('bridge socket', doc['failures'][0]['detail'])
                self.assertFalse(doc['asset_verified'])
                self.assertFalse(doc['measurement_accepted'])
                self.assertFalse(doc['live_runtime_attested'])
            finally:
                replacement.close()

    def test_external_directory_deadline_is_timeout(self):
        with tempfile.TemporaryDirectory(prefix='oxv-', dir='/private/tmp') as name:
            directory = Path(name)
            directory.chmod(0o700)
            sock, endpoint = self._bind_external(directory)
            clock = {'now': 0.0}

            def expire(path):
                if str(path).startswith(str(directory)):
                    clock['now'] = 1000.0

            try:
                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', str(endpoint)],
                    external_communication_directory=str(directory))
                code, doc, _out = self.world.do_seal(
                    timeout_seconds=5, clock=lambda: clock['now'], after_lstat=expire)
                self.assertEqual(code, 3, doc)
                self.assertEqual(doc['result'], 'timeout')
                self.assertEqual(_failure(doc), 'timeout')
                self.assertFalse(doc['asset_verified'])
                self.assertFalse(doc['measurement_accepted'])
                self.assertFalse(doc['live_runtime_attested'])
                with self.assertRaises(GateError) as caught:
                    inspect_runtime(
                        self.world.runtime, self.world.launcher, config='Release',
                        process=self.world.process, deadline=Deadline(30),
                        external_communication_directory=str(directory) + '-other')
                self.assertEqual(caught.exception.code, 'external_directory')
            finally:
                sock.close()

    def _refuse_case_alias(self, path: Path, detail: str, label: str,
                           executable: str | None = None) -> bool:
        alias = _case_alias(path)
        real = os.path.realpath(path)
        if alias is None:
            swapped = Path(real).with_name(Path(real).name.swapcase())
            if swapped.name != Path(real).name and (
                    not swapped.exists() or not os.path.samefile(real, swapped)):
                self.assertFalse(_directories_overlap(real, str(swapped)))
                self.assertFalse(_directories_overlap(str(swapped), real))
            return False
        self.assertTrue(_directories_overlap(str(alias), real))
        self.assertTrue(_directories_overlap(real, str(alias)))
        self.world._write_launcher(
            424242, ['xr_3da', '-agent_bridge', str(alias / 'b.sock')],
            external_communication_directory=str(alias), executable=executable)
        code, doc, _out = self.world.do_seal(self.world.root / f'case-{label}.json')
        self.assertEqual(code, 1, doc)
        self.assertEqual(_failure(doc), 'external_directory')
        self.assertIn(detail, doc['failures'][0]['detail'])
        self.assertFalse(doc['asset_verified'])
        self.assertFalse(doc['measurement_accepted'])
        self.assertFalse(doc['live_runtime_attested'])
        return True

    def test_external_directory_case_alias_uses_inode_identity(self):
        with tempfile.TemporaryDirectory(prefix='oxv-ident-', dir='/private/tmp') as name:
            parent = Path(name)
            upper = parent / 'AbC'
            upper.mkdir()
            child = upper / 'Child'
            child.mkdir()
            sibling = parent / 'Zz'
            sibling.mkdir()
            self.assertTrue(_directories_overlap(str(upper), str(child)))
            self.assertTrue(_directories_overlap(str(child), str(upper)))
            self.assertFalse(_directories_overlap(str(child), str(sibling)))
            self.assertFalse(_directories_overlap(
                str(parent / 'NoSuch-AbC'), str(parent / 'nosuch-abc')))
            other = parent / 'abc'
            try:
                other.mkdir()
            except FileExistsError:
                distinct = False
            else:
                distinct = not os.path.samefile(upper, other)
            if distinct:
                self.assertFalse(_directories_overlap(str(upper), str(other)))
                self.assertFalse(_directories_overlap(str(other), str(child)))
                self.assertFalse(_directories_overlap(str(child), str(other)))
            else:
                folded = parent / 'abc'
                self.assertTrue(os.path.samefile(upper, folded))
                self.assertEqual(os.path.realpath(folded), str(folded))
                self.assertTrue(_directories_overlap(str(folded), str(upper)))
                self.assertTrue(_directories_overlap(str(upper / 'child'), str(folded)))
                self.assertTrue(_directories_overlap(str(folded / 'child'), str(upper)))
                self.assertFalse(_directories_overlap(str(folded / 'child'), str(sibling)))

            app = parent / 'App'
            app.mkdir()
            app.chmod(0o700)
            exe = app / 'xr_3da'
            exe.write_bytes(self.world.exe.read_bytes())
            if not self._refuse_case_alias(app, 'application directory', 'app', executable=str(exe)):
                spelled = Path(os.path.realpath(app)).with_name('aPP')
                self.assertFalse(spelled.exists())
                spelled.mkdir()
                spelled.chmod(0o700)
                sock, _endpoint = self._bind_external(spelled)
                try:
                    self.world._write_launcher(
                        424242, ['xr_3da', '-agent_bridge', str(spelled / 'b.sock')],
                        external_communication_directory=str(spelled), executable=str(exe))
                    code, doc, _out = self.world.do_seal(self.world.root / 'case-distinct.json')
                    self.assertEqual(code, 0, doc)
                    self.assertEqual(doc['bridge_socket']['association'], 'external-observation')
                    self.assertTrue(doc['asset_verified'])
                    self.assertFalse(doc['live_runtime_attested'])
                    self.assertFalse(_directories_overlap(str(app), str(spelled)))
                finally:
                    sock.close()

        self._refuse_case_alias(self.world.base, 'manifest root', 'base')
        self._refuse_case_alias(self.world.runtime / 'database', 'generated alias', 'database')
        self._refuse_case_alias(self.world.runtime / 'appdata', 'settings closure', 'appdata')
        notes = self.world.runtime / 'notes'
        notes.mkdir()
        self._refuse_case_alias(notes, 'runtime', 'notes')
        self._refuse_case_alias(self.world.root, 'settings closure', 'root')

    def test_external_directory_rechecks_identity_after_socket_observation(self):
        with tempfile.TemporaryDirectory(prefix='oxv-race-', dir='/private/tmp') as name:
            parent = Path(name)
            directory = parent / 'comm'
            directory.mkdir()
            directory.chmod(0o700)
            sock, endpoint = self._bind_external(directory)
            moved = directory.with_name('comm-moved')
            seal_only = parent / 'sealcomm'
            seal_only.mkdir()
            seal_only.chmod(0o700)
            seal_sock, seal_endpoint = self._bind_external(seal_only)
            seal_moved = seal_only.with_name('sealcomm-moved')

            def restore_mode(folder: Path) -> None:
                if folder.is_dir() and not folder.is_symlink():
                    folder.chmod(0o700)

            def chmod_during(path, folder=directory, leaf=endpoint):
                if str(path) == str(leaf):
                    folder.chmod(0o755)

            replaced = {'done': False}

            def replace_during(path, folder=directory, leaf=endpoint, target=moved, flag=replaced):
                if str(path) == str(leaf) and not flag['done']:
                    flag['done'] = True
                    folder.rename(target)
                    folder.symlink_to(target, target_is_directory=True)

            try:
                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', str(endpoint)],
                    external_communication_directory=str(directory))
                code, sealed, seal_path = self.world.do_seal(self.world.root / 'race-seal.json')
                self.assertEqual(code, 0, sealed)
                self.assertEqual(sealed['bridge_socket']['association'], 'external-observation')
                self.assertFalse(sealed['live_runtime_attested'])
                text = _csv(sealed['source_build'])

                def assert_race(status, doc):
                    self.assertEqual(status, 2, doc)
                    self.assertEqual(doc['result'], 'unknown')
                    self.assertEqual(_failure(doc), 'stat_race')
                    self.assertIn('changed during inspection', doc['failures'][0]['detail'])
                    self.assertFalse(doc['asset_verified'])
                    self.assertFalse(doc['measurement_accepted'])
                    self.assertFalse(doc['live_runtime_attested'])

                code, doc, _out = self.world.do_seal(
                    self.world.root / 'race-seal-mode.json', after_lstat=chmod_during)
                assert_race(code, doc)
                self.assertEqual(oct(directory.stat().st_mode & 0o777), '0o755')
                restore_mode(directory)
                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / 'race-attest-mode.json', after_lstat=chmod_during)
                assert_race(status, doc)
                self.assertEqual(oct(directory.stat().st_mode & 0o777), '0o755')
                restore_mode(directory)

                clock = {'now': 0.0}

                def expire_socket(path, leaf=endpoint):
                    if str(path) == str(leaf):
                        clock['now'] = 1000.0

                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / 'race-attest-deadline.json',
                    timeout_seconds=5, clock=lambda: clock['now'], after_lstat=expire_socket)
                self.assertEqual(status, 3, doc)
                self.assertEqual(doc['result'], 'timeout')
                self.assertEqual(_failure(doc), 'timeout')
                self.assertFalse(doc['asset_verified'])
                self.assertFalse(doc['measurement_accepted'])
                self.assertFalse(doc['live_runtime_attested'])

                status, doc, _capture, _before = self.world.do_attest(
                    seal_path, text, self.world.root / 'race-attest-link.json', after_lstat=replace_during)
                assert_race(status, doc)
                self.assertTrue(directory.is_symlink())

                seal_replaced = {'done': False}

                def replace_seal(path, folder=seal_only, leaf=seal_endpoint, target=seal_moved,
                                 flag=seal_replaced):
                    if str(path) == str(leaf) and not flag['done']:
                        flag['done'] = True
                        folder.rename(target)
                        folder.symlink_to(target, target_is_directory=True)

                self.world._write_launcher(
                    424242, ['xr_3da', '-agent_bridge', str(seal_endpoint)],
                    external_communication_directory=str(seal_only))
                code, doc, _out = self.world.do_seal(
                    self.world.root / 'race-seal-link.json', after_lstat=replace_seal)
                assert_race(code, doc)
                self.assertTrue(seal_only.is_symlink())
            finally:
                sock.close()
                seal_sock.close()
                for link, target in ((directory, moved), (seal_only, seal_moved)):
                    if link.is_symlink():
                        link.unlink()
                    leaf = target / 'b.sock'
                    if leaf.exists():
                        with contextlib.suppress(OSError):
                            os.unlink(leaf)
                    if target.is_dir() and not target.is_symlink():
                        with contextlib.suppress(OSError):
                            target.rmdir()


class ParserHelperTests(unittest.TestCase):
    def test_macho_and_vmmap_helpers(self):
        self.assertIsNone(parse_macho(b'not-macho'))
        header_only = macho(2)
        parsed = parse_macho(header_only)
        self.assertEqual(parsed['loads'], [])
        self.assertIsNone(parsed['identity'])
        binary = macho(6, identity='libfix.dylib', loads=('libother.dylib',))
        parsed = parse_macho(binary)
        self.assertEqual(parsed['identity']['current_version'], '1.0.0')
        self.assertEqual(parsed['loads'], ['libother.dylib'])
        self.assertTrue(is_os_image(OS_IMAGE))
        self.assertEqual(aggregate([]), hashlib.sha256(b'').hexdigest())
        with tempfile.TemporaryDirectory() as name:
            exe = Path(name) / 'xr_3da'
            dylib = Path(name) / 'libfix.dylib'
            exe.write_bytes(b'exe')
            dylib.write_bytes(b'dylib')
            exe_real = os.path.realpath(exe)
            dylib_real = os.path.realpath(dylib)
            os_real = os.path.realpath(OS_IMAGE)
            text = (
                f'Path: {exe_real}\n'
                f'__TEXT 1000-2000 [4K] r-x/r-x SM=COW {exe_real}\n'
                f'__DATA 3000-4000 [4K] rw-/rw- SM=PRV {dylib_real}\n'
                f'__TEXT 5000-6000 [4K] r-x/r-x SM=COW {os_real}\n'
            )
            self.assertEqual(images_from_vmmap_text(text, exe_real), [exe_real, dylib_real, os_real])
            self.assertIsNone(images_from_vmmap_text(f'Path: {exe_real}\n', exe_real))
            self.assertIsNone(images_from_vmmap_text('Path: /tmp/a-b/xr_3da\n', '/tmp/a-b/xr_3da'))
            self.assertIsNone(images_from_vmmap_text(f'{exe_real}\n{OS_IMAGE}\n', exe_real))
            self.assertIsNone(images_from_vmmap_text('no paths here\n', exe_real))
            spaced = '/tmp/extra images/libfix.dylib'
            wide = (
                'Path:            /tmp/a-b/xr_3da\n'
                'REGION TYPE                    START - END         [ VSIZE  RSDNT  DIRTY   SWAP] '
                'PRT/MAX SHRMOD PURGE    REGION DETAIL\n'
                '__TEXT                      100148000-1001d0000    [  544K   544K     0K     0K] '
                'r-x/r-x SM=COW          /tmp/a-b/xr_3da\n'
                'Activity Tracing            10020c000-10024c000    [  256K    16K    16K     0K] '
                'r--/r-x SM=ALI PURGE=N  \n'
                'STACK GUARD                 16bcb8000-16f4bc000    [ 56.0M     0K     0K     0K] '
                '---/rwx SM=NUL          stack guard for thread 0\n'
                '__TEXT                      1948cc000-194920000    [  336K   336K     0K     0K] '
                f'r-x/r-x SM=COW          {spaced}\n'
            )
            self.assertEqual(
                images_from_vmmap_text(wide, '/tmp/a-b/xr_3da'),
                ['/tmp/a-b/xr_3da', spaced])
            self.assertIsNone(images_from_vmmap_text(
                f'__TEXT 1000-2000 {spaced}\n'
                '__TEXT 3000-4000 [4K] r-x/r-x SM=COW /tmp/a-b/xr_3da\n',
                '/tmp/a-b/xr_3da'))

    def test_design_fence_matches_generator_and_records_the_adapter_gap(self):
        text = DESIGN.read_text()
        block = text.split('```text\n', 1)[1].split('```', 1)[0]
        self.assertEqual(block, filesystem_text())
        self.assertIn('Probe adapter, not implemented', text)
        self.assertIn('e3f273f04bf85b4b265004cbbc8a35f19796cd31', text)
        self.assertIn('source-identity.json', text)
        self.assertNotIn('build/runtime/coc', text)

    def test_libproc_matches_a_socket_bound_by_this_process(self):
        pid = os.getpid()
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            runtime = root / 'runtime'
            (runtime / 'appdata').mkdir(parents=True)
            relative = 'appdata/prov.sock'
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            previous = os.getcwd()
            os.chdir(runtime)
            try:
                sock.bind(relative)
                sock.listen(1)
            finally:
                os.chdir(previous)
            try:
                lexical = str(os.path.realpath(runtime / 'appdata' / 'prov.sock'))
                self.assertTrue(_process_owns_bridge_socket(pid, [relative]))
                self.assertFalse(_process_owns_bridge_socket(pid, [lexical]))
                self.assertFalse(_process_owns_bridge_socket(pid, ['appdata/other.sock']))
            finally:
                sock.close()
            absolute = root / 'abs.sock'
            other = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            other.bind(str(absolute))
            other.listen(1)
            try:
                self.assertTrue(_process_owns_bridge_socket(pid, [str(absolute)]))
                self.assertFalse(_process_owns_bridge_socket(pid, ['abs.sock']))
                self.assertFalse(_process_owns_bridge_socket(pid, [relative]))
            finally:
                other.close()

    def test_socket_fdinfo_rejects_malformed_darwin_buffers(self):
        pid = os.getpid()
        with tempfile.TemporaryDirectory() as name:
            runtime = Path(name) / 'runtime'
            (runtime / 'appdata').mkdir(parents=True)
            rel = 'appdata/b.sock'
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            previous = os.getcwd()
            os.chdir(runtime)
            try:
                sock.bind(rel)
                sock.listen(1)
            finally:
                os.chdir(previous)
            try:
                library = provenance._libproc()
                self.assertIsNotNone(library)
                raw = ctypes.create_string_buffer(4096)
                got = library.proc_pidfdinfo(pid, sock.fileno(), 3, raw, 4096)
                self.assertEqual(got, 792)
                blob = raw.raw[:got]
                self.assertEqual(int.from_bytes(blob[184:188], 'little'), 1)
                self.assertEqual(int.from_bytes(blob[256:260], 'little'), 3)
                self.assertEqual(blob[281], 1)
                self.assertEqual(blob[282:].split(b'\0', 1)[0], rel.encode())
                inode = os.lstat(runtime / 'appdata' / 'b.sock').st_ino
                self.assertEqual(blob.find(inode.to_bytes(8, 'little')), -1)
                listing = sock.fileno().to_bytes(4, 'little', signed=True) + (2).to_bytes(4, 'little')

                def accepted(data: bytes, paths: list[str]) -> bool:
                    class Stub:
                        def proc_pidfdinfo(self, seen, fd, flavor, out, size):
                            del seen, fd, flavor, size
                            ctypes.memmove(out, data, len(data))
                            return len(data)

                    with mock.patch.object(provenance, '_libproc', return_value=Stub()), \
                            mock.patch.object(provenance, '_pid_fd_bytes', return_value=listing):
                        return provenance._process_owns_bridge_socket(pid, paths)

                self.assertTrue(accepted(blob, [rel]))
                wrong = bytearray(blob)
                wrong[184:188] = (2).to_bytes(4, 'little')
                wrong[256:260] = (2).to_bytes(4, 'little')
                wrong[281] = 2
                self.assertFalse(accepted(bytes(wrong), [rel]))
                self.assertFalse(accepted(blob[:282 + len(rel) + 1], [rel]))
                longpath = b'appdata/' + b'a' * 110
                wide = bytearray(blob)
                wide[282:282 + len(longpath) + 1] = longpath + b'\0'
                self.assertFalse(accepted(bytes(wide), [longpath.decode()]))
            finally:
                sock.close()

    def test_nbsp_stays_inside_the_agent_bridge_token(self):
        path = 'appdata/a\u00a0b.sock'
        self.assertEqual(_agent_bridge_token(['exe', '-agent_bridge', path]), path)
        self.assertNotEqual(_agent_bridge_token(['exe', '-agent_bridge', path]), 'appdata/a')
        for space in (' ', '\t', '\n', '\v', '\f', '\r'):
            self.assertEqual(
                _agent_bridge_token(['exe', '-agent_bridge' + space + 'appdata/x.sock']),
                'appdata/x.sock')
        self.assertEqual(_agent_bridge_token(['exe', '-agent_bridge']), '')
        self.assertIsNone(_agent_bridge_token(['-agent_bridge', path]))


def _unused_pid() -> int:
    for pid in range(1_000_000, 1_001_000):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return pid
        except PermissionError:
            continue
    raise AssertionError('no unused pid in the scanned range')


if __name__ == '__main__':
    unittest.main()
