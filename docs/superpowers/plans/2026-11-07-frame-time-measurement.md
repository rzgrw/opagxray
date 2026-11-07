# Frame-time measurement implementation plan

**Date:** 2026-11-07
**Status:** Standalone core/report slice and project-wiring gate verified with project-wide unity ON; runtime instrumentation, export, and overhead gates remain open
**Design:** [Frame-time measurement design](../specs/2026-11-07-frame-time-measurement-design.md)

## Goal

Add opt-in, bounded, complete-loop telemetry to target GL, export it through the existing agent-bridge control path, and validate measurement correctness and overhead before using it for optimization decisions.

## Architecture and constraints

Use a `CFrameTimeCollector` owned by `CRenderDevice`, `steady_clock` nanosecond timestamps, preallocated fixed-size records, and existing main-thread bridge dispatch. Instrument the outer application loop before `SDL_QuitRequested()` and use independent loop IDs. Start/stop control loops are excluded; capacity, dropped loops, and truncation are explicit.

- No per-frame allocation, I/O, logging, network activity, locks, or added GL synchronization.
- No GPU-time claims, phase summation, FPS-derived frame times, or changes to worker/input scheduling.
- Keep Darwin failure handling value-based; no exceptions across engine/Lua boundaries.
- Target first. Do not modify donor code or claim cross-engine results from hidden autotest.
- Preserve existing sound initialization, private appdata, and exact base/patch plus explicit overlay provenance.

## Task 1: Collector state machine and schema tests

**Files:**

- Created `src/xrEngine/FrameTimeCollector.h` and `.cpp`.
- Created `src/xrEngine/tests/FrameTimeCollectorTests.cpp` and `FrameTimeCollectorBoundaryTests.cpp`, with standalone `xrFrameTimeCollectorTests` and `xrFrameTimeCollectorBoundaryTests` wiring in `src/xrEngine/CMakeLists.txt`.
- Device ownership remains in Task 2; no engine device source has been modified.

**Interfaces:**

- `BeginLoop()` obtains `steady_clock` time; `BeginLoopAt` accepts explicit nanosecond boundaries for deterministic tests and production state entry.
- Explicit phase begin/end calls support nested and repeated phases, per-loop containment, monotonic event timestamps, and checked duration/call arithmetic.
- Control methods implement start, stop, status, stopped reset, and deliberate disabled-reset no-op; filesystem export and bridge verbs remain Task 3.
- Fixed-size records contain loop/frame IDs, flags, and every phase's calls/duration. Explicit phase calls require loop-scoped lifetimes because no scope/generation token exists.

**Tests:**

- [x] First boundary is baseline-only; start and stop loops never appear as samples.
- [x] Warmup counts complete loops and stores none; zero warmup starts at the first complete post-baseline loop.
- [x] Early return with no phases produces calls `0`; repeated measured zero durations and same-phase nesting remain distinguishable from not-executed phases.
- [x] Multiple `LoadDraw`/present calls accumulate in one loop while `loop_seq` remains independent of engine frame IDs.
- [x] Exact capacity remains truncated through stop-pending/stopped with zero drops; the next complete loop increments drops without overwriting retained records.
- [x] Capture start/end bound retained measured records, excluding warmup and stop loops; empty captures report zero times.
- [x] Warmup and stop-pending reversal preserve invalid accounting and monotonic status; reversed intervals are not accepted as clean records.
- [x] Start-active, stop-disabled, reset-active, repeated stop, armed/warmup stop, start-after-stopped, stale phase after reset, allocation failure, and free-once lifetime have defined results.
- [x] Reset clears all discarded capture metadata and disabled reset is explicitly idempotent.
- [x] Reversal, mismatch, underflow, depth, duration and frame-range failures invalidate affected loops and prevent clean acceptance.
- [x] Loop-sequence exhaustion rejects new records without wrap; drop/invalid diagnostic counters saturate rather than erase history.
- [x] Disabled calls perform no clock reads, allocations, record writes, or I/O; finalized and stopped records remain immutable.
- [ ] Bridge export/disconnect and engine shutdown warning behavior remain integration tests.

## Task 2: Source instrumentation without behavior changes

**Files:**

- Modify `src/xrEngine/x_ray.cpp`, `device.cpp`, and minimal collector declarations.
- Modify `src/Layers/xrRenderGL/glHW.cpp` for existing `CHW::Present` only.
- Modify `src/Layers/xrRender/r__sync_point.cpp` and `r__occlusion.cpp` only around existing waits.

