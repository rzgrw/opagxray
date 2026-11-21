"""Private freeplay test runtime: clone a play runtime with one save, install the CoC adapter."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / 'tools' / 'freeplay_runtime.py'
ADAPTER = REPO / 'res' / 'coc' / 'gamedata' / 'scripts' / 'xrsim_freeplay.script'


class FreeplayRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'play'
        self.dest = self.root / 'freeplay-ai'
        self.put(self.source / 'database/x.db', 'archive bytes')
        self.put(self.source / 'fsgame.ltx', 'fs aliases')
        self.put(self.source / 'gamedata/configs/a.ltx', 'config a')
        self.put(self.source / 'appdata/user.ltx', 'renderer renderer_r3')
        self.put(self.source / 'appdata/savedgames/cordon_start.scop', 'scop')
        self.put(self.source / 'appdata/savedgames/cordon_start.scoc', 'scoc')
        self.put(self.source / 'appdata/savedgames/1.scop', 'other scop')
        self.put(self.source / 'appdata/savedgames/1.scoc', 'other scoc')
        self.put(self.source / 'appdata/logs/old.log', 'old log')

    def put(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def run_tool(self, *args, success=True):
        result = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], text=True, capture_output=True)
        if success:
            self.assertEqual(0, result.returncode, result.stderr)
        else:
            self.assertNotEqual(0, result.returncode, result.stdout)
            self.assertNotIn('Traceback', result.stderr)
            self.assertIn('freeplay_runtime: ', result.stderr)   # refused by the tool, not a crash
        return result

    def clone(self, dest=None, save='cordon_start', success=True):
        return self.run_tool('clone', '--from', self.source, '--to', dest or self.dest, '--save', save,
                             success=success)

    def snapshot(self, root):
        return sorted((str(p.relative_to(root)), p.read_bytes() if p.is_file() else None) for p in root.rglob('*'))

    def test_clone_copies_assets_one_save_and_adapter(self):
        self.clone()
        self.assertEqual('archive bytes', (self.dest / 'database/x.db').read_text())
        self.assertEqual('fs aliases', (self.dest / 'fsgame.ltx').read_text())
        self.assertEqual('config a', (self.dest / 'gamedata/configs/a.ltx').read_text())
        self.assertEqual(['renderer renderer_r3', 'snd_volume_eff 0.', 'snd_volume_music 0.'],
                         (self.dest / 'appdata/user.ltx').read_text().splitlines())
        saves = sorted(p.name for p in (self.dest / 'appdata/savedgames').iterdir())
        self.assertEqual(['cordon_start.scoc', 'cordon_start.scop'], saves)
        self.assertEqual(['savedgames', 'user.ltx'], sorted(p.name for p in (self.dest / 'appdata').iterdir()))
        self.assertEqual(ADAPTER.read_bytes(), (self.dest / 'gamedata/scripts/xrsim_freeplay.script').read_bytes())
        self.assertFalse((self.source / 'gamedata/scripts').exists())

    def test_clone_silences_nonzero_volumes(self):
        self.put(self.source / 'appdata/user.ltx',
                 'renderer renderer_r3\nsnd_volume_eff 0.8\nvid_mode 1024x576\nsnd_volume_music 0.5\n')
        self.clone()
        self.assertEqual(['renderer renderer_r3', 'snd_volume_eff 0.', 'vid_mode 1024x576', 'snd_volume_music 0.'],
                         (self.dest / 'appdata/user.ltx').read_text().splitlines())
        self.assertIn('snd_volume_eff 0.8', (self.source / 'appdata/user.ltx').read_text())

    def test_clone_adds_volumes_when_source_has_none(self):
        self.put(self.source / 'appdata/user.ltx', 'renderer renderer_r3\nvid_mode 1024x576')
        self.clone()
        self.assertEqual(['renderer renderer_r3', 'vid_mode 1024x576', 'snd_volume_eff 0.', 'snd_volume_music 0.'],
                         (self.dest / 'appdata/user.ltx').read_text().splitlines())

    def test_clone_refuses_nonempty_destination(self):
        self.put(self.dest / 'keep.txt', 'mine')
        before = self.snapshot(self.dest)
        self.clone(success=False)
        self.assertEqual(before, self.snapshot(self.dest))

    def test_clone_refuses_destination_inside_source(self):
        before = self.snapshot(self.source)
        for dest in (self.source, self.source / 'nested', self.source / 'gamedata/clone'):
            with self.subTest(dest=dest):
                self.clone(dest, success=False)
        self.assertEqual(before, self.snapshot(self.source))

    def test_clone_missing_save_fails_before_writing(self):
        self.clone(save='no_such_save', success=False)
        self.assertFalse(self.dest.exists())
        (self.source / 'appdata/savedgames/cordon_start.scoc').unlink()
        self.clone(success=False)
        self.assertFalse(self.dest.exists())
        self.put(self.source / 'appdata/savedgames/cordon_start.scoc', 'scoc')
        for missing in ('database', 'gamedata'):
            with self.subTest(missing=missing):
                moved = self.root / ('moved-' + missing)
                (self.source / missing).rename(moved)
                self.clone(success=False)
                self.assertFalse(self.dest.exists())
                moved.rename(self.source / missing)

    def test_install_refuses_different_existing_adapter(self):
        target = self.source / 'gamedata/scripts/xrsim_freeplay.script'
        self.put(target, '-- someone else')
        self.run_tool('install', '--runtime', self.source, success=False)
        self.assertEqual('-- someone else', target.read_text())
        self.run_tool('install', '--runtime', self.source, '--force')
        self.assertEqual(ADAPTER.read_bytes(), target.read_bytes())

    def test_install_is_idempotent(self):
        target = self.source / 'gamedata/scripts/xrsim_freeplay.script'
        self.run_tool('install', '--runtime', self.source)
        self.assertEqual(ADAPTER.read_bytes(), target.read_bytes())
        self.run_tool('install', '--runtime', self.source)
        self.assertEqual(ADAPTER.read_bytes(), target.read_bytes())
        self.assertEqual(['xrsim_freeplay.script'], [p.name for p in target.parent.iterdir()])


if __name__ == '__main__':
    unittest.main()
