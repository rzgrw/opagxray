# GL macOS Stability Baseline Implementation Plan

> **Status reconciliation (2026-11-07):** HARNESS SHIPPED: `7a9b032cc`…`76209178d` plus fixes. Five-run GL and 20-minute idle acceptance remain open; use explicit private paths and a fresh named save, not Downloads/save `1`. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a repeatable OpenGL macOS soak/benchmark harness that proves `renderer_r3` is stable, measurable, and ready to host the AI overhaul on rz's 24 GB Apple Silicon Mac.

**Architecture:** Add a Python wrapper in `tools/` around the existing `-agent_bridge` socket protocol. The wrapper launches the game from the CoC run directory, drives plain-text bridge scenarios, samples bridge state and process RSS, isolates logs, validates screenshots/logs/process exit, and writes JSON/text artifacts. Keep engine code untouched in the first pass; add C++ bridge metrics only after this harness shows a concrete missing signal.

**Tech Stack:** Python 3 standard library (`argparse`, `subprocess`, `socket`, `threading`, `json`, `unittest`), existing `tools/agentctl.py` protocol, macOS `ps`, existing `bin/arm64/Release/xr_3da`, existing CoC run directory.

## Global Constraints

- Primary runtime is macOS on rz's 24 GB Apple Silicon Mac.
- Renderer is `renderer_r3`.
- Engine binary is `bin/arm64/Release/xr_3da`.
- Active CoC run directory is `/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl`.
- Launch mode is `-agent_bridge`; save `1` on `l05_bar` is the main loaded-game scenario.
- Do not use `-nosound`.
- Do not implement `xrMind`, `xrSim`, or live provider calls in this milestone.
- Do not add a new control channel; use the existing Unix-socket agent bridge.
- Prefer GL harness changes before engine changes.
- Leave unrelated untracked or modified files alone.

---

## File Structure

- Create `tools/gl_macos_soak.py`: command-line harness, scenario parser, launcher, bridge driver, sampler, validators, artifact writer.
- Create `tools/gl_menu_smoke.txt`: menu startup/screenshot/quit scenario.
- Create `tools/gl_load_play_save_load.txt`: GL gate scenario based on `tools/bridge_soak.txt`.
- Create `tools/gl_idle_soak.txt`: loaded-game idle scenario with a 60-second default wait.
- Create `tools/tests/__init__.py`: make `tools.tests` importable by `unittest`.
- Create `tools/tests/test_gl_macos_soak.py`: unit tests for parser, state parsing, log scanning, summary writing, and dry-run behavior.
- Modify `docs/macos-dev-setup.md`: document the GL gate command and artifact output.
- Modify `docs/HANDOVER.md`: add the GL baseline command once the harness exists.

---

### Task 1: Parser, State, Log, And Summary Units

**Files:**
- Create: `tools/gl_macos_soak.py`
- Create: `tools/tests/__init__.py`
- Create: `tools/tests/test_gl_macos_soak.py`

**Interfaces:**
- Produces: `ScenarioStep(kind: str, args: tuple[str, ...], raw: str)`
- Produces: `parse_scenario_lines(lines: Iterable[str]) -> list[ScenarioStep]`
- Produces: `parse_state_payload(payload: str) -> dict[str, str]`
- Produces: `scan_log_text(text: str) -> list[str]`
- Produces: `write_json(path: Path, data: dict[str, Any]) -> None`
- Consumes: no project runtime; this task is pure Python.

- [ ] **Step 1: Write failing unit tests**

Create `tools/tests/__init__.py` as an empty file.

Create `tools/tests/test_gl_macos_soak.py` with:

```python
import json
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'tools.gl_macos_soak'`.

- [ ] **Step 3: Implement pure helpers**

Create `tools/gl_macos_soak.py` with:

```python
#!/usr/bin/env python3
"""GL macOS stability soak harness for OpenXVibeRay.

This tool wraps the existing agent bridge. It intentionally starts with
coarse, reliable signals: bridge state, process RSS, screenshots, logs, and
process exit.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME_DIR = Path("/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl")
DEFAULT_BINARY = REPO_ROOT / "bin/arm64/Release/xr_3da"
DEFAULT_SOCKET_REL = Path("appdata/agent_bridge.sock")
DEFAULT_LOG_REL = Path("appdata/logs/openxray_radik zagirov.log")


@dataclass(frozen=True)
class ScenarioStep:
    kind: str
    args: tuple[str, ...]
    raw: str


def parse_scenario_lines(lines: Iterable[str]) -> list[ScenarioStep]:
    steps: list[ScenarioStep] = []
    for line_no, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        verb, sep, payload = line.partition(" ")
        if verb == "sleep":
            if not sep or not payload.strip():
                raise ValueError(f"line {line_no}: sleep requires seconds")
            seconds = payload.strip()
            float(seconds)
            steps.append(ScenarioStep("sleep", (seconds,), line))
            continue
        steps.append(ScenarioStep("bridge", (verb, payload.strip()), line))
    return steps


def parse_scenario_file(path: Path) -> list[ScenarioStep]:
    return parse_scenario_lines(path.read_text().splitlines())


def parse_state_payload(payload: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for token in payload.split():
        key, sep, value = token.partition("=")
        if sep:
            parsed[key] = value
    return parsed


def scan_log_text(text: str) -> list[str]:
    needles = (
        "FATAL",
        "fatal",
        "GL_INVALID",
        "OpenGL error",
        "lua runtime error",
        "stack traceback",
    )
    findings: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if any(needle in line for needle in needles):
            findings.append(line)
    return findings


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="parse inputs and write a dry-run summary")
    parser.add_argument("--scenario", type=Path, required=True, help="scenario text file")
    parser.add_argument("--artifacts", type=Path, default=REPO_ROOT / "artifacts/gl_macos_soak")
    args = parser.parse_args(argv)

    steps = parse_scenario_file(args.scenario)
    if args.dry_run:
        write_json(args.artifacts / "summary.json", {
            "dry_run": True,
            "scenario": str(args.scenario),
            "steps": [step.raw for step in steps],
        })
        print(f"dry-run ok: {len(steps)} steps")
        return 0

    print("live execution is added in Task 3")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: PASS for 5 tests.

- [ ] **Step 5: Commit**

```bash
git add tools/gl_macos_soak.py tools/tests/__init__.py tools/tests/test_gl_macos_soak.py
git commit -m "test: add GL soak harness helpers"
```

---

### Task 2: Scenario Files And Dry-Run CLI

**Files:**
- Modify: `tools/gl_macos_soak.py`
- Modify: `tools/tests/test_gl_macos_soak.py`
- Create: `tools/gl_menu_smoke.txt`
- Create: `tools/gl_load_play_save_load.txt`
- Create: `tools/gl_idle_soak.txt`

**Interfaces:**
- Consumes: `parse_scenario_file(path: Path) -> list[ScenarioStep]`
- Produces: CLI `python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_menu_smoke.txt --artifacts artifacts/gl_macos_soak/dry_menu`
- Produces: dry-run `summary.json` with `dry_run`, `scenario`, `step_count`, `steps`.

- [ ] **Step 1: Extend tests for dry-run summary**

Append to `tools/tests/test_gl_macos_soak.py`:

```python
class DryRunCliTests(unittest.TestCase):
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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak.DryRunCliTests -v
```

Expected: FAIL with `KeyError: 'step_count'`.

- [ ] **Step 3: Add dry-run step count**

Modify the `if args.dry_run:` block in `tools/gl_macos_soak.py` to:

```python
    if args.dry_run:
        write_json(args.artifacts / "summary.json", {
            "dry_run": True,
            "scenario": str(args.scenario),
            "step_count": len(steps),
            "steps": [step.raw for step in steps],
        })
        print(f"dry-run ok: {len(steps)} steps")
        return 0
