"""One-command launcher: preflight messages, install, composed command and environment. No CLI call, no game start."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import freeplay_play, freeplay_runtime
from tools.tests.test_freeplay_runtime import STORY_LTX, pack_db

STORY_REL = Path('gamedata/configs/misc/sound/script_sound_story.ltx')


class FakeRun:
    """Stands in for subprocess.run of `claude auth status --json`."""

    def __init__(self, stdout='', raises=None):
        self.stdout, self.raises, self.calls = stdout, raises, []

    def __call__(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        if self.raises:
            raise self.raises
        return subprocess.CompletedProcess(argv, 0, stdout=self.stdout, stderr='')


SIGNED_IN = json.dumps({'loggedIn': True, 'authMethod': 'claude.ai', 'email': 'player@example.com',
                        'orgName': 'Example Org'})
SIGNED_OUT = json.dumps({'loggedIn': False})


class LauncherTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.repo = self.root / 'repo'
        self.binary = self.repo / 'bin/arm64/Release/xr_3da'
        self.binary.parent.mkdir(parents=True)
        self.binary.write_text('#!/bin/sh\n')
        self.binary.chmod(0o755)
        self.cli = self.root / 'claude'
        self.cli.write_text('#!/bin/sh\n')
        self.cli.chmod(0o755)
        self.runtime = self.root / 'runtime'
        (self.runtime / 'database/config').mkdir(parents=True)
        (self.runtime / 'fsgame.ltx').write_text('fs')
        (self.runtime / 'database/config/configs.db').write_bytes(pack_db({
            'configs\\misc\\sound\\script_sound_story.ltx': STORY_LTX.replace(b'story_', b'strange_story_')}))
        patcher = mock.patch.object(freeplay_play, 'REPO', self.repo)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.lines, self.execs = [], []
        self.run = FakeRun(SIGNED_IN)
        self.environ = {'PATH': '/usr/bin', 'HOME': '/home/p', 'ANTHROPIC_API_KEY': 'k1', 'XRAY_AGENT_API_KEY': 'k2',
                        'CLAUDECODE': '1', 'CLAUDE_CODE_SESSION_ID': 's', 'CLAUDE_CONFIG_DIR': '/home/p/.claude'}

    def main(self, *args, which=None, run=None):
        argv = ['--runtime', str(self.runtime), *args]
        return freeplay_play.main(argv, environ=self.environ, run=run or self.run,
                                  which=which or (lambda name: str(self.cli) if name == 'claude' else None),
                                  execve=lambda path, argv, env: self.execs.append((path, argv, env)),
                                  chdir=lambda path: self.lines.append(f'chdir {path}'),
                                  system='Darwin', machine='arm64', out=self.lines.append)

    def output(self):
        return '\n'.join(self.lines)

    def snapshot(self):
        return {p: p.read_bytes() for p in self.runtime.rglob('*') if p.is_file()}

    def assertStopped(self, rc, sentence):
        self.assertEqual(1, rc)
        self.assertIn(sentence, self.output())
        self.assertEqual([], self.execs)
        self.assertFalse((self.runtime / freeplay_runtime.ADAPTER_TARGET).exists())

    def test_missing_engine(self):
        self.binary.unlink()
        self.assertStopped(self.main(), 'The game engine is not built yet')

    def test_not_a_game_folder(self):
        (self.runtime / 'fsgame.ltx').unlink()
        self.assertStopped(self.main(), 'is not a staged game folder (it needs fsgame.ltx and database/).')

    def test_cli_not_found(self):
        self.assertStopped(self.main(which=lambda name: None),
                           'Claude Code was not found. Install it, sign in by running `claude`, then try again.')
        self.assertEqual([], self.run.calls)

    def test_cli_path_from_option_or_environment(self):
        self.assertStopped(self.main('--cli', str(self.root / 'nope')), 'Claude Code was not found at')
        self.environ['XRAY_AGENT_CLI'] = str(self.cli)
        self.assertEqual(0, self.main(which=lambda name: None))
        self.assertEqual(str(self.cli), self.run.calls[0][0][0])

    def test_signed_out(self):
        self.run = FakeRun(SIGNED_OUT)
        self.assertStopped(self.main(), 'Claude Code is not signed in. Run `claude auth login` and try again.')

    def test_auth_status_unreadable_or_failing(self):
        self.run = FakeRun('not json')
        self.assertStopped(self.main(), 'did not report whether it is signed in')
        self.run = FakeRun(raises=subprocess.TimeoutExpired('claude', 60))
        self.assertStopped(self.main(), 'did not answer `claude auth status`')

    def test_auth_check_uses_the_games_environment_and_hides_the_account(self):
        self.assertEqual(0, self.main())
        argv, kwargs = self.run.calls[0]
        self.assertEqual([str(self.cli), 'auth', 'status', '--json'], argv)
        self.assertNotIn('ANTHROPIC_API_KEY', kwargs['env'])
        self.assertNotIn('player@example.com', self.output())
        self.assertNotIn('Example Org', self.output())

    def test_command_and_environment(self):
        self.assertEqual(0, self.main('--model', 'claude-sonnet-5-5', '--calls-per-minute', '4'))
        (path, argv, env), = self.execs
        self.assertEqual(str(self.binary), path)
        self.assertEqual([str(self.binary), '-agent_bridge', 'appdata/agent_bridge.sock', '-xrsim_freeplay', 'live'], argv)
        for name in ('ANTHROPIC_API_KEY', 'XRAY_AGENT_API_KEY', 'CLAUDECODE', 'CLAUDE_CODE_SESSION_ID'):
            self.assertNotIn(name, env)
        self.assertEqual('claude_cli', env['XRAY_AGENT_PROVIDER'])
        self.assertEqual('claude-sonnet-5-5', env['XRAY_AGENT_MODEL'])
        self.assertEqual('4', env['XRAY_FREEPLAY_CALLS_PER_MINUTE'])
        self.assertEqual(str(self.cli), env['XRAY_AGENT_CLI'])
        self.assertEqual('/home/p/.claude', env['CLAUDE_CONFIG_DIR'])
        self.assertIn(f'chdir {self.runtime}', self.lines)
        self.assertIn('AI: Claude subscription via Claude Code', self.output())

    def test_defaults_and_background(self):
        self.assertEqual(0, self.main('--background'))
        (_, argv, env), = self.execs
        self.assertEqual(['-agent_bridge', 'appdata/agent_bridge.sock', '-agent_background', '-agent_sound',
                          '-xrsim_freeplay', 'live'], argv[1:])
        self.assertEqual('sonnet', env['XRAY_AGENT_MODEL'])
        self.assertEqual('6', env['XRAY_FREEPLAY_CALLS_PER_MINUTE'])

    def test_recorded_mode_needs_no_cli(self):
        self.assertEqual(0, self.main('--mode', 'recorded', which=lambda name: None))
        (_, argv, env), = self.execs
        self.assertEqual(['-xrsim_freeplay', 'recorded'], argv[-2:])
        self.assertNotIn('XRAY_AGENT_PROVIDER', env)
        self.assertEqual([], self.run.calls)

    def test_bad_model_or_budget(self):
        self.assertStopped(self.main('--model', 'a b'), 'must be one word')
        self.assertStopped(self.main('--calls-per-minute', '601'), 'between 0 (no limit) and 600')

    def test_dry_run_writes_nothing(self):
        before = self.snapshot()
        self.assertEqual(0, self.main('--dry-run', '--model', 'claude-sonnet-5-5'))
        self.assertEqual(before, self.snapshot())
        self.assertEqual([], self.execs)
        out = self.output()
        self.assertIn('would install: ' + str(self.runtime / freeplay_runtime.ADAPTER_TARGET), out)
        self.assertIn('would install: ' + str(self.runtime / STORY_REL), out)
        self.assertIn('-xrsim_freeplay live', out)
        self.assertIn('XRAY_AGENT_PROVIDER=claude_cli XRAY_AGENT_MODEL=claude-sonnet-5-5 '
                      'XRAY_FREEPLAY_CALLS_PER_MINUTE=6', out)
        self.assertIn('environment removals: ANTHROPIC_API_KEY CLAUDECODE CLAUDE_CODE_SESSION_ID XRAY_AGENT_API_KEY', out)

    def test_adapter_is_brought_up_to_date(self):
        target = self.runtime / freeplay_runtime.ADAPTER_TARGET
        target.parent.mkdir(parents=True)
        target.write_text('-- an older adapter\n')
        self.assertEqual(0, self.main())
        self.assertEqual(freeplay_runtime.ADAPTER.read_bytes(), target.read_bytes())
        self.assertTrue((self.runtime / STORY_REL).is_file())

    def test_differing_story_override_is_refused_not_overwritten(self):
        story = self.runtime / STORY_REL
        story.parent.mkdir(parents=True)
        story.write_bytes(b'; my own edit\r\n')
        self.assertStopped(self.main(), 'Cannot install the freeplay files')
        self.assertEqual(b'; my own edit\r\n', story.read_bytes())

    def test_windows_is_not_supported_yet(self):
        rc = freeplay_play.main(['--runtime', str(self.runtime)], environ=self.environ, run=self.run,
                                which=lambda name: None, execve=lambda *a: self.execs.append(a), chdir=lambda p: None,
                                system='Windows', machine='AMD64', out=self.lines.append)
        self.assertStopped(rc, 'supports macOS and Linux only for now')


if __name__ == '__main__':
    unittest.main()
