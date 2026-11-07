# Frame-time measurement — design

**Date:** 2026-11-07
**Status:** Standalone core and offline reporter implemented; runtime integration pending
**Scope:** OpenXVibeRay target GL instrumentation first

## Purpose

Measure complete application-loop wall time and selected primary-thread phases without changing presentation, simulation, input, loading, or bridge scheduling. The initial acceptance case is a visible fixed-camera Cordon scene; movement, loading, save/reload, and donor comparison are separate later gates.

The collector must answer where CPU time was observed and which paths did not execute. It must not claim GPU time from CPU spans, infer frame time from FPS, or compare donor `calc_ms`/`dump_ms`/`total_ms` as though they were complete-loop intervals.

## Non-goals

- No renderer migration, new GL synchronization, worker-thread migration, or input dispatch change.
- No donor modification. Cross-engine claims require a later visible donor implementation with the same full-loop definition; hidden autotest `total_ms` is not a substitute.
- No per-frame allocation, file or log I/O, network traffic, or bridge polling.
- No automatic sampling through `FTimer`, `fTimeDelta`, or existing smoothed statistics.

## Required boundaries

### Complete loop

Use `std::chrono::steady_clock` nanoseconds, independently of paused or factored `FTimer`. Convert `CApplication::Run` to an equivalent `for (;;)` if necessary so `BeginLoop` executes before `SDL_QuitRequested()`, which pumps events, and before window-event processing (`src/xrEngine/x_ray.cpp:376-446`). Preserve quit behavior and existing `FrameMark` calls.

At each loop boundary, `wall_ns = current_start_ns - previous_start_ns` completes the record begun at `previous_start_ns`. This includes the previous loop's SDL work, `Device.ProcessFrame`, `UpdateDiscordStatus`, present/vsync, worker waits, limiter and inactive sleeps. The first boundary establishes a baseline and produces no sample. An independent `loop_seq` is required because `LoadDraw` can advance `Device.dwFrame` or present more than once in one application loop.

### Nested primary-thread phases

Instrument only existing work, with call counts and accumulated durations per loop:

| Field | Existing span | Semantics |
|---|---|---|
| `main` | `FrameMove` in `CRenderDevice::ProcessFrame` | Primary-thread frame/input work |
| `render` | `DoRender` | Render entry, submission, ImGui and present; not GPU time |
| `present` | `CHW::Present` | GL error drain, drawable blit and `SDL_GL_SwapWindow`; includes vsync wait |
| `worker_wait` | `TaskScheduler->Wait(processSeqParallel)` | Primary-thread wait only; never worker execution time |
| `limiter` | Existing FPS-limit sleep | Actual limiter wait, not target interval |
| `inactive_sleep` | Existing inactive `Sleep(1)` | Separate from the FPS limiter |
| `not_ready_sleep` | `BeforeFrame` not-ready `Sleep(100)` | Early-return path |
| `device_sleep` | `RenderBegin` lost-device `Sleep(33)` | Lost/minimized/hidden/occluded path |
| `load_draw` | `LoadDraw` in the loading-event branch | May contain multiple presents |
| `fence_wait` | Existing `glClientWaitSync` | Nested CPU wait, not GPU duration |
| `occlusion_wait` | Existing blocking GL query-result read | Nested CPU wait, not GPU duration |

`render`, `present`, `load_draw`, `fence_wait`, and `occlusion_wait` can nest or repeat. No phase durations may be summed to derive wall time. Add no `glFinish`, fence, availability query, readback, or other driver synchronization for telemetry.

A phase with `calls == 0` has an empty duration field, meaning not executed. A measured zero-nanosecond duration has `calls > 0` and value `0`. Neither case is a missing-data error.

### Classification

Capture flags at the applicable execution point rather than inferring them later:

- `loading`: loading-event branch executed; also retain `load_draw_calls`.
- `menu`: no level or main menu active.
- `paused`: `Device.Paused()` at frame classification.
- `precache`: `Device.dwPrecacheFrame != 0`.
- `inactive`: rendering skipped because the device is inactive.
- `device_unavailable`: `RenderBegin` returned lost/reset or the observed window was minimized, hidden, or occluded.

Early returns still create complete-loop records. Loading, menu, paused, and device-unavailable records are labeled and reported separately; they are not silently discarded and are not mixed into the fixed-camera steady-scene percentile gate.

## Collector contract

Add a primary-thread `CFrameTimeCollector` owned by `CRenderDevice`; the outer application loop accesses it through `Device`, and `CAgentBridge` only submits control requests through its existing main-thread `OnFrame` queue. No socket-thread callback touches collector state.

### Storage and disabled state