```

- [ ] **Step 4: Add scenario files**

Create `tools/gl_menu_smoke.txt`:

```text
# GL menu smoke: launch to menu, prove bridge/screenshot/quit.
hello
state
shot gl_menu_smoke
cmd flush
cmd quit
```

Create `tools/gl_load_play_save_load.txt`:

```text
# GL stability gate: load, move/look, save/load, inventory, quit.
hello
state
cmd start server(1/single/alife/load) client(localhost)
sleep 45
key space tap
sleep 2
state
key w down
sleep 2
key w up
key s down
sleep 1
key s up
key a down
sleep 1
key a up
key d down
sleep 1
key d up
state
mouse move 200 0
sleep 1
mouse move 200 0
sleep 1
mouse move -400 50
sleep 1
state
shot gl_soak_look
key x down
key w down
sleep 2
key w up
key x up
key space tap
sleep 1
key c tap
sleep 1
key c tap
state
cmd save soaktest
sleep 3
cmd load soaktest
sleep 40
key space tap
sleep 2
state
lua db.actor:position().x .. "," .. db.actor:position().z
shot gl_soak_after_reload
sleep 2
key i tap
sleep 1
shot gl_soak_inventory
key escape tap
sleep 1
state
cmd flush
cmd quit
```

Create `tools/gl_idle_soak.txt`:

```text
# GL idle soak: 60-second default wait for the first harness pass.
hello
state
cmd start server(1/single/alife/load) client(localhost)
sleep 45
key space tap
sleep 2
state
sleep 60
state
shot gl_idle_final
cmd flush
cmd quit
```

- [ ] **Step 5: Run tests and dry-run all scenarios**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_menu_smoke.txt --artifacts artifacts/gl_macos_soak/dry_menu
python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_load_play_save_load.txt --artifacts artifacts/gl_macos_soak/dry_load
python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_idle_soak.txt --artifacts artifacts/gl_macos_soak/dry_idle
```

Expected: unit tests PASS; each dry-run prints a `dry-run ok:` line with its parsed step count; each dry-run writes `summary.json`.

- [ ] **Step 6: Commit**

```bash
git add tools/gl_macos_soak.py tools/tests/test_gl_macos_soak.py tools/gl_menu_smoke.txt tools/gl_load_play_save_load.txt tools/gl_idle_soak.txt
git commit -m "test: add GL soak scenarios"
```

---

### Task 3: Live Launch, Bridge Drive, And Sampling

**Files:**
- Modify: `tools/gl_macos_soak.py`
- Modify: `tools/tests/test_gl_macos_soak.py`

**Interfaces:**
- Consumes: scenario files from Task 2.
- Produces: `BridgeClient(path: Path, timeout: float)`
- Produces: `sample_rss_kb(pid: int) -> int | None`
- Produces: CLI live options `--game-dir`, `--binary`, `--socket`, `--sample-interval`, `--timeout`.
- Produces: live summary fields `states`, `rss_samples`, `bridge_failures`, `process_returncode`, `duration_sec`.

- [ ] **Step 1: Write tests for RSS parsing and fake live driver seam**

Append to `tools/tests/test_gl_macos_soak.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak.RssParserTests tools.tests.test_gl_macos_soak.SummaryAnalysisTests -v
```

Expected: FAIL with missing `parse_ps_rss_output` and `longest_frame_stall_sec`.

- [ ] **Step 3: Implement live runner helpers**

Add imports near the top of `tools/gl_macos_soak.py`:

```python
import socket
import threading
```

Add these helpers after `write_json`:

```python
class BridgeClient:
    def __init__(self, path: Path, timeout: float = 15.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(str(path))
        self.buf = b""
        self.next_id = 1

    def close(self) -> None:
        self.sock.close()

    def request(self, verb: str, payload: str = "") -> tuple[bool, str]:
        rid = str(self.next_id)
        self.next_id += 1
        line = f"{rid} {verb} {payload}".strip() + "\n"
        self.sock.sendall(line.encode())
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                resp = self.buf[:nl].decode()
                self.buf = self.buf[nl + 1:]
                parts = resp.split(" ", 2)
                if parts[0] != rid:
                    continue
                ok = len(parts) > 1 and parts[1] == "ok"
                payload_out = parts[2] if len(parts) > 2 else ""
                return ok, payload_out.replace("\\n", "\n")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed the connection")
            self.buf += chunk


def parse_ps_rss_output(output: str) -> int | None:
    for raw in output.splitlines():
        line = raw.strip()
        if not line or line == "RSS":
            continue
        try:
            return int(line)
        except ValueError:
            return None
    return None


def sample_rss_kb(pid: int) -> int | None:
    proc = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return parse_ps_rss_output(proc.stdout)


def longest_frame_stall_sec(samples: list[dict[str, Any]]) -> float:
    longest = 0.0
    last_frame: int | None = None
    last_change_t: float | None = None
    for sample in samples:
        state = sample.get("state", {})
        try:
            frame = int(state.get("frame", ""))
        except ValueError:
            continue
        t = float(sample["t"])
        if last_frame is None or frame != last_frame:
            last_frame = frame
            last_change_t = t
            continue
        if last_change_t is not None:
            longest = max(longest, t - last_change_t)
    return longest


def wait_for_socket(path: Path, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"socket did not appear: {path}")
```

- [ ] **Step 4: Implement live `run_scenario`**

Add this function before `main`:

```python
def run_scenario(args: argparse.Namespace, steps: list[ScenarioStep]) -> dict[str, Any]:
    game_dir = args.game_dir
    socket_path = args.socket if args.socket.is_absolute() else game_dir / args.socket
    log_path = game_dir / DEFAULT_LOG_REL
    artifacts = args.artifacts
    artifacts.mkdir(parents=True, exist_ok=True)

    if log_path.exists():
        log_path.write_text("")
    if socket_path.exists():
        socket_path.unlink()

    command = [str(args.binary), "-agent_bridge", str(socket_path)]
    started = time.monotonic()
    proc = subprocess.Popen(
        command,
        cwd=str(game_dir),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    states: list[dict[str, Any]] = []
    rss_samples: list[dict[str, Any]] = []
    bridge_failures: list[dict[str, str]] = []
    stop_sampling = threading.Event()

    def sampler() -> None:
        while not stop_sampling.is_set():
            t = time.monotonic() - started
            rss = sample_rss_kb(proc.pid)
            if rss is not None:
                rss_samples.append({"t": round(t, 3), "rss_kb": rss})
            time.sleep(args.sample_interval)

    sample_thread = threading.Thread(target=sampler, daemon=True)
    sample_thread.start()
    bridge: BridgeClient | None = None
    timed_out = False
    exception_text = ""

    try:
        wait_for_socket(socket_path, args.timeout)
        bridge = BridgeClient(socket_path, timeout=args.timeout)
        for step in steps:
            if time.monotonic() - started > args.timeout:
                timed_out = True
                break
            if step.kind == "sleep":
                time.sleep(float(step.args[0]))
                continue
            verb, payload = step.args
            ok, out = bridge.request(verb, payload)
            if verb == "state" and ok:
                states.append({
                    "t": round(time.monotonic() - started, 3),
                    "raw": out,
                    "state": parse_state_payload(out),
                })
            if not ok:
                bridge_failures.append({"command": step.raw, "response": out})
        try:
            proc.wait(timeout=15.0)
        except subprocess.TimeoutExpired:
            timed_out = True
    except Exception as exc:
        exception_text = str(exc)
    finally:
        stop_sampling.set()
        sample_thread.join(timeout=2.0)
        if bridge is not None:
            bridge.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5.0)

    log_text = log_path.read_text(errors="replace") if log_path.exists() else ""
    log_copy = artifacts / "openxray.log"
    log_copy.write_text(log_text)

    duration_sec = round(time.monotonic() - started, 3)
    summary = {
        "dry_run": False,
        "scenario": str(args.scenario),
        "game_dir": str(game_dir),
        "binary": str(args.binary),
        "socket": str(socket_path),
        "command": command,
        "duration_sec": duration_sec,
        "process_returncode": proc.returncode,
        "timed_out": timed_out,
        "exception": exception_text,
        "states": states,
        "rss_samples": rss_samples,
        "rss_high_water_kb": max((s["rss_kb"] for s in rss_samples), default=None),
        "bridge_failures": bridge_failures,
        "longest_frame_stall_sec": longest_frame_stall_sec(states),
        "log_findings": scan_log_text(log_text),
        "log_copy": str(log_copy),
    }
    write_json(artifacts / "summary.json", summary)
    return summary
```

