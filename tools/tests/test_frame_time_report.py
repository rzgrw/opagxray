import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.frame_time_report import (
    CaptureError,
    build_report,
    main,
    nearest_rank,
    parse_capture,
)


PHASE_NAMES = (
    "main",
    "render",
    "present",
    "worker_wait",
    "limiter",
    "inactive_sleep",
    "not_ready_sleep",
    "device_sleep",
    "load_draw",
    "fence_wait",
    "occlusion_wait",
)
BASE_FIELDS = (
    "loop_seq", "start_ns", "wall_ns", "engine_frame_begin", "engine_frame_end", "flags",
)
FIELDS = BASE_FIELDS + tuple(
    field for phase in PHASE_NAMES for field in (f"{phase}_calls", f"{phase}_ns")
)
METADATA = {
    "state": "stopped",
    "capacity": "32768",
    "warmup_loops": "0",
    "warmup_observed": "0",
    "dropped_loops": "0",
    "invalid_loops": "0",
    "truncated": "false",
    "clock_is_steady": "true",
    "clock_period_ns": "1",
    "source_build": "fixture-build",
    "renderer": "fixture-gl",
    "content_manifest": "fixture-content",
    "overlay": "fixture-overlay",
    "render_resolution": "1280x720",
    "drawable_resolution": "1280x720",
    "vsync": "off",
    "frame_limit": "501",
    "sdl_version": "fixture-sdl2",
    "allocator_build": "fixture-release-standard",
}
FLAG_NAMES = {
    "loading": 1,
    "menu": 2,
    "paused": 4,
    "precache": 8,
    "inactive": 16,
    "device_unavailable": 32,
    "window_visibility_unknown": 64,
}
NONSTEADY_FLAGS = sum(FLAG_NAMES.values())

# Literal schema and records, independent of the reporter and fixture builders.
# Core boundaries 100, 200, 300, 450, 500 with two warmup loops retain
# loop 2 [300,450] and loop 3 [450,500]; the stop-control loop is excluded.
CORE_CAPTURE = """# xray-frame-time-v1
# state=stopped
# capacity=3
# warmup_loops=2
# warmup_observed=2
# stored_loops=2
# dropped_loops=0
# invalid_loops=0
# truncated=false
# capture_start_ns=300
# capture_end_ns=500
# clock_is_steady=true
# clock_period_ns=1
# source_build=fixture-build
# renderer=fixture-gl
# content_manifest=fixture-content
# overlay=fixture-overlay
# render_resolution=1280x720
# drawable_resolution=2560x1440
# vsync=off
# frame_limit=0
# sdl_version=fixture-sdl2
# allocator_build=fixture-release-standard
loop_seq,start_ns,wall_ns,engine_frame_begin,engine_frame_end,flags,main_calls,main_ns,render_calls,render_ns,present_calls,present_ns,worker_wait_calls,worker_wait_ns,limiter_calls,limiter_ns,inactive_sleep_calls,inactive_sleep_ns,not_ready_sleep_calls,not_ready_sleep_ns,device_sleep_calls,device_sleep_ns,load_draw_calls,load_draw_ns,fence_wait_calls,fence_wait_ns,occlusion_wait_calls,occlusion_wait_ns
2,300,150,42,44,1,2,200,1,100,2,100,1,0,0,,0,,0,,0,,1,140,1,50,1,20
3,450,50,44,44,0,0,,0,,0,,0,,0,,0,,0,,0,,0,,0,,0,
"""


def record(loop, start, wall, flags=0, **changes):
    result = {
        "loop_seq": loop,
        "start_ns": start,
        "wall_ns": wall,
        "engine_frame_begin": loop,
        "engine_frame_end": loop,
        "flags": flags,
    }
    for phase in PHASE_NAMES:
        result[f"{phase}_calls"] = 0
        result[f"{phase}_ns"] = ""
    result.update(changes)
    return result


