"""Player messages of the CoC freeplay adapter, run under LuaJIT when it is installed."""
import shutil
import subprocess
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'res/coc/gamedata/scripts/xrsim_freeplay.script'
HARNESS = ROOT / 'tools/tests/lua/freeplay_messages.lua'


@unittest.skipUnless(shutil.which('luajit'), 'luajit not installed')
class AdapterMessagesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out = subprocess.run(['luajit', str(HARNESS), str(SCRIPT)], capture_output=True, text=True, check=True).stdout
        cls.lines = dict(line.split('\t', 1) for line in out.splitlines())

    def test_activation_names_provider_model_and_budget(self):
        self.assertEqual(self.lines['live'], 'AI freeplay is on: Claude subscription via Claude Code, model sonnet, '
                         'up to 6 decisions per minute')
        self.assertEqual(self.lines['live_api'], 'AI freeplay is on: Anthropic API, model claude-x, '
                         'no limit on decisions per minute')
        self.assertEqual(self.lines['recorded'], 'AI freeplay is on in recorded mode (no model)')

    def test_refusals_say_what_to_do(self):
        self.assertIn('Claude Code was not found. Install it, sign in by running `claude`, then start the game again.',
                      self.lines['refusal:cli_unavailable'])
        self.assertEqual('AI freeplay is off: XRAY_AGENT_PROVIDER is anthropic but no Anthropic API key is set. Set '
                         'ANTHROPIC_API_KEY, or unset XRAY_AGENT_PROVIDER to play on your Claude subscription, '
                         'then start the game again.', self.lines['refusal:missing_api_key'])
        self.assertEqual('AI freeplay is off: no model is set for the Anthropic API. Set XRAY_AGENT_MODEL to a model '
                         'name, then start the game again.', self.lines['refusal:missing_model'])
        self.assertEqual('AI freeplay is off: playing on the Claude subscription through Claude Code is not supported '
                         'on this system yet (macOS and Linux only).', self.lines['refusal:unsupported_platform'])
        self.assertIn('XRAY_FREEPLAY_MAX_IN_FLIGHT', self.lines['refusal:invalid_setting:XRAY_FREEPLAY_MAX_IN_FLIGHT'])
        self.assertIn('XRAY_AGENT_PROVIDER', self.lines['refusal:provider_disabled'])

    def test_failures_and_notice_once_per_value(self):
        self.assertIn('Run `claude` in a terminal to check that you are signed in.', self.lines['failure:cli_exit'])
        self.assertIn('Claude Code is not answering in time.', self.lines['failure:cli_timeout'])
        self.assertIn('queue_shutdown', self.lines['failure:queue_shutdown'])
        self.assertEqual(self.lines['notice:none'], 'nil')
        self.assertEqual(self.lines['notice:first'], self.lines['failure:cli_exit'])
        self.assertEqual(self.lines['notice:repeat'], 'nil')
        self.assertEqual(self.lines['notice:other'], self.lines['failure:cli_timeout'])


if __name__ == '__main__':
    unittest.main()