- [ ] **Step 5: Wire live CLI arguments**

Replace the `main` parser setup in `tools/gl_macos_soak.py` with:

```python
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="parse inputs and write a dry-run summary")
    parser.add_argument("--scenario", type=Path, required=True, help="scenario text file")
    parser.add_argument("--artifacts", type=Path, default=REPO_ROOT / "artifacts/gl_macos_soak")
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME_DIR)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET_REL)
    parser.add_argument("--sample-interval", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=900.0)
    args = parser.parse_args(argv)

    steps = parse_scenario_file(args.scenario)
    if args.dry_run:
        write_json(args.artifacts / "summary.json", {
            "dry_run": True,
            "scenario": str(args.scenario),
            "step_count": len(steps),
            "steps": [step.raw for step in steps],
        })
        print(f"dry-run ok: {len(steps)} steps")
        return 0

    summary = run_scenario(args, steps)
    print(f"summary: {args.artifacts / 'summary.json'}")
    if summary["timed_out"] or summary["exception"] or summary["bridge_failures"]:
        return 1
    return 0
```

- [ ] **Step 6: Run unit tests and dry-run**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_menu_smoke.txt --artifacts artifacts/gl_macos_soak/dry_menu
```

Expected: all unit tests PASS; dry-run exits 0.

- [ ] **Step 7: Commit**

```bash
git add tools/gl_macos_soak.py tools/tests/test_gl_macos_soak.py
git commit -m "feat: run GL bridge scenarios with sampling"
```

---

### Task 4: Validation, Report Text, And Repeat Runs

**Files:**
- Modify: `tools/gl_macos_soak.py`
- Modify: `tools/tests/test_gl_macos_soak.py`

**Interfaces:**
- Consumes: `summary` dict from `run_scenario`.
- Produces: `evaluate_summary(summary: dict[str, Any]) -> tuple[bool, list[str]]`
- Produces: `write_report(path: Path, summary: dict[str, Any], passed: bool, failures: list[str]) -> None`
- Produces: CLI `--repeat N`
- Produces: `report.txt` beside `summary.json`.

- [ ] **Step 1: Write failing validation tests**

Append to `tools/tests/test_gl_macos_soak.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak.SummaryValidationTests -v
```

Expected: FAIL with missing `evaluate_summary`.

- [ ] **Step 3: Implement summary evaluation and report writer**

Add after `run_scenario`:

```python
def evaluate_summary(summary: dict[str, Any]) -> tuple[bool, list[str]]:
    failures: list[str] = []
    if summary.get("timed_out"):
        failures.append("scenario timed out")
    if summary.get("exception"):
        failures.append(f"exception: {summary['exception']}")
    for failure in summary.get("bridge_failures", []):
        failures.append(f"bridge command failed: {failure['command']} -> {failure['response']}")
    if summary.get("process_returncode") != 0:
        failures.append(f"process did not exit cleanly: {summary.get('process_returncode')}")
    for finding in summary.get("log_findings", []):
        failures.append(f"log finding: {finding}")
    if not summary.get("states"):
        failures.append("no state samples recorded")
    if summary.get("rss_high_water_kb") is None:
        failures.append("no RSS samples recorded")
    stall = float(summary.get("longest_frame_stall_sec") or 0.0)
    if stall > 10.0:
        failures.append(f"frame stall exceeded 10 seconds: {stall}")
    return not failures, failures


