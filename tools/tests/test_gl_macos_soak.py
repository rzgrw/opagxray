import json
import os
import tempfile
import time
import unittest
from unittest import mock
from pathlib import Path

from tools.gl_macos_soak import (
    ScenarioStep,
    parse_scenario_lines,
    parse_state_payload,
    scan_log_text,
    write_json,
)


class ScenarioParserTests(unittest.TestCase):
    def test_parse_ignores_blank_lines_and_comments(self):
        steps = parse_scenario_lines([
            "",
            "# comment",
            "hello",
            "sleep 1.5",
            "cmd main_menu off",
        ])

        self.assertEqual([
            ScenarioStep("bridge", ("hello", ""), "hello"),
            ScenarioStep("sleep", ("1.5",), "sleep 1.5"),
            ScenarioStep("bridge", ("cmd", "main_menu off"), "cmd main_menu off"),
        ], steps)

    def test_parse_rejects_empty_sleep(self):
        with self.assertRaisesRegex(ValueError, "sleep requires seconds"):
            parse_scenario_lines(["sleep"])


class StateParserTests(unittest.TestCase):
    def test_parse_state_payload_keeps_position_value(self):
        parsed = parse_state_payload(
            "scene=game fps=60 frame=123 paused=0 loadscr=0 precache=0 "
            "level=l05_bar pos=1.0,2.0,3.0 hp=0.95 time=456"
        )

        self.assertEqual("game", parsed["scene"])
        self.assertEqual("60", parsed["fps"])
        self.assertEqual("123", parsed["frame"])
        self.assertEqual("1.0,2.0,3.0", parsed["pos"])


class LogScannerTests(unittest.TestCase):
    def test_scan_log_text_reports_fatal_gl_and_lua_lines(self):
        findings = scan_log_text(
            "* harmless\n"
            "! error: GL_INVALID_OPERATION during draw\n"
            "FATAL ERROR: crash\n"
            "lua runtime error: sound_theme.script\n"
        )

        self.assertEqual([
            "! error: GL_INVALID_OPERATION during draw",
            "FATAL ERROR: crash",
            "lua runtime error: sound_theme.script",
        ], findings)

    def test_scan_log_text_ignores_known_noise(self):
        self.assertEqual([], scan_log_text("* normal startup\n~ warning: cached texture\n"))


class SummaryWriterTests(unittest.TestCase):
    def test_write_json_creates_parent_and_stable_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "nested" / "summary.json"
            write_json(path, {"b": 2, "a": 1})

            self.assertEqual({"a": 1, "b": 2}, json.loads(path.read_text()))
            self.assertTrue(path.read_text().endswith("\n"))


class DryRunCliTests(unittest.TestCase):
    def test_ai_zone_smoke_script_exercises_ai_bridge_verbs(self):
        scenario = Path("tools/ai_zone_smoke.txt")
        steps = parse_scenario_lines(scenario.read_text().splitlines())

        commands = [step.raw for step in steps if step.kind == "bridge"]
        self.assertIn("ai.reset", commands)
        self.assertIn("ai.observe", commands)
        self.assertIn("ai.wake", commands)
        self.assertIn("ai.inject adjust_population debug_region blind_dog 500", commands)
        self.assertIn("ai.snapshot", commands)
        self.assertIn("ai.log", commands)
        self.assertIn("ai.replay", commands)
        self.assertIn("agent.list", commands)
        self.assertIn("agent.provider", commands)
        self.assertIn("agent.provider live", commands)
        self.assertIn("agent.prompt", commands)
        self.assertIn("agent.wake 1", commands)
        self.assertIn("agent.tree", commands)

    def test_ai_thin_harness_smoke_script_exercises_actor_verbs(self):
        scenario = Path("tools/ai_thin_harness_smoke.txt")
        steps = parse_scenario_lines(scenario.read_text().splitlines())

        commands = [step.raw for step in steps if step.kind == "bridge"]
        start = commands.index("ai.reset")
        end = commands.index("ai.log")
        self.assertEqual([
            "ai.reset",
            "agent.provider live",
            "agent.actor.list",
            "agent.actor.observe squad",
            "agent.actor.wake squad",
            "agent.actor.commands",
            "agent.actor.wake squad live",
            "agent.actor.poll 1",
            "agent.actor.observe mutant_pack",
            "agent.actor.wake mutant_pack",
            "agent.actor.commands",
            "ai.snapshot",
            "ai.log",
        ], commands[start : end + 1])

    def test_dry_run_writes_summary(self):
        from tools.gl_macos_soak import main

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scenario = root / "scenario.txt"
            artifacts = root / "artifacts"
            scenario.write_text("hello\nstate\n")

            rc = main(["--dry-run", "--scenario", str(scenario), "--artifacts", str(artifacts)])

            self.assertEqual(0, rc)
            summary = json.loads((artifacts / "summary.json").read_text())
            self.assertEqual(True, summary["dry_run"])
            self.assertEqual(2, summary["step_count"])
            self.assertEqual(["hello", "state"], summary["steps"])


