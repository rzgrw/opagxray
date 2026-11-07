#!/usr/bin/env python3
"""GL macOS stability soak harness for OpenXVibeRay.

This tool wraps the existing agent bridge. It intentionally starts with
coarse, reliable signals: bridge state, process RSS, screenshots, logs, and
process exit.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable


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
        if verb == "wait_frames":
            count = int(payload.strip())
            if count <= 0:
                raise ValueError(f"line {line_no}: wait_frames requires a positive frame count")
            steps.append(ScenarioStep("wait_frames", (str(count),), line))
            continue
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


def wait_for_frames(read_state: Callable[[float], dict[str, str]], count: int,
                    timeout: float) -> list[dict[str, str]]:
    """Wait for count frame advances; each read gets the remaining time budget."""
    deadline = time.monotonic() + timeout
    target = None
    observed = []
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"wait_frames timed out waiting for {count} frame advances")
        state = read_state(remaining)
        try:
            frame = int(state["frame"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError("wait_frames requires a numeric frame in bridge state") from exc
        observed.append(state)
        if target is None:
            target = frame + count
        elif frame >= target:
            return observed
        time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))


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


def expected_screenshots(steps: list[ScenarioStep]) -> list[str]:
    names: list[str] = []
    for step in steps:
        if step.kind != "bridge":
            continue
        verb, payload = step.args
        if verb == "shot" and payload:
            names.append(payload.split()[0])
    return names


def remove_expected_screenshots(game_dir: Path, names: list[str]) -> None:
    screenshot_dir = game_dir / "appdata" / "screenshots"
    for name in names:
        path = screenshot_dir / f"{name}.jpg"
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def collect_screenshot_status(
    game_dir: Path,
    names: list[str],
    not_before: float | None = None,
) -> list[dict[str, Any]]:
    screenshot_dir = game_dir / "appdata" / "screenshots"
    status: list[dict[str, Any]] = []
    for name in names:
        path = screenshot_dir / f"{name}.jpg"
        exists = path.exists()
        mtime = path.stat().st_mtime if exists else None
        entry: dict[str, Any] = {
            "name": name,
            "path": str(path),
            "exists": exists,
            "size": path.stat().st_size if exists else 0,
        }
        if mtime is not None:
            entry["mtime"] = mtime
        if not_before is not None:
            entry["fresh"] = exists and mtime is not None and mtime >= not_before - 1.0
        status.append(entry)
    return status


def resolve_socket_path(game_dir: Path, socket_arg: Path) -> Path:
    return socket_arg if socket_arg.is_absolute() else game_dir / socket_arg


def resolve_binary_path(binary_arg: Path) -> Path:
    return binary_arg if binary_arg.is_absolute() else (Path.cwd() / binary_arg).resolve()


def build_launch_command(binary: Path, socket_arg: Path) -> list[str]:
    return [str(binary), "-agent_bridge", str(socket_arg)]


def is_quit_command(verb: str, payload: str) -> bool:
    return verb == "cmd" and payload.strip() == "quit"


def sleep_process_aware(seconds: float, process: subprocess.Popen[Any], poll_interval: float = 0.25) -> bool:
    end = time.monotonic() + seconds
    while True:
        if process.poll() is not None:
            return False
        remaining = end - time.monotonic()
        if remaining <= 0.0:
            return True
        time.sleep(min(max(poll_interval, 0.01), remaining))


def bounded_sleep_seconds(requested_seconds: float, remaining_seconds: float) -> tuple[float, bool]:
    if remaining_seconds <= 0.0:
        return 0.0, True
    if requested_seconds > remaining_seconds:
        return remaining_seconds, True
    return requested_seconds, False


def process_wait_timeout_seconds(remaining_seconds: float) -> tuple[float, bool]:
    if remaining_seconds <= 0.0:
        return 0.0, True
    return min(15.0, remaining_seconds), False


def wait_for_socket(path: Path, timeout: float, process: subprocess.Popen[Any] | None = None) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"process exited before socket appeared: {process.returncode}")
        time.sleep(0.1)
    raise TimeoutError(f"socket did not appear: {path}")


def run_scenario(args: argparse.Namespace, steps: list[ScenarioStep]) -> dict[str, Any]:
    game_dir = args.game_dir
    binary_path = resolve_binary_path(args.binary)
    socket_path = resolve_socket_path(game_dir, args.socket)
    log_path = game_dir / DEFAULT_LOG_REL
    artifacts = args.artifacts
    artifacts.mkdir(parents=True, exist_ok=True)

    if log_path.exists():
        log_path.write_text("")
    if socket_path.exists():
        socket_path.unlink()

    expected_shots = expected_screenshots(steps)
    remove_expected_screenshots(game_dir, expected_shots)

    command = build_launch_command(binary_path, args.socket)
    started_wall = time.time()
    started = time.monotonic()
    deadline = started + args.timeout
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
    events: list[dict[str, Any]] = []
    stop_sampling = threading.Event()

    def record_event(step: ScenarioStep, status: str, **extra: Any) -> None:
        event: dict[str, Any] = {
            "t": round(time.monotonic() - started, 3),
            "status": status,
            "step": step.raw,
        }
        event.update(extra)
        events.append(event)

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
        wait_for_socket(socket_path, args.timeout, proc)
        bridge = BridgeClient(socket_path, timeout=args.timeout)
        for step in steps:
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                timed_out = True
                break
            record_event(step, "start")
            if step.kind == "sleep":
                sleep_seconds, clipped = bounded_sleep_seconds(float(step.args[0]), remaining)
                if sleep_seconds > 0.0:
                    if not sleep_process_aware(sleep_seconds, proc):
                        record_event(step, "process_exited_during_sleep", returncode=proc.returncode)
                        break
                if clipped:
                    timed_out = True
                    record_event(step, "clipped_by_timeout")
                    break
                record_event(step, "done")
                continue
            if proc.poll() is not None:
                record_event(step, "process_exited_before_step", returncode=proc.returncode)
                break
            if step.kind == "wait_frames":
                def read_frame_state(budget: float) -> dict[str, str]:
                    if proc.poll() is not None:
                        raise RuntimeError("process exited during wait_frames")
                    bridge.sock.settimeout(budget)
                    ok, out = bridge.request("state")
                    if not ok:
                        raise RuntimeError(f"wait_frames state failed: {out}")
                    state = parse_state_payload(out)
                    states.append({"t": round(time.monotonic() - started, 3),
                                   "raw": out, "state": state})
                    return state

                try:
                    observations = wait_for_frames(read_frame_state, int(step.args[0]), remaining)
                finally:
                    bridge.sock.settimeout(args.timeout)
                record_event(step, "done", first_frame=observations[0]["frame"],
                             last_frame=observations[-1]["frame"])
                continue
            verb, payload = step.args
            try:
                ok, out = bridge.request(verb, payload)
            except ConnectionError:
                if is_quit_command(verb, payload):
                    record_event(step, "closed_on_quit")
                    break
                record_event(step, "connection_error")
                raise
            record_event(step, "response", ok=ok, response=out)
            if verb == "state" and ok:
                states.append({
                    "t": round(time.monotonic() - started, 3),
                    "raw": out,
                    "state": parse_state_payload(out),
                })
            if not ok:
                bridge_failures.append({"command": step.raw, "response": out})
        wait_timeout, already_timed_out = process_wait_timeout_seconds(deadline - time.monotonic())
        if already_timed_out:
            timed_out = True
        else:
            try:
                proc.wait(timeout=wait_timeout)
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
    screenshots = collect_screenshot_status(game_dir, expected_shots, not_before=started_wall)
    summary = {
        "dry_run": False,
        "scenario": str(args.scenario),
        "game_dir": str(game_dir),
        "binary": str(binary_path),
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
        "events": events,
        "longest_frame_stall_sec": longest_frame_stall_sec(states),
        "log_findings": scan_log_text(log_text),
        "log_copy": str(log_copy),
        "screenshots": screenshots,
    }
    write_json(artifacts / "summary.json", summary)
    return summary


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
    for shot in summary.get("screenshots", []):
        if not shot.get("exists") or int(shot.get("size") or 0) <= 0:
            failures.append(f"screenshot missing or empty: {shot.get('name')}")
        elif shot.get("fresh") is False:
            failures.append(f"screenshot stale: {shot.get('name')}")
    if not summary.get("states"):
        failures.append("no state samples recorded")
    if summary.get("rss_high_water_kb") is None:
        failures.append("no RSS samples recorded")
    stall = float(summary.get("longest_frame_stall_sec") or 0.0)
    if stall > 10.0:
        failures.append(f"frame stall exceeded 10 seconds: {stall}")
    return not failures, failures


def write_report(path: Path, summary: dict[str, Any], passed: bool, failures: list[str]) -> None:
    events = summary.get("events") or []
    last_event = json.dumps(events[-1], sort_keys=True) if events else "none"
    lines = [
        f"GL macOS soak: {'PASS' if passed else 'FAIL'}",
        f"scenario: {summary.get('scenario')}",
        f"duration_sec: {summary.get('duration_sec')}",
        f"process_returncode: {summary.get('process_returncode')}",
        f"rss_high_water_kb: {summary.get('rss_high_water_kb')}",
        f"longest_frame_stall_sec: {summary.get('longest_frame_stall_sec')}",
        f"last_event: {last_event}",
        f"log_copy: {summary.get('log_copy')}",
        "",
        "failures:",
    ]
    if failures:
        lines.extend(f"- {failure}" for failure in failures)
    else:
        lines.append("- none")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


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
    parser.add_argument("--repeat", type=int, default=1)
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


if __name__ == "__main__":
    raise SystemExit(main())
