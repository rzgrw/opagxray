#!/usr/bin/env python3
"""Validate and summarize xray-frame-time-v1 captures using only the stdlib."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

SCHEMA_MARKER = "# xray-frame-time-v1"
MAX_CAPACITY = 131072
MAX_WARMUP = 1000000
MAX_U32 = 2**32 - 1
MAX_U64 = 2**64 - 1

PHASES = (
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
    "loop_seq",
    "start_ns",
    "wall_ns",
    "engine_frame_begin",
    "engine_frame_end",
    "flags",
)
FIELDS = BASE_FIELDS + tuple(
    field for phase in PHASES for field in (f"{phase}_calls", f"{phase}_ns")
)
REQUIRED_METADATA = {
    "state",
    "capacity",
    "warmup_loops",
    "warmup_observed",
    "stored_loops",
    "dropped_loops",
    "invalid_loops",
    "truncated",
    "capture_start_ns",
    "capture_end_ns",
    "clock_is_steady",
    "clock_period_ns",
    "source_build",
    "renderer",
    "content_manifest",
    "overlay",
    "render_resolution",
    "drawable_resolution",
    "vsync",
    "frame_limit",
    "sdl_version",
    "allocator_build",
}
PROVENANCE_FIELDS = (
    "source_build", "renderer", "content_manifest", "overlay",
    "render_resolution", "drawable_resolution", "vsync", "frame_limit",
    "sdl_version", "allocator_build",
)
LONG_FRAME_THRESHOLDS = (16_700_000, 33_300_000, 50_000_000, 100_000_000)

FLAG_LOADING = 1 << 0
FLAG_MENU = 1 << 1
FLAG_PAUSED = 1 << 2
FLAG_PRECACHE = 1 << 3
FLAG_INACTIVE = 1 << 4
FLAG_DEVICE_UNAVAILABLE = 1 << 5
FLAG_WINDOW_VISIBILITY_UNKNOWN = 1 << 6
KNOWN_FLAGS = (
    FLAG_LOADING | FLAG_MENU | FLAG_PAUSED | FLAG_PRECACHE |
    FLAG_INACTIVE | FLAG_DEVICE_UNAVAILABLE | FLAG_WINDOW_VISIBILITY_UNKNOWN
)
NONSTEADY_FLAGS = KNOWN_FLAGS
SCENE_FLAGS = {
    "loading": FLAG_LOADING,
    "menu": FLAG_MENU,
    "paused": FLAG_PAUSED,
    "precache": FLAG_PRECACHE,
    "inactive": FLAG_INACTIVE,
    "device_unavailable": FLAG_DEVICE_UNAVAILABLE,
    "window_visibility_unknown": FLAG_WINDOW_VISIBILITY_UNKNOWN,
}

# Not-ready is an executed phase, not evidence that the render device was lost.
SCENE_PHASES = {"not_ready": "not_ready_sleep"}


class CaptureError(ValueError):
    pass


@dataclass(frozen=True)
class Capture:
    metadata: dict[str, str]
    rows: tuple[dict[str, str | int], ...]


def _integer(value: str, field: str, maximum: int = MAX_U64) -> int:
    if not isinstance(value, str) or not value or not value.isascii() or not value.isdigit():
        raise CaptureError(f"{field}: expected a nonnegative decimal integer")
    digits = value.lstrip("0") or "0"
    if len(digits) > len(str(maximum)):
        raise CaptureError(f"{field}: overflow")
    result = int(digits)
    if result > maximum:
        raise CaptureError(f"{field}: overflow")
    return result


def _boolean(value: str, field: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise CaptureError(f"{field}: expected true or false")


def _metadata_int(metadata: dict[str, str], key: str, maximum: int = MAX_U64) -> int:
    return _integer(metadata[key], f"metadata {key}", maximum)


def _unknown(value: str) -> bool:
    return value.strip().lower() in {"unknown", "unavailable"}


def _validate_presentation_metadata(metadata: dict[str, str]) -> None:
    for field in ("render_resolution", "drawable_resolution"):
        value = metadata[field]
        if _unknown(value):
            continue
        match = re.fullmatch(r"([1-9][0-9]*)x([1-9][0-9]*)", value)
        if not match:
            raise CaptureError(f"{field}: expected positive <width>x<height> or unknown")
        for part in match.groups():
            _integer(part, field, MAX_U32)
    vsync = metadata["vsync"]
    if not _unknown(vsync) and vsync not in {"on", "off", "adaptive"}:
        raise CaptureError("vsync: expected on, off, adaptive, or unknown")
    frame_limit = metadata["frame_limit"]
    if not _unknown(frame_limit):
        _integer(frame_limit, "frame_limit", MAX_U32)


def _csv_rows(lines: Iterable[str]) -> Iterable[list[str]]:
    try:
        yield from csv.reader(lines, strict=True)
    except csv.Error as error:
        raise CaptureError(f"malformed CSV: {error}") from error


def parse_capture(text: str) -> Capture:
    # Preserve newlines inside quoted CSV fields: removing them could turn an
    # invalid multiline integer into an apparently valid duration.
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].rstrip("\r\n") != SCHEMA_MARKER:
        raise CaptureError("expected xray-frame-time-v1 schema marker")

    metadata: dict[str, str] = {}
    index = 1
    while index < len(lines) and lines[index].startswith("#"):
        line = lines[index].rstrip("\r\n")
        if not line.startswith("# ") or "=" not in line[2:]:
            raise CaptureError(f"line {index + 1}: invalid metadata")
        key, value = line[2:].split("=", 1)
        if not key or key not in REQUIRED_METADATA:
            raise CaptureError(f"line {index + 1}: unknown metadata field {key!r}")
        if key in metadata:
            raise CaptureError(f"line {index + 1}: duplicate metadata field {key!r}")
        if not value or not value.strip():
            raise CaptureError(f"line {index + 1}: empty metadata field {key!r}")
        metadata[key] = value
        index += 1

    missing = REQUIRED_METADATA - metadata.keys()
    if missing:
        raise CaptureError(f"missing metadata: {', '.join(sorted(missing))}")
    if index >= len(lines):
        raise CaptureError("missing CSV header")

    header = next(iter(_csv_rows([lines[index]])))
    if len(header) != len(FIELDS) or tuple(header) != FIELDS:
        raise CaptureError("CSV header does not match xray-frame-time-v1")
    if len(set(header)) != len(header):
        raise CaptureError("duplicate CSV fields")
    index += 1

    rows: list[dict[str, str | int]] = []
    previous_loop = -1
    for row_index, values in enumerate(_csv_rows(lines[index:]), start=index + 1):
        if not values or all(value == "" for value in values):
            raise CaptureError(f"line {row_index}: empty record")
        if len(values) != len(FIELDS):
            raise CaptureError(f"line {row_index}: wrong record field count")
        row = dict(zip(FIELDS, values))
        parsed: dict[str, str | int] = {"flags": _integer(row["flags"], "flags")}
        if int(parsed["flags"]) & ~KNOWN_FLAGS:
            raise CaptureError(f"line {row_index}: unknown classification flags")

        for field in BASE_FIELDS[:3]:
            parsed[field] = _integer(row[field], field)
        for field in ("engine_frame_begin", "engine_frame_end"):
            parsed[field] = _integer(row[field], field, MAX_U32)
        if int(parsed["loop_seq"]) <= previous_loop:
            raise CaptureError(f"line {row_index}: duplicate or decreasing loop_seq")
        previous_loop = int(parsed["loop_seq"])
        start = int(parsed["start_ns"])
        wall = int(parsed["wall_ns"])
        if start > MAX_U64 - wall:
            raise CaptureError(f"line {row_index}: start_ns + wall_ns overflow")
        parsed["end_ns"] = start + wall
        if int(parsed["engine_frame_end"]) < int(parsed["engine_frame_begin"]):
            raise CaptureError(f"line {row_index}: engine frame range is reversed")

        for phase in PHASES:
            calls_field = f"{phase}_calls"
            ns_field = f"{phase}_ns"
            calls = _integer(row[calls_field], calls_field, MAX_U32)
            parsed[calls_field] = calls
            if calls == 0:
                if row[ns_field] != "":
                    raise CaptureError(f"line {row_index}: {ns_field} must be empty with zero calls")
                parsed[ns_field] = ""
                continue
            duration = _integer(row[ns_field], ns_field)
            if duration > calls * wall:
                raise CaptureError(f"line {row_index}: {ns_field} exceeds the calls*wall bound")
            parsed[ns_field] = duration
        rows.append(parsed)

    _validate_metadata(metadata, rows)
    return Capture(metadata, tuple(rows))


def _validate_metadata(metadata: dict[str, str], rows: list[dict[str, str | int]]) -> None:
    if metadata["state"] != "stopped":
        raise CaptureError("capture must be stopped before offline analysis")
    if not _boolean(metadata["clock_is_steady"], "clock_is_steady"):
        raise CaptureError("capture clock is not steady")
    if _metadata_int(metadata, "clock_period_ns") == 0:
        raise CaptureError("clock_period_ns must be positive")
    _validate_presentation_metadata(metadata)

    capacity = _metadata_int(metadata, "capacity")
    if capacity == 0 or capacity > MAX_CAPACITY:
        raise CaptureError("capacity outside collector bounds")
    warmup = _metadata_int(metadata, "warmup_loops")
    warmup_observed = _metadata_int(metadata, "warmup_observed")
    if warmup > MAX_WARMUP or warmup_observed > warmup:
        raise CaptureError("warmup counts outside collector bounds")
    stored = _metadata_int(metadata, "stored_loops")
    dropped = _metadata_int(metadata, "dropped_loops")
    invalid = _metadata_int(metadata, "invalid_loops")
    if stored != len(rows) or stored > capacity:
        raise CaptureError("stored_loops does not match record count/capacity")
    if rows and warmup_observed != warmup:
        raise CaptureError("stored samples require completed warmup")

    truncated = _boolean(metadata["truncated"], "truncated")
    if dropped and not truncated:
        raise CaptureError("dropped loops require truncated=true")
    if stored == capacity and not truncated:
        raise CaptureError("full capacity requires truncated=true even without drops")
    if dropped and stored != capacity:
        raise CaptureError("dropped loops require full retained capacity")
    # Besides capacity saturation, sequence exhaustion can stop the core early.
    # That path always records an invalid loop, even without any retained rows.
    if truncated and stored < capacity and not invalid:
        raise CaptureError("truncation below capacity requires invalid-loop accounting")
    if warmup_observed + stored + dropped + invalid > MAX_U64:
        raise CaptureError("loop accounting overflow")

    capture_start = _metadata_int(metadata, "capture_start_ns")
    capture_end = _metadata_int(metadata, "capture_end_ns")
    if not rows:
        if capture_start != 0 or capture_end != 0:
            raise CaptureError("empty capture must use zero capture timestamps")
        return
    if capture_start != int(rows[0]["start_ns"]):
        raise CaptureError("capture_start_ns must equal the first retained loop begin")
    if capture_end != int(rows[-1]["end_ns"]):
        raise CaptureError("capture_end_ns must equal the last retained loop end")

    first_loop = int(rows[0]["loop_seq"])
    if first_loop < warmup:
        raise CaptureError("first retained loop precedes completed warmup")
    # IDs start at zero; valid warmup loops and invalid loops are the only
    # possible omissions before the first retained row. Invalid accounting may
    # also include later failures (including the excluded stop boundary).
    sequence_holes = first_loop - warmup
    for previous, current in zip(rows, rows[1:]):
        previous_end = int(previous["end_ns"])
        current_start = int(current["start_ns"])
        if current_start < previous_end:
            raise CaptureError("retained loop intervals overlap or reverse time")
        sequence_gap = int(current["loop_seq"]) - int(previous["loop_seq"]) - 1
        time_gap = current_start - previous_end
        if sequence_gap == 0 and time_gap != 0:
            raise CaptureError("unexplained time gap between consecutive loop IDs")
        if sequence_gap > 0:
            sequence_holes += sequence_gap
    if sequence_holes:
        if invalid == 0:
            raise CaptureError("loop/time holes require invalid-loop accounting")
        if invalid < sequence_holes:
            raise CaptureError("invalid_loops is smaller than omitted loop IDs")


def nearest_rank(values: Iterable[int], percentile: float) -> int:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("nearest rank requires samples")
    if not math.isfinite(percentile) or percentile <= 0 or percentile > 1:
        raise ValueError("percentile must be in (0, 1]")
    return ordered[math.ceil(percentile * len(ordered)) - 1]


def _selected_rows(capture: Capture, scene: str) -> list[dict[str, str | int]]:
    if scene == "all":
        return list(capture.rows)
    if scene == "steady":
        return [row for row in capture.rows
                if int(row["flags"]) & NONSTEADY_FLAGS == 0 and int(row["not_ready_sleep_calls"]) == 0]
    if scene in SCENE_PHASES:
        return [row for row in capture.rows if int(row[f"{SCENE_PHASES[scene]}_calls"]) > 0]
    required = SCENE_FLAGS[scene]
    return [row for row in capture.rows if int(row["flags"]) & required]


def _population_report(rows: list[dict[str, str | int]]) -> dict:
    walls = [int(row["wall_ns"]) for row in rows]
    wall_report: dict[str, int] = {}
    if walls:
        wall_report = {
            "p50": nearest_rank(walls, 0.50),
            "p95": nearest_rank(walls, 0.95),
            "p99": nearest_rank(walls, 0.99),
            "maximum": max(walls),
        }

    flags = {
        name: sum(bool(int(row["flags"]) & bit) for row in rows)
        for name, bit in SCENE_FLAGS.items()
    }
    phases = {}
    for phase in PHASES:
        calls_field = f"{phase}_calls"
        ns_field = f"{phase}_ns"
        phases[phase] = {
            "loops": sum(int(row[calls_field]) > 0 for row in rows),
            "calls": sum(int(row[calls_field]) for row in rows),
            "duration_ns": sum(int(row[ns_field]) for row in rows if row[ns_field] != ""),
        }

    return {
        "sample_count": len(rows),
        "first_loop_seq": int(rows[0]["loop_seq"]) if rows else None,
        "last_loop_seq": int(rows[-1]["loop_seq"]) if rows else None,
        "first_start_ns": int(rows[0]["start_ns"]) if rows else None,
        "last_end_ns": int(rows[-1]["end_ns"]) if rows else None,
        "sampled_wall_ns": sum(walls),
        "elapsed_ns": int(rows[-1]["end_ns"]) - int(rows[0]["start_ns"]) if rows else 0,
        "wall_ns": wall_report,
        "long_frames": {
            str(threshold): sum(wall > threshold for wall in walls)
            for threshold in LONG_FRAME_THRESHOLDS
        },
        "flags": flags,
        "phases": phases,
    }


def build_report(capture: Capture, scene: str = "all") -> dict:
    if scene != "all" and scene != "steady" and scene not in SCENE_FLAGS and scene not in SCENE_PHASES:
        raise ValueError(f"unknown scene filter {scene!r}")
    populations = {
        name: _population_report(_selected_rows(capture, name))
        for name in ("all", "steady")
    }
    selected = populations[scene] if scene in populations else _population_report(_selected_rows(capture, scene))
    dropped = _metadata_int(capture.metadata, "dropped_loops")
    invalid = _metadata_int(capture.metadata, "invalid_loops")
    truncated = _boolean(capture.metadata["truncated"], "truncated")
    reasons: list[str] = []
    if truncated:
        reasons.append("truncated")
    if dropped:
        reasons.append("dropped_loops")
    if invalid:
        reasons.append("invalid_loops")
    unknown_fields = [field for field in PROVENANCE_FIELDS if _unknown(capture.metadata[field])]
    reasons.extend(f"unknown_provenance:{field}" for field in unknown_fields)
    if not capture.rows:
        reasons.append("empty_capture")
    elif not selected["sample_count"]:
        reasons.append("empty_selection")

    if truncated or dropped or invalid:
        status = "partial"
    elif not capture.rows:
        status = "empty_capture"
    elif not selected["sample_count"]:
        status = "empty_selection"
    elif unknown_fields:
        status = "unprovenanced"
    else:
        status = "complete"

    return {
        "schema": "xray-frame-time-report-v1",
        "status": status,
        "acceptance": not reasons,
        "reasons": reasons,
        "scene_filter": scene,
        **selected,
        "populations": populations,
        "stored_loops": len(capture.rows),
        "dropped_loops": dropped,
        "invalid_loops": invalid,
        "capture_start_ns": _metadata_int(capture.metadata, "capture_start_ns"),
        "capture_end_ns": _metadata_int(capture.metadata, "capture_end_ns"),
        "provenance": {field: capture.metadata[field] for field in PROVENANCE_FIELDS},
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--scene", choices=("all", "steady", *SCENE_FLAGS, *SCENE_PHASES), default="all")
    parser.add_argument("--acceptance", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = build_report(parse_capture(args.capture.read_text()), scene=args.scene)
    except (OSError, CaptureError, ValueError) as error:
        print(f"frame-time report: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["acceptance"] or not args.acceptance else 1


if __name__ == "__main__":
    raise SystemExit(main())