**Implementation:**

- [ ] Preserve loop quit semantics while placing `BeginLoop` before `SDL_QuitRequested()` and all SDL/window work.
- [ ] Span `FrameMove`, `DoRender`, `CHW::Present`, scheduler wait, limiter, inactive/not-ready/device sleeps, and `LoadDraw` exactly as the design table specifies.
- [ ] Record loading/menu/paused/precache/inactive/device-unavailable flags at execution; do not infer occlusion from a later state query.
- [ ] Measure existing fence and blocking occlusion calls as nested primary-thread CPU waits. Add no GL call or synchronization.
- [ ] Confirm worker wait is only main-thread blocking time and no worker execution field is introduced.
- [ ] Confirm all early returns finalize on the next outer boundary and no phase is defaulted to zero.

**Focused checks:**

- [ ] Existing collector tests plus a source-level call-order review covering loading, not-ready, inactive, lost/occluded, normal render, and quit.
- [ ] Build the normal target and focused tests when implementation is authorized; no runtime change is accepted on compilation alone.

## Task 3: Bridge control, bounded export, and offline report

**Files:**

- Modify `src/xrEngine/AgentBridge.h` and `.cpp` using existing `OnFrame` request handling.
- Modify `tools/gl_macos_soak.py` only for control/artifact integration; `tools/agentctl.py` already accepts arbitrary verbs.
- Created `tools/frame_time_report.py` and `tools/tests/test_frame_time_report.py`; bridge and filesystem export remain unimplemented.

**Implementation:**

- [ ] Add `perf.start [capacity] [warmup_loops]`, `perf.stop`, `perf.status`, `perf.export`, and `perf.reset`; socket-thread behavior and input paths remain unchanged.
- [ ] Start capture before a load; never move bridge `OnFrame` into loading-only loops. Surface existing command timeout/responsiveness limitations truthfully.
- [ ] Export only after stopped, below `$app_data_root$/perf/`, using a generated nonoverwriting capture name. On write failure return an error and retain records for retry.
- [ ] Export metadata and raw integer CSV from the design; no per-frame response or giant socket payload.
- [x] Implement nearest-rank p50/p95/p99, strict long-frame thresholds, declared scene filters, provenance display, and explicit empty/partial/unprovenanced/complete verdicts; always include both all-loop and steady populations with loop/time bounds, sampled/elapsed duration, phase summaries, flag counts, and combined failure reasons.

**Tests:**

- [x] Independent literal 11-phase core-compatible begin-timestamp fixture, N=20 nearest rank, and strict threshold equality; empty, malformed CSV, nonfinite/invalid, duplicate/missing, uint32/uint64 overflow, retained-envelope, warmup/prefix/capacity accounting, out-of-order, and count/duration-mismatched inputs fail without dropping rows.
- [x] Accounted invalid-loop holes remain partial diagnostics, including zero-time omitted loops; unexplained gaps, overlap, and reversed recovery envelopes reject even with invalid accounting. One phase call is bounded by wall time; repeated/nested calls are bounded by calls times wall time.
- [ ] Export failure/retry, socket disconnect, active shutdown, stop during loading, and export before stopped retain truthful state and normal teardown.
- [x] Reports keep nested CPU phases separate, reject truncated/empty captures for acceptance, and block unknown/unavailable required provenance from acceptance.

## Task 4: Target-only runtime scenarios

Create scenarios only when implementing the plan; do not reuse historical save `1` or donor settings/saves.

1. **Fixed camera:** fresh private Cordon game, verified level/position/camera, visible 1280×720 GL, matched settings, declared warmup, then a stationary capture with no screenshots/readback during the measured interval.
2. **Movement:** the same starting conditions plus a bounded movement/look route, captured separately from the fixed-camera gate.
3. **Lifecycle:** start before load, measure loading with labels, save to a unique private name, reload, stop after bridge responsiveness returns, export, and quit normally.

Use the exact base and patch sources and private runtime from `docs/macos-dev-setup.md`. Keep sound enabled with effect/music volume muted. Record content and overlay hashes, actual cvars, SDL/build/allocator, cache state, resolution, and drawable size.

The current harness parser supports `--repeat`; use target-only repetitions, not a claimed donor A/B. Example after scenario implementation:

