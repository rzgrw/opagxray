"""Private runtime behavior; fixtures contain tiny stand-ins for archive bytes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'stage_coc_runtime.py'


class StageRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.base = self.root / 'base'
        self.patch = self.root / 'patch'
        self.output = self.root / 'runtime'
        for name in ('config', 'maps', 'resource', 'sound'):
            self.put(self.base / 'database' / name / 'base.db', name)
        for name in ('scripts', 'configs'):
            self.put(self.base / f'database/config/{name}.db', 'base-' + name)
            self.put(self.patch / f'database/config/{name}.db', 'patch-' + name)
        self.put(self.patch / 'database/patch/xpatch_textures.db', 'extra')
        self.put(self.patch / 'gamedata/scripts/untrusted.script', 'never stage me')

    def put(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def stage(self, output=None, success=True):
        result = subprocess.run([sys.executable, str(SCRIPT), '--base', str(self.base),
                                 '--patch', str(self.patch), '--output', str(output or self.output)],
                                text=True, capture_output=True)
        if success:
            self.assertEqual(0, result.returncode, result.stderr)
        else:
            self.assertNotEqual(0, result.returncode)
        return result

    def test_archive_patch_precedence_provenance_and_private_gl_overlay(self):
        self.stage()
        archive = self.output / 'database/config/scripts.db'
        self.assertEqual('patch-scripts', archive.read_text())
        self.assertFalse(archive.is_symlink())
        self.assertEqual('extra', (self.output / 'database/patch/xpatch_textures.db').read_text())
        self.assertFalse((self.output / 'gamedata/scripts/untrusted.script').exists())
        self.assertTrue(any((self.output / 'gamedata/shaders/gl').rglob('*.vs')))
        self.assertTrue((self.output / 'gamedata/configs').is_dir())
        self.assertIn('renderer renderer_r3', (self.output / 'appdata/user.ltx').read_text())
        manifest = json.loads((self.output / 'stage-manifest.json').read_text())
        record = manifest['files']['database/config/scripts.db']
        self.assertEqual(str(self.patch / 'database/config/scripts.db'), record['source'])
        self.assertEqual(hashlib.sha256(b'patch-scripts').hexdigest(), record['sha256'])
        aliases = {key.strip(): value.split('|') for line in (self.output / 'fsgame.ltx').read_text().splitlines()
                   if line.startswith('$') for key, value in [line.split('=', 1)]}
        self.assertEqual('database\\', aliases['$arch_dir$'][3].strip())
        self.assertEqual('appdata\\', aliases['$app_data_root$'][3].strip())

    def test_split_db_archives_are_staged(self):
        (self.base / 'database/resource/base.db').unlink()
        self.put(self.base / 'database/resource/textures.db0', 'part zero')
        self.put(self.base / 'database/resource/textures.db1', 'part one')
        self.stage()
        self.assertEqual('part one', (self.output / 'database/resource/textures.db1').read_text())

    def test_missing_base_or_patch_is_rejected_before_output_writes(self):
        (self.base / 'database/maps/base.db').unlink()
        self.stage(success=False)
        self.assertFalse(self.output.exists())
        self.put(self.base / 'database/maps/base.db', 'maps')
        (self.patch / 'database/config/scripts.db').unlink()
        self.stage(success=False)
        self.assertFalse(self.output.exists())

    def test_repeat_preserves_user_configs_saves_and_refreshes_managed_files(self):
        self.stage()
        self.put(self.output / 'appdata/user.ltx', 'my settings')
        self.put(self.output / 'appdata/savedgames/mine.scoc', 'my save')
        self.put(self.output / 'gamedata/configs/axr_options.ltx', 'my options')
        self.put(self.patch / 'database/config/scripts.db', 'updated patch')
        self.stage()
        self.assertEqual('updated patch', (self.output / 'database/config/scripts.db').read_text())
        self.assertEqual('my settings', (self.output / 'appdata/user.ltx').read_text())
        self.assertEqual('my save', (self.output / 'appdata/savedgames/mine.scoc').read_text())
        self.assertEqual('my options', (self.output / 'gamedata/configs/axr_options.ltx').read_text())
        before = (self.output / 'stage-manifest.json').read_bytes()
        self.stage()
        self.assertEqual(before, (self.output / 'stage-manifest.json').read_bytes())

    def test_restage_resumes_asset_written_before_manifest_commit(self):
        self.stage()
        self.put(self.patch / 'database/config/scripts.db', 'updated patch')
        self.put(self.output / 'database/config/scripts.db', 'updated patch')
        self.stage()
        manifest = json.loads((self.output / 'stage-manifest.json').read_text())
        self.assertEqual(hashlib.sha256(b'updated patch').hexdigest(),
                         manifest['files']['database/config/scripts.db']['sha256'])

    def test_unowned_matching_asset_is_not_adopted(self):
        self.stage()
        path = self.output / 'stage-manifest.json'
        manifest = json.loads(path.read_text())
        del manifest['files']['database/config/scripts.db']
        path.write_text(json.dumps(manifest))
        self.stage(success=False)
        self.assertEqual(manifest, json.loads(path.read_text()))

    def test_malformed_manifest_types_report_error_without_traceback_or_writes(self):
        self.stage()
        path = self.output / 'stage-manifest.json'
        valid = json.loads(path.read_text())
        for invalid in ([], {**valid, 'files': []},
                        {**valid, 'files': {'database/config/scripts.db': None}},
                        {**valid, 'files': {'database/config/scripts.db': {'sha256': 12}}}):
            with self.subTest(invalid=invalid):
                path.write_text(json.dumps(invalid))
                result = self.stage(success=False)
                self.assertNotIn('Traceback', result.stderr)
                self.assertIn('manifest', result.stderr.lower())
                self.assertEqual(invalid, json.loads(path.read_text()))

    def test_output_source_overlap_both_directions_is_rejected(self):
        for output in (self.base, self.base / 'nested', self.patch, self.root):
            with self.subTest(output=output):
                self.stage(output, success=False)
        self.assertFalse((self.base / 'nested').exists())
        self.assertFalse((self.root / 'stage-manifest.json').exists())

    def test_unowned_nonempty_output_and_modified_managed_file_are_not_overwritten(self):
        self.put(self.output / 'user-file', 'keep')
        self.stage(success=False)
        self.assertEqual(['user-file'], [p.name for p in self.output.iterdir()])
        (self.output / 'user-file').unlink()
        self.stage()
        self.put(self.output / 'database/config/scripts.db', 'local edit')
        before = (self.output / 'stage-manifest.json').read_bytes()
        self.stage(success=False)
        self.assertEqual('local edit', (self.output / 'database/config/scripts.db').read_text())
        self.assertEqual(before, (self.output / 'stage-manifest.json').read_bytes())

    def test_symlink_destination_or_parent_rejected_before_any_writes(self):
        external = self.root / 'external'
        external.mkdir()
        self.output.symlink_to(external, target_is_directory=True)
        self.stage(success=False)
        self.assertEqual([], list(external.iterdir()))
        self.stage(self.output / 'child', success=False)
        self.assertEqual([], list(external.iterdir()))

    def test_hardlinked_mutable_file_is_rejected_without_source_write(self):
        self.stage()
        external = self.root / 'external-user.ltx'
        external.write_text('donor user settings')
        settings = self.output / 'appdata/user.ltx'
        settings.unlink()
        os.link(external, settings)
        self.stage(success=False)
        self.assertEqual('donor user settings', external.read_text())

    def test_managed_destination_parent_collision_does_not_partially_stage(self):
        self.stage()
        self.put(self.patch / 'database/config/scripts.db', 'new scripts')
        (self.output / 'appdata').rename(self.output / 'private-appdata')
        (self.output / 'appdata').write_text('obstruction')
        self.stage(success=False)
        self.assertEqual('patch-scripts', (self.output / 'database/config/scripts.db').read_text())

    def test_nested_symlink_escape_rejected_even_on_restage(self):
        self.stage()
        external = self.root / 'external'
        external.mkdir()
        before = (self.output / 'stage-manifest.json').read_bytes()
        (self.output / 'gamedata/configs/options.ltx').symlink_to(external / 'options.ltx')
        self.put(self.patch / 'database/config/scripts.db', 'new patch')
        self.stage(success=False)
        self.assertEqual([], list(external.iterdir()))
        self.assertEqual(before, (self.output / 'stage-manifest.json').read_bytes())
        self.assertEqual('patch-scripts', (self.output / 'database/config/scripts.db').read_text())


if __name__ == '__main__':
    unittest.main()