def capture_text(records=None, metadata_changes=None):
    records = [record(0, 0, 100)] if records is None else records
    metadata = dict(METADATA)
    metadata.update({
        "capacity": str(len(records) + 1),
        "stored_loops": str(len(records)),
        "capture_start_ns": str(records[0]["start_ns"]) if records else "0",
        "capture_end_ns": str(records[-1]["start_ns"] + records[-1]["wall_ns"])
            if records and isinstance(records[-1]["start_ns"], int) and
                isinstance(records[-1]["wall_ns"], int) else "0",
    })
    if metadata_changes:
        metadata.update(metadata_changes)
    lines = ["# xray-frame-time-v1"]
    lines.extend(f"# {key}={value}" for key, value in metadata.items())
    lines.append(",".join(FIELDS))
    for item in records:
        lines.append(",".join(str(item[field]) for field in FIELDS))
    return "\n".join(lines) + "\n"


def intervals(walls, flags=None):
    flags = list(flags or []) + [0] * (len(walls) - len(flags or []))
    result = []
    start = 0
    for loop, (wall, flag) in enumerate(zip(walls, flags)):
        result.append(record(loop, start, wall, flag))
        start += wall
    return result


class ParserTests(unittest.TestCase):
    def test_literal_capture_reports_both_populations_and_nested_phases(self):
        report = build_report(parse_capture(CORE_CAPTURE), scene="loading")
        self.assertTrue(report["acceptance"])
        self.assertIn("populations", report)
        all_rows = report["populations"]["all"]
        steady = report["populations"]["steady"]
        self.assertEqual(2, all_rows["sample_count"])
        self.assertEqual({"p50": 50, "p95": 150, "p99": 150, "maximum": 150}, all_rows["wall_ns"])
        self.assertEqual((2, 3, 300, 500), tuple(all_rows[key] for key in (
            "first_loop_seq", "last_loop_seq", "first_start_ns", "last_end_ns")))
        self.assertEqual(200, all_rows["sampled_wall_ns"])
        self.assertEqual(200, all_rows["elapsed_ns"])
        self.assertEqual({"loops": 1, "calls": 2, "duration_ns": 200}, all_rows["phases"]["main"])
        self.assertEqual({"loops": 1, "calls": 1, "duration_ns": 0}, all_rows["phases"]["worker_wait"])
        self.assertEqual(50, all_rows["phases"]["fence_wait"]["duration_ns"])
        self.assertEqual(20, all_rows["phases"]["occlusion_wait"]["duration_ns"])
        self.assertEqual(1, all_rows["flags"]["loading"])
        self.assertEqual(1, steady["sample_count"])
        self.assertEqual((3, 3, 450, 500), tuple(steady[key] for key in (
            "first_loop_seq", "last_loop_seq", "first_start_ns", "last_end_ns")))
        self.assertEqual(50, steady["wall_ns"]["p95"])
        self.assertEqual(0, steady["flags"]["loading"])
        self.assertEqual(1, report["sample_count"])

    def test_malformed_csv_is_not_repaired(self):
        text = capture_text()
        for malformed in (
            text.rstrip("\n") + '"\n',  # unterminated empty duration
            text.replace("0,0,100,", '0,0,"1\n00",'),  # newline within integer
            text.replace("0,0,100,", '0,0,"100"junk,'),
            text + "\n",
            text.rstrip("\n") + ",\n",
        ):
            with self.subTest(malformed=malformed[-100:]), self.assertRaises(CaptureError):
                parse_capture(malformed)

    def test_capacity_and_prefix_loop_accounting(self):
        invalid = (
            ([record(0, 100, 100)], {"capacity": "1"}),
            ([record(0, 100, 100)], {"capacity": "2", "truncated": "true", "dropped_loops": "1"}),
            ([record(0, 100, 100)], {"truncated": "true"}),
            ([record(1, 100, 100)], {}),  # missing prefix without invalid accounting
            ([record(1, 100, 100)], {"warmup_loops": "2", "warmup_observed": "2"}),
            ([record(1, 100, 100), record(3, 250, 100)], {"invalid_loops": "1"}),
            ([record(1, 100, 100)], {"warmup_loops": "1", "warmup_observed": "1", "invalid_loops": str(2**64 - 2)}),
            ([], {"warmup_loops": "2", "warmup_observed": "1", "dropped_loops": "1", "truncated": "true"}),
        )
        for rows, changes in invalid:
            with self.subTest(rows=rows, changes=changes), self.assertRaises(CaptureError):
                parse_capture(capture_text(rows, changes))
        full = build_report(parse_capture(capture_text(metadata_changes={"capacity": "1", "truncated": "true"})))
        self.assertEqual("partial", full["status"])
        self.assertFalse(full["acceptance"])

    def test_accounted_zero_time_invalid_loop_is_inspectable(self):
        # A mismatched phase at a repeated boundary can invalidate a zero-time loop.
        rows = [record(0, 100, 100), record(2, 200, 100)]
        report = build_report(parse_capture(capture_text(rows, {"invalid_loops": "1"})))
        self.assertEqual("partial", report["status"])
        self.assertFalse(report["acceptance"])

    def test_reversed_recovery_envelope_rejected_even_with_invalid_accounting(self):
        # Core can recover after an injected clock reversal at a lower origin.
        rows = [record(0, 100, 100), record(3, 20, 10)]
        with self.assertRaises(CaptureError):
            parse_capture(capture_text(rows, {"invalid_loops": "2"}))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 100, 100), record(1, 250, 100)], {"invalid_loops": "1"}))

    def test_not_ready_is_separate_from_device_loss_and_never_steady(self):
        capture = parse_capture(capture_text([
            record(0, 0, 100),
            record(1, 100, 100, not_ready_sleep_calls=1, not_ready_sleep_ns=0),
            record(2, 200, 100, not_ready_sleep_calls=1, not_ready_sleep_ns=100),
            record(3, 300, 100, flags=32, device_sleep_calls=1, device_sleep_ns=50),
        ]))
        steady = build_report(capture, scene="steady")
        self.assertEqual(1, steady["sample_count"])
        self.assertEqual(0, steady["last_loop_seq"])
        not_ready = build_report(capture, scene="not_ready")
        self.assertEqual(2, not_ready["sample_count"])
        self.assertEqual(0, not_ready["flags"]["device_unavailable"])
        lost = build_report(capture, scene="device_unavailable")
        self.assertEqual(1, lost["sample_count"])
        self.assertEqual(3, lost["first_loop_seq"])
        self.assertEqual(4, steady["populations"]["all"]["sample_count"])
        only_not_ready = capture_text([
            record(0, 0, 100, not_ready_sleep_calls=1, not_ready_sleep_ns=0)
        ])
        self.assertEqual("empty_selection", build_report(parse_capture(only_not_ready), scene="steady")["status"])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "not-ready.csv"
            path.write_text(only_not_ready)
            with mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(1, main([str(path), "--scene", "steady", "--acceptance"]))
                self.assertEqual(0, main([str(path), "--scene", "not_ready", "--acceptance"]))

    def test_unknown_window_visibility_is_not_occlusion_or_steady(self):
        capture = parse_capture(capture_text([
            record(0, 0, 100), record(1, 100, 100, flags=64),
            record(2, 200, 100, flags=65, load_draw_calls=1, load_draw_ns=20),
        ]))
        unknown = build_report(capture, scene="window_visibility_unknown")
        self.assertEqual(2, unknown["sample_count"])
        self.assertEqual(0, unknown["flags"]["device_unavailable"])
        self.assertEqual(1, build_report(capture, scene="steady")["sample_count"])
        self.assertEqual(3, build_report(capture)["sample_count"])

    def test_integer_bounds_and_unknown_flags(self):
        for changes in (
            {"engine_frame_begin": 2**32}, {"engine_frame_end": 2**32},
            {"flags": 128}, {"wall_ns": " 100"}, {"wall_ns": "+100"},
            {"wall_ns": "١٠٠"}, {"wall_ns": "9" * 5000},
            {"main_calls": 1, "main_ns": 2**64},
        ):
            with self.subTest(changes=str(changes)[:100]), self.assertRaises(CaptureError):
                parse_capture(capture_text([record(0, 0, 100, **changes)]))
        for changes in (
            {"capacity": "131073"}, {"warmup_loops": "1000001"},
            {"warmup_observed": "1"}, {"truncated": "yes"},
            {"clock_is_steady": "1"}, {"invalid_loops": str(2**64)},
            {"render_resolution": "4294967296x720"}, {"render_resolution": "9" * 5000 + "x720"},
            {"frame_limit": "-1"}, {"frame_limit": "4294967296"},
        ):
            with self.subTest(changes=str(changes)[:100]), self.assertRaises(CaptureError):
                parse_capture(capture_text(metadata_changes=changes))
        maximum = parse_capture(capture_text([record(0, 2**64 - 1, 0,
            engine_frame_begin=2**32 - 1, engine_frame_end=2**32 - 1,
            main_calls=1, main_ns=0)]))
        self.assertEqual(2**64 - 1, maximum.rows[0]["end_ns"])

    def test_all_required_provenance_fields_block_unknown_or_blank(self):
        fields = ("source_build", "renderer", "content_manifest", "overlay",
                  "render_resolution", "drawable_resolution", "vsync", "frame_limit",
                  "sdl_version", "allocator_build")
        for field in fields:
            for value in ("", " \t "):
                with self.subTest(field=field, value=value), self.assertRaises(CaptureError):
                    parse_capture(capture_text(metadata_changes={field: value}))
            for value in ("unknown", " UnAvAiLaBlE "):
                with self.subTest(field=field, value=value):
                    report = build_report(parse_capture(capture_text(metadata_changes={field: value})))
                    self.assertFalse(report["acceptance"])
                    self.assertIn(f"unknown_provenance:{field}", report["reasons"])

    def test_literal_core_begin_record_and_nearest_rank(self):
        core_record = record(2, 300, 150)
        capture = parse_capture(capture_text([core_record], {"warmup_loops": "2", "warmup_observed": "2"}))
        self.assertEqual(300, capture.rows[0]["start_ns"])
        self.assertEqual(150, capture.rows[0]["wall_ns"])
        report = build_report(capture)
        self.assertEqual("complete", report["status"])
        self.assertEqual(2, report["first_loop_seq"])
        self.assertEqual(300, report["first_start_ns"])
        self.assertEqual(450, report["last_end_ns"])
        self.assertEqual(10, nearest_rank(range(1, 21), 0.50))
        self.assertEqual(19, nearest_rank(range(1, 21), 0.95))
        self.assertEqual(20, nearest_rank(range(1, 21), 0.99))
        report = build_report(parse_capture(capture_text(intervals(list(range(20, 0, -1))))))
        self.assertEqual({"p50": 10, "p95": 19, "p99": 20, "maximum": 20}, report["wall_ns"])

    def test_duplicate_missing_and_independent_phase_schema(self):
        invalid = (
            capture_text().replace("# state=stopped\n", "# state=stopped\n# state=stopped\n"),
            capture_text().replace("# renderer=fixture-gl\n", ""),
            capture_text().replace("loop_seq,start_ns", "loop_seq,loop_seq"),
            capture_text().replace("wall_ns,", ""),
            capture_text().replace("# xray-frame-time-v1", "# xray-frame-time-v2"),
        )
        for text in invalid:
            with self.subTest(text=text[:100]), self.assertRaises(CaptureError):
                parse_capture(text)
        self.assertEqual(11, len(PHASE_NAMES))
        self.assertEqual("fence_wait", PHASE_NAMES[-2])
        self.assertEqual("occlusion_wait", PHASE_NAMES[-1])

    def test_invalid_numbers_and_overflow_rejected(self):
        for wall in ("-1", "nan", "inf", str(2**64), "1.5"):
            with self.subTest(wall=wall), self.assertRaises(CaptureError):
                parse_capture(capture_text([record(0, 0, wall)]))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 2**64 - 1, 1)]))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 0, 100, engine_frame_end=2**32)]))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 0, 100, main_calls=2**32, main_ns=0)]))

    def test_record_and_metadata_consistency(self):
        invalid = (
            {"stored_loops": "2"},
            {"dropped_loops": "1", "truncated": "false"},
            {"warmup_loops": "1", "warmup_observed": "0"},
            {"clock_is_steady": "false"},
            {"clock_period_ns": "0"},
            {"capture_start_ns": "1"},
            {"capture_end_ns": "50"},
            {"capture_end_ns": str(2**64 - 1)},
            {"state": "capturing"},
            {"state": "begin"},
            {"capacity": "0"},
        )
        for changes in invalid:
            with self.subTest(changes=changes), self.assertRaises(CaptureError):
                parse_capture(capture_text(metadata_changes=changes))

        for phase_change in (
            {"main_calls": 0, "main_ns": 0},
            {"main_calls": 1, "main_ns": ""},
        ):
            with self.subTest(changes=phase_change), self.assertRaises(CaptureError):
                parse_capture(capture_text([record(0, 0, 100, **phase_change)]))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 0, 100, engine_frame_begin=9, engine_frame_end=7)]))

    def test_envelope_overlap_and_unexplained_gaps_rejected(self):
        invalid_records = (
            [record(0, 100, 100), record(0, 200, 100)],
            [record(0, 100, 100), record(2, 200, 100)],
            [record(0, 100, 100), record(1, 150, 100)],
            [record(0, 100, 100), record(1, 50, 100)],
        )
        for records in invalid_records:
            with self.subTest(records=records), self.assertRaises(CaptureError):
                parse_capture(capture_text(records))

    def test_accounted_invalid_hole_is_partial(self):
        rows = [record(0, 100, 100), record(2, 250, 100)]
        report = build_report(parse_capture(capture_text(rows, {"invalid_loops": "1"})))
        self.assertEqual("partial", report["status"])
        self.assertFalse(report["acceptance"])
        self.assertEqual(2, report["sample_count"])
        self.assertIn("invalid_loops", report["reasons"])
        self.assertEqual(200, report["sampled_wall_ns"])
        self.assertEqual(250, report["elapsed_ns"])
        with self.assertRaises(CaptureError):
            parse_capture(capture_text(rows))
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([
                record(0, 100, 100), record(3, 250, 100)
            ], {"invalid_loops": "1"}))

    def test_phase_duration_bounds_allow_sound_nesting(self):
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 0, 100, main_calls=1, main_ns=101)]))
        nested = parse_capture(capture_text([
            record(0, 0, 100, main_calls=2, main_ns=150, present_calls=1, present_ns=100)
        ]))
        report = build_report(nested)
        self.assertEqual(2, report["phases"]["main"]["calls"])
        self.assertEqual(150, report["phases"]["main"]["duration_ns"])
        with self.assertRaises(CaptureError):
            parse_capture(capture_text([record(0, 0, 100, main_calls=3, main_ns=301)]))

    def test_provenance_resolution_and_vsync_validation(self):
        with self.assertRaises(CaptureError):
            parse_capture(capture_text(metadata_changes={"source_build": "   "}))
        for field, value in (
            ("render_resolution", "large"), ("drawable_resolution", "0x720"),
            ("render_resolution", "1280X720"), ("vsync", "perhaps"),
        ):
            with self.subTest(field=field, value=value), self.assertRaises(CaptureError):
                parse_capture(capture_text(metadata_changes={field: value}))
        unknown = build_report(parse_capture(capture_text(metadata_changes={
            "render_resolution": "unknown", "vsync": "unavailable"
        })))
        self.assertEqual("unprovenanced", unknown["status"])
        self.assertFalse(unknown["acceptance"])

    def test_empty_capture_selection_and_combined_reasons(self):
        empty = build_report(parse_capture(capture_text([])))
        self.assertEqual("empty_capture", empty["status"])
        self.assertEqual("empty_capture", empty["reasons"][0])
        self.assertNotIn("p50", empty["wall_ns"])

        selected = build_report(parse_capture(capture_text([
            record(0, 0, 100, NONSTEADY_FLAGS)
        ])), scene="steady")
        self.assertEqual("empty_selection", selected["status"])
        self.assertEqual(1, selected["stored_loops"])
        self.assertEqual(0, selected["sample_count"])
        self.assertEqual({}, selected["populations"]["steady"]["wall_ns"])
        self.assertEqual(1, selected["populations"]["all"]["sample_count"])

        combined = build_report(parse_capture(capture_text(
            metadata_changes={"invalid_loops": "1", "source_build": "unknown"}
        )))
        self.assertEqual("partial", combined["status"])
        self.assertIn("invalid_loops", combined["reasons"])
        self.assertIn("unknown_provenance:source_build", combined["reasons"])

    def test_strict_thresholds_filters_and_summaries(self):
        thresholds = (16_700_000, 33_300_000, 50_000_000, 100_000_000)
        walls = [value for threshold in thresholds for value in (threshold, threshold + 1)]
        report = build_report(parse_capture(capture_text(intervals(walls))))
        for index, threshold in enumerate(thresholds):
            self.assertEqual(len(walls) - 2 * index - 1, report["long_frames"][str(threshold)])

        filtered = build_report(parse_capture(capture_text(intervals(
            [100, 200, 300], [0, FLAG_NAMES["loading"], FLAG_NAMES["menu"]]
        ))), scene="steady")
        self.assertEqual("steady", filtered["scene_filter"])
        self.assertEqual(1, filtered["sample_count"])
        self.assertEqual(0, filtered["first_loop_seq"])
        self.assertEqual(100, filtered["wall_ns"]["p50"])

        summary = build_report(parse_capture(capture_text(intervals(
            [100, 200], [FLAG_NAMES["loading"], FLAG_NAMES["menu"]]
        ))))
        self.assertEqual(1, summary["flags"]["loading"])
        self.assertEqual(1, summary["flags"]["menu"])
        self.assertEqual(0, summary["flags"]["paused"])
        self.assertEqual(0, summary["first_loop_seq"])
        self.assertEqual(1, summary["last_loop_seq"])
        self.assertEqual(0, summary["first_start_ns"])
        self.assertEqual(300, summary["last_end_ns"])

    def test_cli_acceptance_failure_for_invalid_populations(self):
        cases = (
            capture_text(metadata_changes={"capacity": "1", "truncated": "true", "dropped_loops": "1"}),
            capture_text(metadata_changes={"invalid_loops": "1"}),
            capture_text(metadata_changes={"source_build": "unknown"}),
            capture_text([]),
        )
        for text in cases:
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "capture.csv"
                path.write_text(text)
                with mock.patch("sys.stdout", io.StringIO()):
                    self.assertEqual(1, main([str(path), "--acceptance"]))

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.csv"
            path.write_text(capture_text([record(0, 0, 100, FLAG_NAMES["loading"])]))
            with mock.patch("sys.stdout", io.StringIO()):
                self.assertEqual(1, main([str(path), "--acceptance", "--scene", "steady"]))

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.csv"
            path.write_text(capture_text())
            output = io.StringIO()
            with mock.patch("sys.stdout", output):
                self.assertEqual(0, main([str(path), "--acceptance"]))
            self.assertEqual(1, json.loads(output.getvalue())["sample_count"])

    def test_cli_process_exit_codes_and_combined_reasons(self):
        script = Path(__file__).resolve().parents[1] / "frame_time_report.py"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.csv"
            cases = (
                (CORE_CAPTURE, 0),
                (capture_text(metadata_changes={"capacity": "1", "truncated": "true",
                     "dropped_loops": "1", "invalid_loops": "1", "source_build": "unknown"}), 1),
                (capture_text([record(0, 100, 100), record(2, 250, 100)], {"invalid_loops": "1"}), 1),
                (capture_text(metadata_changes={"source_build": "unknown"}), 1),
                (capture_text().rstrip("\n") + '"\n', 2),
                (capture_text(metadata_changes={"source_build": " "}), 2),
                (capture_text([record(0, 100, 100), record(3, 20, 10)], {"invalid_loops": "2"}), 2),
            )
            for text, expected in cases:
                with self.subTest(expected=expected, text=text[-100:]):
                    path.write_text(text)
                    result = subprocess.run([sys.executable, str(script), str(path), "--acceptance"],
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(expected, result.returncode, result.stderr)
                    if expected == 2:
                        self.assertEqual("", result.stdout)
                        self.assertIn("frame-time report:", result.stderr)
                        self.assertNotIn("Traceback", result.stderr)
                    else:
                        report = json.loads(result.stdout)
                        self.assertEqual(expected == 0, report["acceptance"])
                        if "# dropped_loops=1\n" in text:
                            self.assertEqual(["truncated", "dropped_loops", "invalid_loops",
                                              "unknown_provenance:source_build"], report["reasons"])
            path.write_text(capture_text(metadata_changes={"invalid_loops": "1"}))
            diagnostic = subprocess.run([sys.executable, str(script), str(path)],
                                        capture_output=True, text=True, check=False)
            self.assertEqual(0, diagnostic.returncode)
            self.assertFalse(json.loads(diagnostic.stdout)["acceptance"])


if __name__ == "__main__":
    unittest.main()