```bash
python3 tools/gl_macos_soak.py \
  --game-dir "$PWD/build/runtime/coc-verification" \
  --binary "$PWD/bin/arm64/Release/xr_3da" \
  --socket appdata/b.sock \
  --scenario tools/gl_fixed_camera_perf.txt \
  --artifacts artifacts/frame-time/fixed-camera \
  --repeat 5 \
  --timeout 300
```

## Task 5: Acceptance gates

### Correctness and lifecycle

- [ ] All focused C++/Python tests pass and the normal target builds.
- [ ] A full-loop record includes present, worker wait, and limiter; loading/not-ready/device sleep cases are labeled, not lost.
- [ ] No per-frame allocation/I/O/network/sync appears in the implementation review.
- [ ] Export/stop/shutdown tests pass, including active shutdown without implicit export.
- [ ] World and menu images are inspected; logs and normal process exit are clean.

### Overhead

**Open prerequisite:** disabled and pre-change binaries cannot export this collector's full-loop distributions. Before any overhead comparison, declare and separately validate a common external start-to-start reference or an independently gated reference method in those binaries; do not substitute FPS, sparse bridge samples, or donor autotest totals. Until that method exists, the runtime overhead gate remains open and no overhead value is claimed.

After the reference method exists, use the same optimized binary, visible fixed-camera scene, uncapped/vsync-off settings, and five alternating disabled/capture-active target runs, with a separate pre-change reference comparison:

- Disabled instrumentation versus pre-change: median whole-loop regression no more than `max(0.05 ms, 1% of baseline)` and p95 regression no more than `max(0.10 ms, 3% of baseline)`.
- Capture-active versus disabled: median whole-loop increase no more than `0.10 ms` and p95 increase no more than `0.25 ms`.
- Report every distribution and environment difference. Noise beyond a threshold is inconclusive, not proof of zero overhead; rerun once under controlled conditions or reduce instrumentation before acceptance.

### Initial measurement gate

- [ ] Five complete, nontruncated fixed-camera captures have valid provenance and schema, no dropped loops, and clean visual/log/exit evidence.
- [ ] Report raw all-loop and declared steady-scene p50/p95/p99, maximum, strict long-frame counts, actual duration, and RSS separately.
- [ ] No optimization is selected until the dominant measured phase identifies it.

### Later gates

Movement and load-save-reload-normal-quit are independent follow-ups. Cold shader cache and warm cache are separate conditions, with isolated private appdata. Cross-engine comparison remains blocked until donor has equivalent visible steady-clock full-loop telemetry; existing donor calc/dump/total and hidden autotest cannot satisfy that gate.

## Standalone slice verification evidence

Red checks before implementation:

- `c++ -std=c++20 -fno-exceptions -Isrc src/xrEngine/tests/FrameTimeCollectorTests.cpp src/xrEngine/FrameTimeCollector.cpp -o build-frame-time-red` failed because `FrameTimeCollector.cpp` did not exist.
- `python3 -m unittest tools.tests.test_frame_time_report -v` failed with `ModuleNotFoundError: tools.frame_time_report`.

Green checks after implementation and review-fixture corrections:

- `c++ -std=c++20 -fno-exceptions -Wall -Wextra -Werror -Isrc src/xrEngine/FrameTimeCollector.cpp src/xrEngine/tests/FrameTimeCollectorTests.cpp -o artifacts/frame-time/task-2b6658f92323/xrFrameTimeCollectorTests && artifacts/frame-time/task-2b6658f92323/xrFrameTimeCollectorTests` passes all standalone collector checks.
- `python3 -m unittest tools.tests.test_frame_time_report -v` passes 7 tests.
- `python3 -m unittest discover -s tools/tests -v` passes 48 tests.

Project wiring was attempted without a full engine build. `cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0`, and the same command with the documented MacOSX15.4 SDK, both stopped at the existing `Externals/LuaJIT-proj/CMakeLists.txt` unsupported-target-architecture check. Therefore the direct standalone test is green, but `xrFrameTimeCollectorTests` has not been generated or built through project CMake. No game or runtime acceptance was run.

### Independent review delta

Before fixes, the supplied ASan/UBSan suite passed, the independent `probes.cpp` reproduced seven contract failures, and `boundary-probes.cpp` reproduced three injected counter failures. After fixes, using `-std=c++20 -fno-exceptions -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc` and outputs below `artifacts/frame-time/task-8fe1382118f0/`:

- The supplied collector tests pass with no sanitizer diagnostics.
- The independent contract probes report `0 contract assertions failed`; retained status times are `[200,300]`, warmup reversal retains invalid accounting, and stop-boundary reversal returns `TimeReversal`.
- `src/xrEngine/tests/FrameTimeCollectorBoundaryTests.cpp` passes safe sequence/drop/invalid exhaustion regressions. The original sequence probe indexed `Samples().data[1]`/`[2]` after assuming wrapped records were accepted; correct rejection retains only the pre-exhaustion record, so the safe replacement checks state and retained size without out-of-bounds access.
- Same-phase stale-scope behavior remains an explicit API limitation, not a claimed rejection capability; future hooks must keep RAII scopes within one loop/capture generation.

The standalone core delta was subsequently independently approved by Codex for the production monotonic steady-clock path; see the independent approval and CMake diagnosis section below. Integration and runtime acceptance remain open.

### Parser recovery verification (2026-11-07, dispatch ctx_eb1f0ac95b50)

Preserved the prior parser edits and the independently approved C++ core, tests, and CMake wiring. The parser now treats `start_ns` as the loop begin, checks `end=start+wall`, and matches the retained-row envelope without imposing a warmup timestamp gap. Inconsistent saturation and prefix-loop accounting cannot produce a clean report. Strict CSV parsing preserves embedded newlines and rejects malformed quoting instead of repairing invalid input. Required provenance cannot pass when blank or explicitly unknown; resolution/vsync/frame-limit values are validated. Both all-loop and steady summaries are present regardless of the selected scene.

The added regressions first produced **12 assertion failures and 3 errors across 19 tests**: malformed CSV was accepted, saturation/prefix accounting was underchecked, both populations were missing, very large integers escaped as raw conversion errors, and zero-time invalid loops were rejected instead of remaining partial diagnostics. After the fixes, with isolated `TMPDIR` and `PYTHONDONTWRITEBYTECODE=1`:

- `python3 -m unittest tools.tests.test_frame_time_report -v`: **19 passed**.
- `python3 -m unittest discover -s tools/tests -v`: **60 passed**. The trailing printed `run 1/1: FAIL` is the expected mocked negative-process-exit scenario in `LiveCliTests.test_live_cli_fails_nonzero_process_returncode`; the suite exits zero.
- Real Python CLI subprocess tests verify acceptance exit codes 0/1/2, combined failure reasons, inspectable partial/unknown captures, and rejected reversed envelopes. No engine executable is launched.
- SHA-256 comparison of the frozen collector source/header, both C++ test sources, and CMake file matches the pre-edit snapshot. No C++ edits, compile, configure, game, GUI, provider/network call, commit, push, or merge was performed in this recovery.

Local evidence was initially recorded under `/var/folders/kb/k444p9xs5b5b2p1z_rzqwjj80000gn/T/frame-parser-ctx-eb1f-rtj7g30k/` and subsequently preserved by the coordinator under `artifacts/frame-time/accepted-standalone-evidence/parser-recovery/`, including `red.log`, `focused.log`, `tools.log`, and `frozen-hashes.json`. The seven/48-test results above are historical and superseded for Python verification by this rerun; C++ results were not rerun in the parser task. Grok subsequently independently approved the final parser hashes as recorded below. Runtime/hooks/export, token-based stale-scope protection, overhead/reference measurement, provenance authenticity, and donor equivalence remain outside this slice.

### Independent approvals and project CMake diagnosis (2026-11-07)

The coordinator confirmed both independent approvals: Codex approved the standalone core delta, and Grok approved the final parser at SHA-256 `24e75c36cb63e70ddc0b2b1e5a8b7235cc4027b8d34452bb79ba6f7785c10670`, with tests at `297fb69cfe5861e4582870f017d1a4c49d4e9fd69d7ad2697d2483660b5f0156`. The seven reviewed core/header/C++ test/CMake/parser/Python test hashes are retained in `artifacts/frame-time/accepted-standalone-evidence/approved-source-hashes.json`; all matched before and after this diagnosis. The independent core report is `core-delta-review/review.md` beneath that evidence directory. These approvals cover the standalone slice, not runtime integration or performance acceptance.