- `perf.start [capacity=32768] [warmup_loops=0]` accepts capacity 1–131072 and warmup 0–1000000 complete loops. Allocate a fixed record array once on start with `new (std::nothrow)`; allocation failure returns `allocation_failed` and leaves the collector disabled. Do not rely on `vector::reserve` or exception propagation on Darwin.
- Capacity limits loops, not time: 32768 loops last about 546 seconds at 60 Hz but only about 65.5 seconds at 500 Hz. Export actual duration and capacity.
- When never enabled, perform no clock read, allocation, record write, I/O, or logging. Only the disabled-state branch is permitted at instrumentation points.
- While capturing, use fixed-size integer records and no strings, container growth, locks, exceptions, file access, network access, or per-frame log messages. Darwin failures are returned as values.
- All control and export run outside measured loops. The start-control loop and stop-control loop are excluded in full. While armed, capturing, or saturated, all other `perf` controls, including status, are forbidden; stop is the only permitted control. Status is available while disabled, stop-pending, or stopped.

### State transitions

States are `disabled`, `armed`, `capturing`, `saturated`, `stop_pending`, and `stopped`.

1. `perf.start` in bridge `OnFrame` allocates and enters `armed`; the partial current loop is excluded.
2. The next outer boundary establishes the baseline. Warmup counts complete loops and stores no samples; capture starts only after the requested number of complete warmup loops.
3. Each later boundary finalizes the preceding complete loop. `loop_seq`, timestamps, flags, phase durations, and call counts remain unchanged after finalization.
4. Filling capacity enters `saturated` and marks persistent capture history truncated even when `dropped_loops` is still zero. Stop-pending and stopped retain that history. Every subsequent complete, otherwise-recordable loop increments `dropped_loops` without wrapping. Never overwrite old records or report a saturated capture as complete.
5. `perf.stop` enters `stop_pending` and marks the partial current loop excluded. The next outer boundary enters `stopped` without recording that loop or counting it as dropped. Stop while disabled, already stop-pending, or stopped returns an error without changing samples.
6. `perf.status` reports state, capacity, stored loops, warmup observed, dropped loops, invalid loops, truncation, first/last loop IDs, and measured capture start/end, subject to the active-capture control restriction above. Capture start is the first retained record's start and capture end is the last retained record's boundary; excluded warmup, invalid, dropped, and stop-control loops do not extend it. Empty captures unambiguously report `has_samples=false` and start/end zero.
7. `perf.export` is valid only in `stopped`; active or stop-pending export fails truthfully. `perf.reset` in stopped discards the capture and clears all capture metadata; reset in disabled is a deliberate idempotent no-op, and reset in every active state is an error.

Starting while already active fails; starting after stopped returns `reset_required` until an explicit reset. Phase/order/overflow errors, including reversal during warmup or stop-pending, increment `invalid_loops` without wrapping and prevent acceptance; a reversed boundary and the interval beginning there cannot become clean retained data. Loop-sequence exhaustion stops acceptance with `loop_sequence_overflow` instead of wrapping IDs. Stopped samples are immutable. Client disconnect does not implicitly start, stop, reset, or export a capture.

The explicit phase begin/end API has no scope token or capture-generation identity. Future hooks must use loop-scoped RAII and must not carry a scope across stop/reset/new capture; an old same-phase end can otherwise close a newly opened phase. The standalone core does not claim token-based stale-scope rejection.

### Loading and shutdown limitations

Existing `AgentBridge::OnFrame` runs after `BeforeFrame`, so it does not run on loading-only or not-ready early-return loops (`src/xrEngine/device.cpp:161-185,262-275`). Start capture before issuing a load and stop after loading has completed and the bridge is responsive again. A queued command can therefore time out during an indefinite load; report that as a bridge/lifecycle limitation, not as collector progress.

Do not move bridge scheduling to make export available. Shutdown never performs an implicit export and never blocks on the socket. Destroying an unexported capture discards it and emits one shutdown-time warning containing stored/dropped counts; normal engine teardown remains unchanged.

## Export and schema

`perf.export` writes one bounded file from the retained records after capture has stopped. The bridge returns the generated relative path and summary. Use a collector-generated capture ID below `$app_data_root$/perf/`; accept no arbitrary output path, never overwrite an existing file, and retain the buffer with an error response if any write fails. Export I/O and formatting are allowed only after stop and are not included in captured wall intervals.

The implemented offline schema begins with exactly `# xray-frame-time-v1`, followed by unique `# key=value` metadata and one exact CSV header. Required metadata is `state`, `capacity`, `warmup_loops`, `warmup_observed`, `stored_loops`, `dropped_loops`, `invalid_loops`, `truncated`, `capture_start_ns`, `capture_end_ns`, `clock_is_steady`, `clock_period_ns`, `source_build`, `renderer`, `content_manifest`, `overlay`, `render_resolution`, `drawable_resolution`, `vsync`, `frame_limit`, `sdl_version`, and `allocator_build`.