class RssParserTests(unittest.TestCase):
    def test_parse_ps_rss_output(self):
        from tools.gl_macos_soak import parse_ps_rss_output

        self.assertEqual(123456, parse_ps_rss_output("  RSS\n123456\n"))
        self.assertIsNone(parse_ps_rss_output(""))
        self.assertIsNone(parse_ps_rss_output("RSS\nnot-a-number\n"))


class SummaryAnalysisTests(unittest.TestCase):
    def test_frame_progress_detects_stall(self):
        from tools.gl_macos_soak import longest_frame_stall_sec

        samples = [
            {"t": 0.0, "state": {"frame": "10"}},
            {"t": 1.0, "state": {"frame": "10"}},
            {"t": 2.0, "state": {"frame": "11"}},
            {"t": 7.0, "state": {"frame": "11"}},
        ]

        self.assertEqual(5.0, longest_frame_stall_sec(samples))


class TimeoutHelperTests(unittest.TestCase):
    def test_bounded_sleep_seconds_clamps_to_remaining_deadline(self):
        from tools.gl_macos_soak import bounded_sleep_seconds, process_wait_timeout_seconds

        self.assertEqual((2.0, True), bounded_sleep_seconds(45.0, 2.0))
        self.assertEqual((1.5, False), bounded_sleep_seconds(1.5, 2.0))
        self.assertEqual((0.0, True), bounded_sleep_seconds(1.0, -0.1))

        self.assertEqual((10.0, False), process_wait_timeout_seconds(10.0))
        self.assertEqual((0.0, True), process_wait_timeout_seconds(0.0))
        self.assertEqual((0.0, True), process_wait_timeout_seconds(-1.0))

    def test_sleep_process_aware_stops_when_process_already_exited(self):
        from tools.gl_macos_soak import sleep_process_aware

        class ExitedProcess:
            def poll(self):
                return -11

        class RunningProcess:
            def poll(self):
                return None

        self.assertFalse(sleep_process_aware(10.0, ExitedProcess()))
        self.assertTrue(sleep_process_aware(0.0, RunningProcess()))


class SocketWaitTests(unittest.TestCase):
    def test_relative_binary_launch_arg_becomes_absolute(self):
        from tools.gl_macos_soak import resolve_binary_path

        self.assertEqual(Path.cwd() / "bin/arm64/Release/xr_3da", resolve_binary_path(Path("bin/arm64/Release/xr_3da")))

    def test_relative_socket_launch_arg_stays_relative_for_spaced_game_dir(self):
        from tools.gl_macos_soak import build_launch_command, resolve_socket_path

        game_dir = Path("/tmp/S.T.A.L.K.E.R. - Call of Chernobyl")
        socket_arg = Path("appdata/agent_bridge.sock")

        self.assertEqual(game_dir / socket_arg, resolve_socket_path(game_dir, socket_arg))
        self.assertEqual(
            ["/tmp/xr_3da", "-agent_bridge", "appdata/agent_bridge.sock"],
            build_launch_command(Path("/tmp/xr_3da"), socket_arg),
        )

    def test_wait_for_socket_fails_when_process_exits_first(self):
        from tools.gl_macos_soak import wait_for_socket

        class ExitedProcess:
            returncode = 42

            def poll(self):
                return self.returncode

        with tempfile.TemporaryDirectory() as tmp:
            missing_socket = Path(tmp) / "missing.sock"

            with self.assertRaisesRegex(RuntimeError, "process exited before socket appeared: 42"):
                wait_for_socket(missing_socket, 60.0, ExitedProcess())


class BridgeCommandTests(unittest.TestCase):
    def test_is_quit_command_only_matches_console_quit(self):
        from tools.gl_macos_soak import is_quit_command

        self.assertTrue(is_quit_command("cmd", " quit "))
        self.assertFalse(is_quit_command("cmd", "flush"))
        self.assertFalse(is_quit_command("bye", ""))


class LiveCliTests(unittest.TestCase):
    def test_live_cli_fails_nonzero_process_returncode(self):
        from tools.gl_macos_soak import main

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scenario = root / "scenario.txt"
            artifacts = root / "artifacts"
            scenario.write_text("hello\n")

            with mock.patch("tools.gl_macos_soak.run_scenario") as run_scenario:
                run_scenario.return_value = {
                    "timed_out": False,
                    "exception": "",
                    "bridge_failures": [],
                    "process_returncode": -11,
                }

                rc = main(["--scenario", str(scenario), "--artifacts", str(artifacts)])

            self.assertEqual(1, rc)