The core review retains a **nonblocking P3 injected-clock high-water follow-up**: boundaries `100,200,50,60,70` can retain `[100,200]` and `[60,70]`, with two invalid loops and a reversed capture envelope. A future optional fix can stop on reversal or preserve a high-water timestamp until monotonic recovery. The approved parser rejects these invalid overlapping/reversed captures; the core was deliberately left unchanged. This requires a reversing injected clock, rather than the production steady clock.

**Concrete configure blocker:** every one of the nine git submodules is uninitialized (`git submodule status` has a leading `-` for each). In particular, `Externals/LuaJIT/src` and `lj_arch.h` do not exist; the pinned LuaJIT gitlink is `5a5cd82e435a7b08d61b4e0af887493d24d9eca1`. `Externals/LuaJIT-proj/CMakeLists.txt` runs its architecture preprocessor with that nonexistent working directory, ignores `RESULT_VARIABLE`, sees empty stdout/stderr, and emits the misleading unsupported-architecture error at line 183. A read-only reproduction of the exact probe with its result captured reports `probe_result=no such file or directory`, with empty output and error. This is missing source material, not evidence of an unsupported arm64 compiler or SDK.

The existing `build/CMakeCache.txt` and `build/CMakeFiles/CMakeConfigureLog.yaml` were inspected first: compiler ABI and LTO probes had succeeded for arm64, while the cache pointed to the absent LuaJIT directory. The retained authority-recovery configure log belongs to another historical workspace and records success there; it does not establish that this checkout has initialized dependencies. The installed `MacOSX15.4.sdk` was verified before using it.

A new isolated configure was attempted with AppleClang 17 and CMake 4.3.1:

```bash
cmake -S /Users/rz/orca/opagxray \
  -B /Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_CXX_FLAGS=-fno-exceptions \
  -DXRAY_METAL=OFF -DXRAY_VULKAN=OFF \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/runtime \
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/runtime
```

Result: **configure exit 1 at the same LuaJIT check** after successful C/C++ ABI, libc++, and LTO checks. No project build system was generated, so neither collector target was built or run through CMake. The Release-specific output settings were cached and avoid the repository's generic output-directory override; no executable was emitted to existing `bin`. Darwin `XRAY_EXCEPTIONS=0` remains unchanged in source, with `-fno-exceptions` additionally supplied for the eventual focused build.

The precise next step is to authorize populating this checkout's pinned submodules from verified local data or an explicitly permitted fetch, then rerun the configure above and, only on success, execute:

```bash
cmake --build /Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/build \
  --target xrFrameTimeCollectorTests xrFrameTimeCollectorBoundaryTests -j10
/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/runtime/xrFrameTimeCollectorTests
/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/runtime/xrFrameTimeCollectorBoundaryTests
```

Those build/run commands are **pending, not executed**. No source fix is required to supply missing dependencies; an optional future diagnostic-only patch could check that `${LUAJIT_DIR}/lj_arch.h` exists and check the probe's result before interpreting its output. Configuring also invokes `make clean` within LuaJIT's source directory, so future dependency provisioning must account for that existing side effect; this attempt could not enter the absent directory. No submodules, reviewed source, existing build/bin/runtime trees, or dependency installations were changed. No full engine build, game, GUI, network/provider activity, commit, or publication occurred. Diagnosis evidence and unchanged-hash/submodule checks are under `artifacts/frame-time/task-d6a77ea9b43c/`.

### Authorized pinned-dependency retry and project-target verification (2026-11-07, dispatch ctx_8105c278e080)

This retry supersedes the missing-submodule blocker and pending build/run status above. The coordinator explicitly authorized recursive initialization at superproject-recorded revisions and necessary dependency fetches. Before initialization, all nine top-level directories were verified absent or empty, all submodules uninitialized, and all seven approved hashes matched. `git submodule update --init --recursive` exited **0**, producing nine top-level plus four nested clean submodules at their recorded gitlinks; no dependency branch was upgraded. Configure's existing LuaJIT `make clean` ran only in this freshly provisioned, clean source. The complete revision list appears below and in `artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/submodules-after.txt`.

The first fresh isolated SDK 15.4/arm64 Release configure with `CMAKE_UNITY_BUILD=ON` exited **0**, resolving the misleading LuaJIT architecture failure without a source change. Its two-target build exited **2**: `xrFrameTimeCollectorTests` linked, but the boundary target had three private-member access errors. The generated unity source includes `FrameTimeCollector.cpp` first, so its header is already guarded before the boundary test's `#define private public` takes effect. This is a concrete unity/test-translation-unit incompatibility, unrelated to LuaJIT or architecture.

