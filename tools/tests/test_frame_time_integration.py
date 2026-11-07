"""Literal C++ production-writer fixture, independently checked by strict Python parser."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools import frame_time_report as report

ROOT = Path(__file__).resolve().parents[2]


class CppExportRoundTripTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="xray-frame-roundtrip-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.fixture = Path(cls.temp.name) / "capture.csv"
        binary = Path(cls.temp.name) / "session-tests"
        subprocess.run([
            "c++", "-std=c++20", "-fno-exceptions", "-pthread", "-I" + str(ROOT / "src"),
            str(ROOT / "src/xrEngine/FrameTimeCollector.cpp"),
            str(ROOT / "src/xrEngine/FrameTimeSession.cpp"),
            str(ROOT / "src/xrEngine/tests/FrameTimeSessionTests.cpp"), "-o", str(binary),
        ], check=True, capture_output=True, text=True)
        cls.cpp_run = subprocess.run([str(binary), str(cls.fixture)], check=True,
                                 capture_output=True, text=True)

    def test_production_control_export_lifecycle(self):
        self.assertIn("PASS: session control", self.cpp_run.stdout)

    def test_literal_integer_boundaries_and_nested_spans(self):
        capture = report.parse_capture(self.fixture.read_text())
        self.assertEqual(len(capture.rows), 1)
        row = capture.rows[0]
        self.assertEqual((row["loop_seq"], row["start_ns"], row["wall_ns"]), (0, 2000, 100))
        self.assertEqual((row["engine_frame_begin"], row["engine_frame_end"]), (40, 42))
        self.assertEqual((row["render_calls"], row["render_ns"]), (1, 30))
        self.assertEqual((row["present_calls"], row["present_ns"]), (1, 10))
        self.assertEqual(row["main_calls"], 0)
        self.assertEqual(capture.metadata["capture_end_ns"], "2100")

    def test_real_cli_acceptance_of_known_fixture(self):
        result = subprocess.run(["python3", str(ROOT / "tools/frame_time_report.py"),
                                 str(self.fixture), "--acceptance"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"sample_count": 1', result.stdout)

    def test_unknown_assets_stay_diagnostic(self):
        unknown = Path(self.temp.name) / "unknown.csv"
        unknown.write_text(self.fixture.read_text().replace("content_manifest=fixture-content", "content_manifest=unknown"))
        result = subprocess.run(["python3", str(ROOT / "tools/frame_time_report.py"),
                                 str(unknown), "--acceptance"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("unprovenanced", result.stdout)