class SummaryValidationTests(unittest.TestCase):
    def test_evaluate_summary_passes_clean_run(self):
        from tools.gl_macos_soak import evaluate_summary

        summary = {
            "timed_out": False,
            "exception": "",
            "bridge_failures": [],
            "process_returncode": 0,
            "log_findings": [],
            "states": [{"state": {"scene": "game", "frame": "100"}}],
            "rss_high_water_kb": 123,
            "longest_frame_stall_sec": 1.0,
        }

        passed, failures = evaluate_summary(summary)
        self.assertTrue(passed)
        self.assertEqual([], failures)

    def test_evaluate_summary_reports_failures(self):
        from tools.gl_macos_soak import evaluate_summary

        summary = {
            "timed_out": True,
            "exception": "socket did not appear",
            "bridge_failures": [{"command": "state", "response": "err"}],
            "process_returncode": None,
            "log_findings": ["FATAL ERROR"],
            "states": [],
            "rss_high_water_kb": None,
            "longest_frame_stall_sec": 11.5,
        }

        passed, failures = evaluate_summary(summary)
        self.assertFalse(passed)
        self.assertIn("scenario timed out", failures)
        self.assertIn("exception: socket did not appear", failures)
        self.assertIn("bridge command failed: state -> err", failures)
        self.assertIn("process did not exit cleanly: None", failures)
        self.assertIn("log finding: FATAL ERROR", failures)
        self.assertIn("no state samples recorded", failures)
        self.assertIn("no RSS samples recorded", failures)
        self.assertIn("frame stall exceeded 10 seconds: 11.5", failures)


class ScreenshotTests(unittest.TestCase):
    def test_expected_screenshots_from_steps(self):
        from tools.gl_macos_soak import expected_screenshots

        steps = parse_scenario_lines(["shot first", "state", "shot second"])

        self.assertEqual(["first", "second"], expected_screenshots(steps))

    def test_collect_screenshot_status(self):
        from tools.gl_macos_soak import collect_screenshot_status

        with tempfile.TemporaryDirectory() as tmp:
            game_dir = Path(tmp)
            screenshot_dir = game_dir / "appdata" / "screenshots"
            screenshot_dir.mkdir(parents=True)
            (screenshot_dir / "exists.jpg").write_bytes(b"jpg")

            status = collect_screenshot_status(game_dir, ["exists", "missing"])

            self.assertEqual("exists", status[0]["name"])
            self.assertEqual(str(screenshot_dir / "exists.jpg"), status[0]["path"])
            self.assertEqual(True, status[0]["exists"])
            self.assertEqual(3, status[0]["size"])
            self.assertIn("mtime", status[0])
            self.assertEqual(
                {"name": "missing", "path": str(screenshot_dir / "missing.jpg"), "exists": False, "size": 0},
                status[1],
            )

    def test_collect_screenshot_status_marks_stale_files(self):
        from tools.gl_macos_soak import collect_screenshot_status

        with tempfile.TemporaryDirectory() as tmp:
            game_dir = Path(tmp)
            screenshot_dir = game_dir / "appdata" / "screenshots"
            screenshot_dir.mkdir(parents=True)
            shot = screenshot_dir / "old.jpg"
            shot.write_bytes(b"jpg")
            os.utime(shot, (1, 1))

            status = collect_screenshot_status(game_dir, ["old"], not_before=time.time())

            self.assertFalse(status[0]["fresh"])

    def test_remove_expected_screenshots_deletes_only_named_files(self):
        from tools.gl_macos_soak import remove_expected_screenshots

        with tempfile.TemporaryDirectory() as tmp:
            game_dir = Path(tmp)
            screenshot_dir = game_dir / "appdata" / "screenshots"
            screenshot_dir.mkdir(parents=True)
            (screenshot_dir / "delete.jpg").write_bytes(b"jpg")
            (screenshot_dir / "keep.jpg").write_bytes(b"jpg")

            remove_expected_screenshots(game_dir, ["delete"])

            self.assertFalse((screenshot_dir / "delete.jpg").exists())
            self.assertTrue((screenshot_dir / "keep.jpg").exists())


class FrameWaitTests(unittest.TestCase):
    def test_parser_accepts_positive_frame_count_and_rejects_invalid_counts(self):
        self.assertEqual([ScenarioStep('wait_frames', ('2',), 'wait_frames 2')],
                         parse_scenario_lines(['wait_frames 2']))
        for line in ('wait_frames', 'wait_frames 0', 'wait_frames -1', 'wait_frames 1.5'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                parse_scenario_lines([line])

    def test_wait_requires_advancement_from_first_observed_frame(self):
        from tools.gl_macos_soak import wait_for_frames
        responses = iter([{'frame': '40'}, {'frame': '40'}, {'frame': '41'}, {'frame': '42'}])
        with mock.patch('tools.gl_macos_soak.time.sleep'):
            states = wait_for_frames(lambda _budget: next(responses), 2, timeout=1.0)
        self.assertEqual(['40', '40', '41', '42'], [s['frame'] for s in states])

    def test_wait_times_out_on_stalled_frames(self):
        from tools.gl_macos_soak import wait_for_frames
        with mock.patch('tools.gl_macos_soak.time.monotonic', side_effect=[0.0, 0.0, 0.1, 2.0]), \
                mock.patch('tools.gl_macos_soak.time.sleep'):
            with self.assertRaises(TimeoutError):
                wait_for_frames(lambda _budget: {'frame': '40'}, 2, timeout=1.0)

    def test_wait_rejects_state_without_numeric_frame(self):
        from tools.gl_macos_soak import wait_for_frames
        with self.assertRaises(ValueError):
            wait_for_frames(lambda _budget: {'scene': 'menu'}, 2, timeout=1.0)


if __name__ == "__main__":
    unittest.main()