def write_report(path: Path, summary: dict[str, Any], passed: bool, failures: list[str]) -> None:
    lines = [
        f"GL macOS soak: {'PASS' if passed else 'FAIL'}",
        f"scenario: {summary.get('scenario')}",
        f"duration_sec: {summary.get('duration_sec')}",
        f"process_returncode: {summary.get('process_returncode')}",
        f"rss_high_water_kb: {summary.get('rss_high_water_kb')}",
        f"longest_frame_stall_sec: {summary.get('longest_frame_stall_sec')}",
        f"log_copy: {summary.get('log_copy')}",
        "",
        "failures:",
    ]
    if failures:
        lines.extend(f"- {failure}" for failure in failures)
    else:
        lines.append("- none")
    path.write_text("\n".join(lines) + "\n")
```

- [ ] **Step 4: Add repeat support**

Change `main` parser to add:

```python
    parser.add_argument("--repeat", type=int, default=1)
```

Replace the live execution block in `main` with:

```python
    all_passed = True
    run_summaries: list[dict[str, Any]] = []
    for index in range(args.repeat):
        run_artifacts = args.artifacts if args.repeat == 1 else args.artifacts / f"run_{index + 1:02d}"
        run_args = argparse.Namespace(**{**vars(args), "artifacts": run_artifacts})
        summary = run_scenario(run_args, steps)
        passed, failures = evaluate_summary(summary)
        write_report(run_artifacts / "report.txt", summary, passed, failures)
        summary["passed"] = passed
        summary["failures"] = failures
        write_json(run_artifacts / "summary.json", summary)
        run_summaries.append({
            "artifacts": str(run_artifacts),
            "passed": passed,
            "failures": failures,
            "rss_high_water_kb": summary.get("rss_high_water_kb"),
            "longest_frame_stall_sec": summary.get("longest_frame_stall_sec"),
        })
        print(f"run {index + 1}/{args.repeat}: {'PASS' if passed else 'FAIL'} -> {run_artifacts / 'report.txt'}")
        all_passed = all_passed and passed

    if args.repeat > 1:
        write_json(args.artifacts / "summary.json", {
            "repeat": args.repeat,
            "passed": all_passed,
            "runs": run_summaries,
        })
    return 0 if all_passed else 1
```

- [ ] **Step 5: Run tests and dry-run**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
python3 tools/gl_macos_soak.py --dry-run --scenario tools/gl_load_play_save_load.txt --artifacts artifacts/gl_macos_soak/dry_load
```

Expected: tests PASS; dry-run exits 0.

- [ ] **Step 6: Commit**

```bash
git add tools/gl_macos_soak.py tools/tests/test_gl_macos_soak.py
git commit -m "feat: validate GL soak runs"
```

---

### Task 5: Screenshot Artifact Checks

**Files:**
- Modify: `tools/gl_macos_soak.py`
- Modify: `tools/tests/test_gl_macos_soak.py`

**Interfaces:**
- Consumes: scenario steps and game directory.
- Produces: `expected_screenshots(steps: list[ScenarioStep]) -> list[str]`
- Produces: `collect_screenshot_status(game_dir: Path, names: list[str]) -> list[dict[str, Any]]`
- Adds `screenshots` field to summary.

- [ ] **Step 1: Write failing screenshot tests**

Append to `tools/tests/test_gl_macos_soak.py`:

```python
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

            self.assertEqual([
                {"name": "exists", "path": str(screenshot_dir / "exists.jpg"), "exists": True, "size": 3},
                {"name": "missing", "path": str(screenshot_dir / "missing.jpg"), "exists": False, "size": 0},
            ], status)
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak.ScreenshotTests -v
```

Expected: FAIL with missing `expected_screenshots`.

- [ ] **Step 3: Implement screenshot helpers**

Add after `longest_frame_stall_sec`:

```python
def expected_screenshots(steps: list[ScenarioStep]) -> list[str]:
    names: list[str] = []
    for step in steps:
        if step.kind != "bridge":
            continue
        verb, payload = step.args
        if verb == "shot" and payload:
            names.append(payload.split()[0])
    return names


def collect_screenshot_status(game_dir: Path, names: list[str]) -> list[dict[str, Any]]:
    screenshot_dir = game_dir / "appdata" / "screenshots"
    status: list[dict[str, Any]] = []
    for name in names:
        path = screenshot_dir / f"{name}.jpg"
        exists = path.exists()
        status.append({
            "name": name,
            "path": str(path),
            "exists": exists,
            "size": path.stat().st_size if exists else 0,
        })
    return status
```

