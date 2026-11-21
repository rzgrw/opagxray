"""Private freeplay test runtime: clone a play runtime with one save, install the CoC adapter."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from tools import freeplay_runtime


REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / 'tools' / 'freeplay_runtime.py'
ADAPTER = REPO / 'res' / 'coc' / 'gamedata' / 'scripts' / 'xrsim_freeplay.script'
STORY_PATH = 'gamedata/configs/misc/sound/script_sound_story.ltx'

# Made-up theme file in the shape of the CoC data: CRLF, cp1251 comment bytes, one story theme
# without shuffle, one with it, and two that the rule must leave alone.
STORY_LTX = (b'; \xd0\xe0\xe7\xe3\xee\xe2\xee\xf0\xfb\r\n'
             b'[story_a_0]\r\ntype = npc\r\npath = a\\b\r\n\r\n'
             b'[story_a_1]\r\ntype = npc\r\nshuffle = seq\r\npath = a\\c\r\n\r\n'
             b'[chat_npc]\r\ntype = npc\r\npath = d\r\n\r\n'
             b'[story_noise]\r\ntype = 3d\r\npath = e\r\n')
STORY_LTX_FIXED = STORY_LTX.replace(b'[story_a_0]\r\n', b'[story_a_0]\r\nshuffle = rnd\r\n')


def pack_db(files):
    """A minimal uncompressed .db archive (stored bodies, plain file table) readable by xrdb_unpack."""
    bodies = b''
    table = b''
    offset = 8
    for name, data in files.items():
        encoded = name.encode('cp1251')
        table += struct.pack('<HIII', len(encoded) + 16, len(data), len(data), zlib.crc32(data) & 0xffffffff)
        table += encoded + struct.pack('<I', offset + len(bodies))
        bodies += data
    return struct.pack('<II', 0, len(bodies)) + bodies + struct.pack('<II', 1, len(table)) + table


class StoryThemePatchTests(unittest.TestCase):
    patch = staticmethod(lambda data: freeplay_runtime.patch_story_themes(data, prefix='story_'))

    def test_adds_shuffle_only_to_story_npc_themes_without_it(self):
        fixed, sections = self.patch(STORY_LTX)
        self.assertEqual(['story_a_0'], sections)
        self.assertEqual(STORY_LTX_FIXED, fixed)

    def test_keeps_lf_line_endings(self):
        lf = STORY_LTX.replace(b'\r\n', b'\n')
        fixed, _ = self.patch(lf)
        self.assertEqual(STORY_LTX_FIXED.replace(b'\r\n', b'\n'), fixed)

    def test_inherited_shuffle_and_type_are_resolved(self):
        data = (b'[base]\r\ntype = npc\r\nshuffle = rnd\r\n'
                b'[story_b]:base\r\npath = x\r\n'
                b'[typed]\r\ntype = npc\r\n'
                b'[story_c]:typed\r\npath = y')
        fixed, sections = self.patch(data)
        self.assertEqual(['story_c'], sections)
        self.assertEqual(data.replace(b'[story_c]:typed\r\n', b'[story_c]:typed\r\nshuffle = rnd\r\n'), fixed)

    def test_idempotent(self):
        fixed, _ = self.patch(STORY_LTX)
        again, sections = self.patch(fixed)
        self.assertEqual([], sections)
        self.assertEqual(fixed, again)


class FreeplayRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'play'
        self.dest = self.root / 'freeplay-ai'
        self.put(self.source / 'database/x.db', 'archive bytes')
        self.write_configs_db(self.source)
        self.put(self.source / 'fsgame.ltx', 'fs aliases')
        self.put(self.source / 'gamedata/configs/a.ltx', 'config a')
        self.put(self.source / 'appdata/user.ltx', 'renderer renderer_r3')
        self.put(self.source / 'appdata/savedgames/cordon_start.scop', 'scop')
        self.put(self.source / 'appdata/savedgames/cordon_start.scoc', 'scoc')
        self.put(self.source / 'appdata/savedgames/1.scop', 'other scop')
        self.put(self.source / 'appdata/savedgames/1.scoc', 'other scoc')
        self.put(self.source / 'appdata/logs/old.log', 'old log')

    def write_configs_db(self, runtime):
        path = runtime / 'database/config/configs.db'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(pack_db({
            'configs\\misc\\sound\\script_sound.ltx': b'#include "script_sound_story.ltx"\r\n',
            'configs\\misc\\sound\\script_sound_story.ltx': STORY_LTX.replace(b'story_', b'strange_story_'),
            'configs\\misc\\other.ltx': b'[strange_story_x]\r\ntype = npc\r\n',
        }))

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
        self.assertEqual(STORY_LTX_FIXED.replace(b'story_', b'strange_story_'), (self.dest / STORY_PATH).read_bytes())
        self.assertFalse((self.dest / 'gamedata/configs/misc/sound/script_sound.ltx').exists())
        self.assertFalse((self.dest / 'gamedata/configs/misc/other.ltx').exists())
        self.assertFalse((self.source / 'gamedata/configs/misc').exists())

    def test_clone_refuses_differing_story_override_in_source(self):
        self.put(self.source / STORY_PATH, 'someone else')
        self.clone(success=False)
        self.assertFalse(self.dest.exists())

    def test_install_without_flag_leaves_story_themes_alone(self):
        before = self.snapshot(self.source / 'gamedata/configs')
        self.run_tool('install', '--runtime', self.source)
        self.assertEqual(before, self.snapshot(self.source / 'gamedata/configs'))

    def test_install_fix_story_themes_is_idempotent(self):
        expected = STORY_LTX_FIXED.replace(b'story_', b'strange_story_')
        self.run_tool('install', '--runtime', self.source, '--fix-story-themes')
        self.assertEqual(expected, (self.source / STORY_PATH).read_bytes())
        result = self.run_tool('install', '--runtime', self.source, '--fix-story-themes')
        self.assertIn('already', result.stdout)
        self.assertEqual(expected, (self.source / STORY_PATH).read_bytes())
        self.assertEqual(['script_sound_story.ltx'], [p.name for p in (self.source / STORY_PATH).parent.iterdir()])

    def test_install_fix_story_themes_refuses_differing_file_without_force(self):
        self.put(self.source / STORY_PATH, 'someone else')
        self.run_tool('install', '--runtime', self.source, '--fix-story-themes', success=False)
        self.assertEqual('someone else', (self.source / STORY_PATH).read_text())
        self.assertFalse((self.source / 'gamedata/scripts').exists())
        self.run_tool('install', '--runtime', self.source, '--fix-story-themes', '--force')
        self.assertEqual(STORY_LTX_FIXED.replace(b'story_', b'strange_story_'), (self.source / STORY_PATH).read_bytes())

    def test_install_fix_story_themes_needs_configs_db(self):
        (self.source / 'database/config/configs.db').unlink()
        self.run_tool('install', '--runtime', self.source, '--fix-story-themes', success=False)
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
