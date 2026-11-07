# OpenGL macOS Stability Baseline - Design Spec

**Date:** 2026-07-07
**Status (2026-11-07):** Harness shipped; five-run GL and 20-minute idle acceptance remain open. Historical runtime defaults below are superseded by [macOS setup](../../macos-dev-setup.md); see [PLAN_STATUS](../../PLAN_STATUS.md) for evidence and refs.
**Parent context:** `2026-07-05-engine-roadmap-v2.md` and `docs/HANDOVER.md`.

## 0. Purpose

OpenGL (`renderer_r3`) is the working runtime for OpenXVibeRay. Before the AI overhaul, the engine needs a dependable macOS baseline: the game should run CoC smoothly enough, survive repeated save/load/quit cycles, surface renderer errors clearly, and avoid unified-memory blowups on rz's 24 GB Apple Silicon Mac.

This milestone is not a renderer rewrite. It is a hardening and measurement pass for the existing GL path so future `xrMind` / `xrSim` work has a stable host runtime. Vulkan comes later; Metal remains parked unless explicitly resumed.

## 1. Goals

- Make `renderer_r3` the blessed development runtime for the AI phase.
- Build a repeatable bridge-driven GL benchmark and soak harness.
- Measure frame pacing, process memory, screenshots, logs, bridge responsiveness, and clean process exit.
- Convert crash/hang/performance issues into small repro scripts and targeted fixes.
- Produce per-run artifacts that make regressions easy to compare.
- Keep all changes compatible with the current CoC run directory and `-agent_bridge` workflow.

## 2. Non-goals

- Do not implement `xrMind`, `xrSim`, or live provider calls in this milestone.
- Do not rewrite the GL renderer architecture.
- Do not chase maximum graphics settings or Vulkan/Metal parity.
- Do not introduce a new test control channel; use the existing Unix-socket agent bridge.
- Do not make `-nosound` part of testing; it is known to break CoC scripts.

## 3. Target Runtime

Primary target:

- macOS on rz's 24 GB Apple Silicon Mac.
- Current CoC active run directory: `/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl`.
- Engine binary: `bin/arm64/Release/xr_3da`.
- Renderer: `renderer_r3`.
- Launch mode: `-agent_bridge`, with save `1` on `l05_bar` as the main loaded-game scenario.

The first pass measures this machine. Lower-memory Apple Silicon support can be treated as a later regression target once the current Mac is stable and understood.

## 4. Harness Architecture

The harness lives in `tools/` and wraps the existing bridge client rather than replacing it.

Core pieces:

- A scenario runner that starts `xr_3da` from the active CoC run directory with `-agent_bridge`.
- Socket readiness polling and bridge health checks (`hello`, `state`).
- Scenario scripts for menu smoke, load/play/save/load, repeat soak, and idle soak.
- Periodic sampling of bridge `state`.
- OS-side sampling of process RSS and basic macOS memory pressure signals where available.
- Log isolation by truncating or snapshotting `appdata/logs/openxray_radik zagirov.log` before a run.
- Screenshot checkpoints through `shot <name>`.
- A run-summary writer that emits both machine-readable data and a short human-readable report.

The first implementation can use existing bridge verbs plus process sampling. If that proves too thin, add narrow bridge verbs such as `perf` or extend `state`, but only after the harness shows the missing data clearly.

## 5. Scenarios

### 5.1 Menu Smoke

Launch to the menu, wait for the bridge, sample state, capture a screenshot, flush logs, and quit. This catches startup, renderer creation, screenshot naming, and teardown.

### 5.2 Load / Play / Save / Load

Drive the existing `bridge_soak.txt` path under a wrapper:

- load save `1`;
- dismiss load screen;
- move in all directions;
- sweep mouse look;
- capture screenshots;
- save `soaktest`;
- load `soaktest`;
- open/close inventory;
- quit.

This is the main regression gate because it exercises gameplay, UI, script systems, GL rendering, save/load, and process teardown in one route.

### 5.3 Repeat Soak

Run the load/play/save/load scenario multiple times in fresh processes. This catches exit hangs, save/load flakes, memory growth across runs, stale sockets, and log-only failures.

Initial target: 5 consecutive clean runs. Raise this after the harness is stable.

### 5.4 Idle Soak

