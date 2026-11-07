# Frame-time overhead reference — design

**Date:** 2026-11-07
**Status:** Design only. Corrects the earlier four-arm draft. Not implemented. Does not close the overhead gate.
**Checkpoint:** `a389540d83ea962f34c22dd7b0f422a514895b4d`
**Baseline:** `e3f273f04bf85b4b265004cbbc8a35f19796cd31`
**Owns:** this file only. No production patch, build, or runtime.

## Decision

Three experimental builds each carry the same probe. There is no bare arm. A build with no clock and no store cannot emit the per-loop CSV this gate compares, so a bare median or an absolute probe-cost gate is not implementable.

| Role | Source | Probe | Collector |
|---|---|---|---|
| `base` | `e3f273f04` | yes | absent |
| `off` | `a389540d8` | yes | compiled, never started |
| `on` | `a389540d8` | yes | started and stopped at the excluded control boundaries below |

The reported quantity is conditional wall overhead under that common probe:

- `off − base`: disabled instrumentation.
- `on − off`: capture-active instrumentation.

Thresholds stay those in the measurement plan: median `max(0.05 ms, 1% of base)` and p95 `max(0.10 ms, 3% of base)` for `off − base`; median `0.10 ms` and p95 `0.25 ms` for `on − off`. Apply them to the median of the predeclared run-medians and the median of the run-p95s. Also list every run. This does not prove the probe's perturbation is zero, and it does not prove the common cost cancels exactly. Matched arms only make that cost common.

Disabled collector code still must not read a clock unless the probe, which is outside it, is armed. Bridge FPS, soak poll gaps, Tracy `FrameMark`, sampling profilers, and donor autotest totals are not this boundary.

An optional standalone microbenchmark may time one probe call in a tight loop and report nanoseconds per call. That number is not a game p50 or p95 and is not subtracted from either delta.

## When the probe runs

Do not arm at process start. Startup, the menu, and loading are not the measured scene, and a fixed warmup such as 600 loops does not detect readiness.

The runner loads one private save, checks the level and the fixed camera, and only then issues one local control on all three builds. That check is outside the stamp window. After the control returns, the runner sends no bridge, socket, or network traffic until the process has quit.

The control is the same command on every build. It arms the probe. On `on` only, the same call also starts the collector. `base` and `off` do not start one. Starting is part of the excluded control, not a startup `perf.start` and not a load-time capture.

The probe stores nothing on the control iteration. The next outer boundary is stamp 0.

## Stamps

The boundary is the current `BeginLoop` site: after `Device.Run()`, before `SDL_QuitRequested()`, window events, `ProcessFrame`, and present. The baseline experimental patch uses the same `for (;;)` shape so its call sits before the pump. Production `e3f273f04` is not changed. On the checkpoint, the probe call is the line immediately before `BeginLoop`, not inside it.

```text
if not armed: return
t = steady_clock now, nanoseconds
if count < capacity: stamps[count++] = t
if count == capacity: armed = false
```

`capacity = Nwarmup + Nmeasured + 1`. Defaults: `Nwarmup = 120`, `Nmeasured = 4096`. The extra stamp only closes the last interval. Stored walls are `capacity - 1`: the first `Nwarmup` stay in the file and are excluded from the gate; the next `Nmeasured` are scored. Warmup begins only after the ready control. It is not a readiness test.

Auto-disarm on the closing stamp. The collector on `on` stops in that same iteration, after the stamp and before the next boundary, so its stop loop is not a retained probe wall and is not a retained collector sample. The process then quits normally. Quit and any later boundary find the probe disarmed and add no stamp. A file with any other stamp count, a non-monotonic wall, or `clock_is_steady=false` is invalid.

One nothrow allocation, size `capacity`, happens in the control call before the next boundary. No wrap, no I/O, and no second clock read on the hot path. Export runs after quit has left the loop, `O_EXCL`, then rename. A crash leaves no accepted file.

## Limiter

Both revisions use the same integer test in `CRenderDevice::ProcessFrame`:

```text
frameTime   = TimerGlobal.GetElapsed_ms() after worker wait
            - TimerGlobal.GetElapsed_ms() after BeforeFrame
updateDelta = 1000 / ps_fps_limit          # integer division
if frameTime < updateDelta:
    Sleep(updateDelta - frameTime)
```

`rs_fps_limit` is an integer from 30 to 501, default 501. `1000 / 501` is `1`. The sleep runs only when `frameTime == 0`, and the argument is then `1`. When `frameTime >= 1`, the sleep is skipped. This is not a 1 ms budget minus sub-millisecond work. `frameTime` is the integer millisecond span of FrameMove, render, present, and worker wait. It excludes the sleep itself and excludes the outer event pump that the probe includes.