The locally installed CMake help documents `CMAKE_UNITY_BUILD` as a developer-controlled option and recommends disabling the target property for incompatible targets. A second fresh isolated configure changed only unity mode plus isolated directory names; **configure, both target builds, and both executable runs exited 0**. This uses real root-project CMake wiring, C++20, Release `-O3`, ThinLTO, SDK 15.4, deployment target 15.0, arm64, inherited `XRAY_EXCEPTIONS=0`, and explicit `-fno-exceptions`; the verbose log confirms those flags. The tests use active check functions rather than disabled Release assertions. Both binaries are Mach-O arm64. Existing `-fdelayed-template-parsing` deprecation warnings remain nonfatal.

Exact provisioning, both configure attempts, both builds, and runs are also recorded as argument arrays in `retry-pinned/commands.json`. Successful commands (from the repository root):

```bash
git submodule update --init --recursive
cmake \
  -S \
  /Users/rz/orca/opagxray \
  -B \
  /Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/build-nonunity \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=OFF \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_FLAGS=-fno-exceptions \
  -DXRAY_METAL=OFF \
  -DXRAY_VULKAN=OFF \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/runtime-nonunity \
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/runtime-nonunity
cmake --build /Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/build-nonunity --target xrFrameTimeCollectorTests xrFrameTimeCollectorBoundaryTests -j10 --verbose
/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/runtime-nonunity/xrFrameTimeCollectorTests
/Users/rz/orca/opagxray/artifacts/frame-time/task-d6a77ea9b43c/retry-pinned/runtime-nonunity/xrFrameTimeCollectorBoundaryTests
```

The first attempt used the identical configure arguments with `-DCMAKE_UNITY_BUILD=ON`, `-B .../retry-pinned/build`, and both Release output paths `.../retry-pinned/runtime`; its build command used that first build directory and the same targets, `-j10 --verbose`.

- `xrFrameTimeCollectorTests`: `PASS: standalone frame-time collector state, bounds, phases, clocks and accounting`.
- `xrFrameTimeCollectorBoundaryTests`: `PASS: nonwrapping loop sequence and diagnostic counters`.

**Historical limitation from the provisioning retry (resolved by the final verification below):** the documented unity-on build could not build the boundary target. Recommended minimal follow-up in `src/xrEngine/CMakeLists.txt`, immediately after that target is created:

```cmake
set_target_properties(xrFrameTimeCollectorBoundaryTests PROPERTIES UNITY_BUILD OFF)
```

This recommendation was **not applied in that retry**: all seven reviewed source hashes remained unchanged at that point. The coordinator can authorize and independently review this one-line CMake delta, then rerun the unity-on two-target build and both executables; the current unity-off configuration already verifies both project targets. No broader dependency setting was disabled. CTest registration was not added; the generated target executables were run directly. Normal full-engine compilation, hooks/export, runtime regression and overhead gates remain unexecuted, so the combined acceptance checkbox above stays open. The Codex core and Grok parser approvals and nonblocking injected-clock high-water note remain as recorded above.

Pinned resulting revisions (all clean after configure/build/run):

```text
5d8812d703d0335741b6f7ffc37838eeb8b967f7 Externals/AGS_SDK
3e434803d95c206c065c6172536b45c357bf3e21 Externals/GameSpy
5a5cd82e435a7b08d61b4e0af887493d24d9eca1 Externals/LuaJIT
7efdcf87dd51cfe828b0da1068f2072542b5631a Externals/gli
3fb22b836ba968c62d280d9de079ec5ed7fcd1b5 Externals/imgui
8da131b830f2615115c65438d48ac52cb9b7fa00 Externals/luabind
63a4a82773f2b6230fc0e9f246dbeeb1d66732e8 Externals/luabind/test/jenkins-ctest-plugin
f1bc16e4b107f368a6098edd0d423803657837bd Externals/sse2neon
373f7886c9d70237e426bd7fb7b3a021085427a2 Externals/sse2rvv
0e89050f39fc1e6b21b856ed8b7a3d68ff486ccb Externals/xrLuaFix
314c0d0fe8f4676ef35ac8abf3731be8535812fb Externals/xrLuaFix/lfs
983a3bfd646486292daa9f2ec9b72409f86dc931 Externals/xrLuaFix/lua-marshal
c1e5a149b571cc31069e7e3146e881c203bdd052 Externals/xrLuaFix/lua-pack
```