Load into `l05_bar`, dismiss the load screen, then stand in-game while sampling state and memory for an extended period.

Initial target: 20 minutes. Raise to 60 minutes once short soaks are clean.

## 6. Metrics

Each run records:

- Build identity: git commit, branch, build type, binary path.
- Runtime settings: renderer, resolution, CoC run directory, scenario, launch command.
- Bridge health: connect latency, command failures, final state.
- Frame observations: `fps` and `frame` from bridge `state`, sampled over time.
- Stall signals: long gaps in frame counter progress or bridge response.
- Memory: RSS high-water mark, start/end RSS, and growth over scenario.
- Log findings: fatal lines, GL error lines, script runtime errors, hangs during quit/load/save.
- Screenshot artifacts: expected screenshot names, file existence, nonzero size.
- Process result: clean quit, timeout, crash, signal, or forced kill.

The first pass should prefer simple, reliable metrics over elaborate profiler integration. More detailed GL timing can come after there is a baseline.

## 7. Acceptance Gates

A GL baseline run passes when all of the following are true:

- The bridge connects and stays responsive.
- The scenario completes without process crash, forced kill, or timeout.
- Save/load steps complete and return to playable state.
- Expected screenshots exist and are nonempty.
- The isolated log has no fatal GL errors, Lua runtime errors, or repeated renderer error spam.
- The process exits after `cmd quit`.
- Memory high-water is recorded and does not hit macOS critical pressure.
- Frame samples show continued frame progress after load warm-up.

Initial performance targets for the current 24 GB Mac:

- Menu smoke: no crash or GL fatal, screenshot produced, clean exit.
- Load/play/save/load: no frame-progress stall longer than 10 seconds after warm-up.
- Idle soak: no monotonic RSS growth above 512 MB after the first 5 minutes.
- Repeat soak: 5 clean runs with no stale socket or teardown hang.

Frame-rate targets should be calibrated from the first measured run rather than guessed. The first report establishes baseline median FPS and visible spikes; subsequent fixes should improve or preserve it.

## 8. Fix Policy

The harness is the source of truth. Fixes should map to observed failures:

- Crash or hang: highest priority; make a minimal repro scenario first.
- GL error: isolate the command/log path and stop repeated spam.
- Save/load or quit flake: preserve bridge repro and verify with repeat soak.
- Frame-time spike: add measurement, then fix the specific stall.
- Memory growth: identify whether it is per-load, per-screenshot, per-run, or steady idle growth.

Every engine change must keep GL behavior intact and avoid touching Metal/Vulkan unless the failure is in shared platform code and the shared change is justified.

## 9. AI Handoff

This milestone prepares, but does not implement, the AI overhaul.

The future AI work needs:

- long-lived GL sessions that do not crash while agents are thinking;
- bridge-driven run control for `ai.*` verbs;
- reliable screenshots for agent observation and regression evidence;
- predictable save/load behavior for replay tests;
- memory headroom for future `xrMind` state, logs, and provider tooling.

Once the GL baseline is green, the next design/plan should start the first AI vertical slice on top of it: `xrWorldState` scaffold, `ToolDispatcher`, tool log, provider-off replay, and bridge inspection verbs.

## 10. Risks

- Current FPS and memory behavior are not yet measured, so numeric performance thresholds may need calibration after the first harness run.
- CoC scripts can fail for reasons unrelated to GL; the harness should classify these separately instead of hiding them under renderer failures.
- macOS process memory numbers can vary by tool; use one primary method consistently and treat others as supporting evidence.
- A long GUI process can survive a wrapper timeout; the runner must always clean up by PID and stale socket path.
- The existing bridge reports coarse FPS only; deeper frame pacing may require a narrow engine-side metric addition.

## 11. Deliverables

- `tools/gl_macos_soak.py` or equivalent wrapper for launch, sampling, scenario execution, and artifact collection.
- Scenario files or inline scenario definitions for menu smoke, load/play/save/load, repeat soak, and idle soak.
- Per-run artifact folder under `appdata/` or `artifacts/` with summary JSON, report text, log copy, and screenshots.
- Documentation update in `docs/macos-dev-setup.md` or `docs/HANDOVER.md` describing the GL gate command.
- Targeted engine fixes only after the harness exposes concrete failures.