At this setting a cost that leaves `frameTime` at 0 keeps a 1 ms sleep. A cost that moves `frameTime` from 0 to 1 removes that sleep, so the probe wall can fall by about 1 ms minus the added work. Sub-millisecond work inside a 0 ms bucket does not change the sleep. The conditional deltas are therefore full-loop wall deltas under this limiter, not isolated CPU cost. The campaign keeps `rs_fps_limit 501` and vsync off. Setting the limit to 0 is a divide by zero, not an uncapped mode. Menu or pause uses `1000 / ps_fps_limit_in_menu` (`1000 / 60 = 16`) and is not a measured scene.

Do not change the condition or the sleep argument in the experimental builds. Do not drop rows because a wall is short or long.

Optional, and only if implemented identically on all three builds: store one `limiter_slept` byte for the sleep decision just taken, without changing whether `Sleep` runs. The limiter-free table is the predeclared subset `limiter_slept == 0`. Publish it beside the full-population table, with its count per run. It is not a latency filter. If the byte is absent, do not invent the subset; the integer formula above is the limitation to report.

`!b_is_Active` still does `Sleep(1)` after the limiter on both revisions. A measured window that hits that path, a level change, device loss, the menu, or pause is functional contamination.

## Runs

Predeclare five processes per arm. Order cycles `base, off, on`, rotating the first arm each cycle. One extra unscored launch per binary may warm the shader cache; name it in the campaign record before launch so it is setup, not a discarded result.

Score every predeclared run that is valid. A large `p95 − p50`, or a spread across run medians, is reported and kept. It can be a real stall. It is not a reason to drop or replace a run.

Invalidate a run only for explicit contamination: wrong level or camera, control issued before the save was ready, bridge or network traffic during the stamp window, crash or non-zero exit, bad stamp count, collector saturation or a collector sample outside the probe window on `on`, inactive/menu/pause/device-loss in the log, or mismatched scene, save, `user.ltx`, manifest, or overlay. Keep the file. A rerun is allowed and is also kept. The campaign is inconclusive while a predeclared slot has no valid file. Inconclusive is not a pass and not a measured zero.

Percentiles are nearest rank `ceil(p * N)`, one-based, on the `Nmeasured` walls. Also report min, max, and the five medians. No bootstrap.

## Identity and provenance

Hash the probe core once. It is the same bytes on all three builds. Hash each call-site patch separately: the baseline `for (;;)` insertion and the checkpoint insertion are different diffs. Hash a limiter-byte patch separately too if it exists.

The probe CSV keeps `content_manifest=unknown` and `overlay=unknown`. Do not write a manifest checksum into those fields. Asset identity is the harness sidecar from `docs/superpowers/specs/2026-11-07-frame-time-provenance-design.md`: seal and attest outside the stamp window, leave the CSV bytes unchanged, and treat sidecar `result=verified` as the asset gate. In-band reporter acceptance stays false on `unknown`. The overhead campaign needs both a valid probe file and a verified sidecar. A missing sidecar leaves the asset gate open.

Header, no screenshot key and no image in the capture directory:

```text
# xray-overhead-probe-v1
# role=base|off|on
# revision=<full sha>
# probe_core_sha256=<sha256>
# callsite_patch_sha256=<sha256>
# binary_sha256=<sha256>
# scene=<name>  save=<name>  level=<name>
# vsync=off  frame_limit=501  resolution=<w>x<h>  renderer=renderer_r3
# user_ltx_sha256=<sha256>
# content_manifest=unknown  overlay=unknown
# clock_is_steady=<true|false>  clock_period_ns=<decimal>
# nwarmup=<n>  nmeasured=<n>  stamps=<n>  truncated=false
# machine=<hw.model>  run_index=<n>
loop_seq,start_ns,wall_ns
```

`stamps` must equal `nwarmup + nmeasured + 1`. The runner does not call `shot`.

## Limits

Conditional wall overhead under one shared probe, on one machine and one fixed-camera scene, five valid runs per arm. Not a probe-free baseline, not GPU time, and not proof that a matched difference is the instrumentation alone. The 501 limiter can hide or flip a sub-millisecond to 1 ms cost, as calculated above. Optimizations and donor comparisons wait until both deltas are inside the thresholds, every predeclared slot is valid or the campaign is explicitly inconclusive, and the sidecar is `verified`.

## Not in this slice

No source edit, build, launch, install, network call, provider call, commit, or PR. The later owner implements the probe, the one control command, and the runner.