Every row contains `loop_seq`, `start_ns`, `wall_ns`, engine frame begin/end, and numeric classification flags. For every phase it contains `<phase>_calls` and `<phase>_ns`. Integers are nanoseconds or counts; no floating-point FPS is part of the raw schema. `start_ns` is the loop **begin**, and the checked sum `start_ns + wall_ns` is its end. Engine frame IDs and phase call counts are uint32; loop IDs, timestamps, and durations are uint64. A partial row, malformed CSV, duplicate/unknown field, unknown schema, invalid integer, decreasing timestamp, or invalid count/duration pair rejects the report rather than silently dropping or repairing records.

The metadata envelope must exactly equal the first retained row's begin and last retained row's end; warmup does not add a timestamp gap before the first row. Empty captures use zero for both times. Consecutive loop IDs require touching intervals; overlapping or reversed intervals always reject, even with invalid-loop accounting. Omitted IDs before the first row (after accounting for warmup) and between rows require at least that many `invalid_loops`. Their elapsed gaps may be zero because an invalid loop can occur between repeated clock ticks. Such captures remain inspectable as partial diagnostics and cannot pass acceptance. Invalid counts may also include failures outside the retained envelope, so the parser does not require equality with the visible holes. Dropped loops occur only after full capacity and cannot explain holes within retained rows. Full capacity requires `truncated=true`, even with zero drops; below capacity, truncation requires invalid accounting for sequence exhaustion. Aggregate warmup/stored/dropped/invalid counts must fit uint64.

For each phase, zero calls require an empty duration, and positive calls require an integer duration. One call cannot exceed `wall_ns`; multiple calls cannot exceed `calls * wall_ns`. Same-phase nesting can legitimately exceed one wall interval in accumulated duration; phases are never summed to infer wall time. The core's nonblocking recovery from an injected clock reversal can retain later rows at a lower time origin; that export is rejected for overlap/reversal rather than relaxing the timestamp contract.

Required provenance may be explicitly `unknown` or `unavailable`, never fabricated; such a capture parses for diagnostics but cannot pass acceptance. Blank or whitespace-only metadata rejects. Known render/drawable resolutions must be positive uint32 `<width>x<height>`, vsync must be `on`, `off`, or `adaptive`, and frame limit must be uint32 (zero means uncapped); explicit unknown values remain diagnostic-only. `clock_is_steady=false`, nonstopped export, or inconsistent capacity/warmup/count/time metadata is rejected. Provenance syntax validation does not independently authenticate the supplied build or content hashes.

## Analysis rules

- Compute percentiles from raw `wall_ns`, sorted ascending, using nearest rank: for `N > 0`, percentile `p` is element `ceil(p * N)` in one-based indexing. No interpolation and no FPS conversion.
- Report `N`, first/last loop and monotonic times, p50/p95/p99, maximum, and strict counts where `wall_ns >` 16,700,000, 33,300,000, 50,000,000, or 100,000,000 ns. Equality does not count as a long frame.
- Warmup loops are excluded only by the declared start contract. Additional scene-class filters must be fixed before measurement and reported with the result; never remove samples because their latency is large. The implemented reporter supports `all`, `steady`, and each individual classification flag. Every report includes both `populations.all` and `populations.steady`, with the selected population also exposed at the top level. Each population includes loop/time bounds, percentiles, strict thresholds, per-flag counts, and separate per-phase loop/call/duration summaries. `sampled_wall_ns` sums retained selected intervals; `elapsed_ns` spans the selected first begin to last end, including any gaps or intervening excluded scene rows.
- Empty captures produce `N=0` and no percentile or passing performance verdict. Nonfinite values are invalid because the raw schema is integer-only. Truncated, dropped-loop, invalid-loop, or unprovenanced captures may be inspected as diagnostics but cannot pass an acceptance gate.
- Reports retain all applicable failure `reasons`, even when one `status` takes priority. CLI `--acceptance` exits 1 for a valid but nonpassing diagnostic report and 2 for malformed input; without that option, a valid diagnostic report exits 0. This gate validates capture completeness/provenance and the selected population, not runtime correctness or a performance target. An empty steady population is reported even when another selected population passes; use `--scene steady --acceptance` for that population's gate.
- RSS, load/save/reload/quit timings, logs, exit status, and screenshots remain separate harness evidence. CPU spans are not GPU timings, and visual correctness is not inferred from frame freshness.

## Initial acceptance and later gates

The first measurement gate is target-only, visible GL, fixed location/camera/resolution/settings, audio enabled with muted volumes, private appdata, and exact CoC 1.4.12 + 1.4.22 archive provenance with the explicit target GL overlay.

The runtime overhead gate remains open until a common external or separately gated reference can measure disabled and pre-change binaries over the same full-loop boundary. Those binaries cannot export this collector's distributions; do not invent comparable values from FPS, sparse bridge samples, or donor autotest. Capture-active overhead is assessed only after that reference method is validated.

Only after those gates pass, add separate movement and load-save-reload-normal-quit scenarios. Donor work remains blocked until an equivalent visible, steady-clock, start-to-start implementation exists; donor hidden autotest excludes primary frame work, present, secondary waits, and limiting and cannot establish a cross-engine advantage.