Evidence: `retry-pinned/provision.log`, `configure.log`, `build.log` (unity failure), `configure-nonunity.log`, `build-nonunity.log`, both target-named run logs, `runs.json`, and before/after reviewed-hash and submodule snapshots, beneath `artifacts/frame-time/task-d6a77ea9b43c/`. Only pinned dependency provisioning and the measurement plan/report were changed in this retry; no reviewed code edits, existing build/bin/runtime deletion, Homebrew install, full engine build, game/GUI/provider call, commit, push, PR, or merge occurred. Network use was limited to authorized dependency provisioning.

### Final project-wide unity verification (2026-11-07, dispatch ctx_923b21a44581)

The authorized follow-up applied exactly one build-setting line in `src/xrEngine/CMakeLists.txt`, immediately after the boundary executable declaration:

```diff
+set_target_properties(xrFrameTimeCollectorBoundaryTests PROPERTIES UNITY_BUILD OFF)
```

The prior unity-off pass above remains historical evidence; this final pass verifies the documented **project-wide `CMAKE_UNITY_BUILD=ON` configuration** with only the boundary target excluded. The prior unity failure supplies the red check: its generated translation unit parsed the collector header before the test's private-access macro. No collector, parser, or test-source changes were necessary.

Fresh isolated configure, two-target build, and both executable runs all exited **0**. Exact commands:

```bash
cmake -S /Users/rz/orca/opagxray -B /Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_FLAGS=-fno-exceptions \
  -DXRAY_METAL=OFF \
  -DXRAY_VULKAN=OFF \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/runtime \
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=/Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/runtime
cmake --build /Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/build --target xrFrameTimeCollectorTests xrFrameTimeCollectorBoundaryTests -j10 --verbose
/Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/runtime/xrFrameTimeCollectorTests
/Users/rz/orca/opagxray/artifacts/frame-time/task-29dd71aa0257/runtime/xrFrameTimeCollectorBoundaryTests
```

The verbose build contains exactly three C++ compile invocations: the normal collector test uses `Unity/unity_0_cxx.cxx`, while the boundary target separately compiles `FrameTimeCollector.cpp` and `tests/FrameTimeCollectorBoundaryTests.cpp`. All three explicitly contain `-DXRAY_EXCEPTIONS=0`, `-fno-exceptions`, `-std=gnu++20`, `-O3`, `-flto=thin`, `-arch arm64`, SDK 15.4, and deployment target 15.0. This confirms target-only unity exclusion and unchanged Darwin exception policy. The existing delayed-template-parsing deprecation warnings remain nonfatal.

Both executables report PASS: collector state/bounds/phases/clocks/accounting, and nonwrapping loop-sequence/diagnostic counters respectively. No CTest registration was added; both real project target executables were run directly.

CMake SHA-256 changed from `f23a66ae60ff3f37dcb3e1408285a0f020558025d7f189cdff67b2bd5a98a6c6` to **`26e7831fe41dace1a12e06645302b0f12e1ba46ed2818e5421ce03becf41b824`**. The other six approved hashes (core/header, both C++ tests, parser, Python tests) are unchanged; all 13 recursive submodule revisions are unchanged and clean. Prior independent core and parser approvals remain applicable to their unchanged files; this isolated CMake delta is separately evidenced here.

**Standalone project-wiring gate: closed.** The broader acceptance checkbox remains open because normal full-engine compilation, runtime instrumentation, bridge/filesystem export, runtime regression, and overhead/reference measurement were not performed in this slice. The nonblocking injected-clock high-water note remains unchanged. The coordinator can review this exact CMake delta and evidence before its checkpoint commit.

Evidence is under `artifacts/frame-time/task-29dd71aa0257/`: `configure.log`, `build.log`, `verified-compile-commands.txt`, both target-named run logs, `runs.json`, `commands.json`, `target-only-unity.patch`, before/after reviewed hashes and submodule snapshots, and `report.json`. Only the CMake line and this plan were edited outside ignored build/evidence artifacts; no dependency revision change, full engine/game/provider/GUI activity, network, commit, push, PR, merge, or deletion occurred.

## Deliverable

A standalone collector core and strict offline reporter with focused evidence. Device/loop/GL instrumentation, bridge/filesystem export, the external overhead reference, runtime acceptance, and donor equivalence remain pending; this slice does not claim a performance improvement or complete the telemetry milestone.