In `run_scenario`, before `summary = {`, add:

```python
    screenshots = collect_screenshot_status(game_dir, expected_screenshots(steps))
```

Add to the `summary` dict:

```python
        "screenshots": screenshots,
```

In `evaluate_summary`, add after log findings:

```python
    for shot in summary.get("screenshots", []):
        if not shot.get("exists") or int(shot.get("size") or 0) <= 0:
            failures.append(f"screenshot missing or empty: {shot.get('name')}")
```

- [ ] **Step 4: Run tests**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/gl_macos_soak.py tools/tests/test_gl_macos_soak.py
git commit -m "feat: validate GL soak screenshots"
```

---

### Task 6: Documentation And GL Gate Command

**Files:**
- Modify: `docs/macos-dev-setup.md`
- Modify: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: `tools/gl_macos_soak.py` CLI from previous tasks.
- Produces: documented menu smoke, load/play/save/load, repeat, and idle commands.

- [ ] **Step 1: Inspect current docs anchor points**

Run:

```bash
rg -n "agent_bridge|bridge_soak|Run & test|Testing" docs/macos-dev-setup.md docs/HANDOVER.md
```

Expected: output includes existing bridge sections in both files.

- [ ] **Step 2: Add GL baseline section to `docs/macos-dev-setup.md`**

Insert this text after the existing agent bridge usage section:

```markdown
### GL macOS stability gate

The AI phase uses OpenGL (`renderer_r3`) as the stable host runtime. Use the GL soak wrapper to launch the game, drive the bridge, collect logs/screenshots, sample RSS, and write artifacts:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_menu_smoke.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/menu

python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/load_play_save_load

python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --repeat 5 \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5
```

Artifacts include `summary.json`, `report.txt`, a copied engine log, bridge state samples, RSS samples, screenshot status, and pass/fail reasons.
```
```

- [ ] **Step 3: Add GL baseline note to `docs/HANDOVER.md`**

Insert this text in the run/test section after the `bridge_soak.txt` paragraph:

```markdown
### 3.5 GL stability gate

Before AI work, keep `renderer_r3` green with the GL macOS soak wrapper:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --repeat 5 \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5
```

This launches the game from the CoC run directory, drives the bridge, samples FPS/frame progress and RSS, validates screenshots/logs/exit, and writes `summary.json` plus `report.txt`.
```
```

- [ ] **Step 4: Verify docs mention the command**

Run:

```bash
rg -n "gl_macos_soak|GL stability gate|renderer_r3" docs/macos-dev-setup.md docs/HANDOVER.md
```

Expected: output includes the new commands in both docs.

- [ ] **Step 5: Commit**

```bash
git add docs/macos-dev-setup.md docs/HANDOVER.md
git commit -m "docs: document GL macOS soak gate"
```

---

### Task 7: First Local GL Runs And Baseline Report

**Files:**
- No source edits required unless the run exposes a harness bug.
- Generated artifacts: `artifacts/gl_macos_soak/menu/`, `artifacts/gl_macos_soak/load_play_save_load/`, `artifacts/gl_macos_soak/repeat_5/`.

**Interfaces:**
- Consumes: finished harness and docs.
- Produces: measured baseline artifacts for rz's 24 GB Mac.

- [ ] **Step 1: Confirm build binary exists**

Run:

```bash
test -x /Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da && echo "binary ok"
```

Expected: prints `binary ok`. If it does not, build:

```bash
cmake --build /Users/rz/OpenXVibeRay/build -j10
```

Expected: build exits 0 and produces `/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da`.

- [ ] **Step 2: Confirm renderer setting is GL**

Run:

```bash
rg -n "^renderer " "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl/appdata/user.ltx"
```

Expected: line contains `renderer renderer_r3`. If it does not, stop the game and edit `user.ltx` to set:

```text
renderer renderer_r3
```

- [ ] **Step 3: Run menu smoke**

Run:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_menu_smoke.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/menu
```

Expected: exits 0 and prints `run 1/1: PASS`.

- [ ] **Step 4: Inspect menu report**

Run:

```bash
sed -n '1,120p' /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/menu/report.txt
```

Expected: first line is `GL macOS soak: PASS`; failures section says `- none`.

- [ ] **Step 5: Run load/play/save/load gate**

Run:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/load_play_save_load
```

Expected: exits 0 and prints `run 1/1: PASS`.

- [ ] **Step 6: Run repeat gate**

Run:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --repeat 5 \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5
```

Expected: exits 0; each run prints `PASS`; aggregate `summary.json` has `"passed": true`.

- [ ] **Step 7: Commit harness bug fixes only if needed**

If Tasks 7.3-7.6 expose a harness bug, make the smallest fix, rerun the failing command, and commit with:

```bash
git add tools/gl_macos_soak.py tools/tests/test_gl_macos_soak.py
git commit -m "fix: stabilize GL soak harness"
```

If no harness bug is exposed, do not create a commit for generated artifacts.

---

### Task 8: Failure Evidence Package And Follow-Up Gate

**Files:**
- No source edits in this task.
- Generated artifacts remain under `artifacts/gl_macos_soak/`.

**Interfaces:**
- Consumes: `report.txt`, `summary.json`, copied `openxray.log`, and screenshots from Task 7.
- Produces: a precise failure classification and focused repro artifact folder.
- Produces: a decision: green baseline complete, or write a new bug-specific implementation plan for the first failure.

- [ ] **Step 1: Classify the first failing report**

Run:

```bash
for report in /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5/run_*/report.txt; do
  echo "== $report =="
  sed -n '1,80p' "$report"
done
```

Expected for a green run: every report says `GL macOS soak: PASS`. If any report fails, classify the first failure as exactly one of:

```text
crash_or_hang
bridge_unresponsive
save_load_failure
quit_failure
gl_log_error
lua_runtime_error
screenshot_failure
frame_stall
memory_growth
```

- [ ] **Step 2: Create a focused repro artifact**

For `crash_or_hang`, `bridge_unresponsive`, `save_load_failure`, `quit_failure`, `gl_log_error`, `lua_runtime_error`, `screenshot_failure`, or `frame_stall`, rerun the single failing scenario:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_first_failure
```

For `memory_growth`, run idle soak:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_idle_soak.txt \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_memory
```

Expected: the selected command reproduces the same failure class in `report.txt`.

- [ ] **Step 3: Inspect logs and status**

Run:

```bash
sed -n '1,120p' /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_first_failure/report.txt 2>/dev/null || true
sed -n '1,120p' /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_memory/report.txt 2>/dev/null || true
rg -n "FATAL|fatal|GL_INVALID|OpenGL error|lua runtime error|stack traceback|agent_bridge" \
  /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_first_failure/openxray.log \
  /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_memory/openxray.log 2>/dev/null || true
```

Expected: output identifies the first actionable log line or confirms the failure is timeout/frame/memory based.

- [ ] **Step 4: List the evidence paths**

Run:

```bash
ls -la /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_first_failure 2>/dev/null || true
ls -la /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repro_memory 2>/dev/null || true
```

Expected: the artifact folder for the reproduced failure contains `summary.json`, `report.txt`, and `openxray.log`.

- [ ] **Step 5: Stop for a targeted fix plan if the gate is red**

If the repeat gate is green, the GL baseline harness implementation is complete.

If the repeat gate is red, do not patch engine code from this generic plan. Write a new bug-specific plan named with the failure class, for example `docs/superpowers/plans/2026-07-07-gl-quit-failure.md`, and include the exact failing report path, log line, suspected source files, failing command, expected fixed command, and verification command.

Expected: there is either a green `artifacts/gl_macos_soak/repeat_5/summary.json` with `"passed": true`, or a new failure-specific plan ready for implementation.
