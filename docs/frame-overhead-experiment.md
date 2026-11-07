# Isolated common-probe experiment

Task `task_5feca4024e59`, dispatch `ctx_a19ef2b2e6da`. This is uncommitted review
material. No game was launched and no overhead result is claimed.

Sources are pinned to `e3f273f04bf85b4b265004cbbc8a35f19796cd31` (`base`) and
`45680801007e8c5a3fab7555eb4e4ed17a35ffe3` (`off`, `on`). The latter is the reviewed
classification revision requested by the coordinator, superseding the design's
older `a389540d8`. The exact design was read from the main worktree; main was not
edited. The parent owns independent review, provenance adaptation and runtime.

## Isolation and build

Owned worktrees, both retained for parent cleanup:

- `/Users/rz/orca/workspaces/opagxray/frame-overhead-reference`, branch
  `rzgrw/frame-overhead-reference`, reviewed target HEAD above.
- `/Users/rz/orca/workspaces/opagxray/frame-overhead-base`, branch
  `rzgrw/frame-overhead-base`, baseline HEAD above. Created with Orca; full creation
  receipt is in `artifacts/frame-overhead/task-5feca4024e59/baseline-worktree.json`.

All builds and binaries are under the first worktree's
`artifacts/frame-overhead/task-5feca4024e59/{build-,bin-}{base,off,on}`.
No binary was installed in a runtime. Dependencies were initialized at their
existing gitlink revisions, including nested dependencies; no upgrades.

From the reference worktree, configure each role with its own output directory:

```sh
role=on
source_dir="$PWD" # for base: /Users/rz/orca/workspaces/opagxray/frame-overhead-base
artifact_dir="$PWD/artifacts/frame-overhead/task-5feca4024e59"
cmake -S "$source_dir" -B "$artifact_dir/build-$role" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DMEMORY_ALLOCATOR=standard -DXRAY_METAL=OFF \
  -DXRAY_OVERHEAD_PROBE=ON -DXRAY_OVERHEAD_ROLE="$role" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE="$artifact_dir/bin-$role" \
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE="$artifact_dir/bin-$role"
cmake --build "$artifact_dir/build-$role" -j10
```

Repeat for `off` with the same source and `base` with the baseline worktree.
`XRAY_OVERHEAD_PROBE` defaults OFF. Without it no reference source, bridge command
or loop call is compiled. Production exceptions policy stays `XRAY_EXCEPTIONS=0`.
Focused new CMake tests use `-UNDEBUG -fno-exceptions`, including in Release;
they refuse compilation if assertions are disabled.

Always reconfigure after source edits. `tools/prepare_overhead_identity.py` rejects
wrong HEAD, missing/unpinned submodules and tracked dependency modifications.
`source-identity.json` under each build's `src/xrEngine/overhead/` names the source
files actually hashed and pinned submodules. This is a source snapshot, not loaded
image evidence. `probe_core_sha256` is SHA-256 of `OverheadProbe.h` followed by
`OverheadProbe.cpp`; both files are identical in both worktrees. The separately
hashed `callsite.patch` is the diff from each pinned HEAD for `x_ray.cpp`,
`AgentBridge.cpp`, `FrameTimeSession.{h,cpp}` when present, and engine CMake.
The new common adapter and CMake/script bytes are covered by the source snapshot.

The runner also checks the declared machine against actual `sysctl -n hw.model`
(including its comma, such as `Mac16,12`).

The main executable hash is supplied by the runner after hashing the executable
it launches. It is not a self-hash embedded into the binary. All mapped non-OS
images, especially `xrEngine.dylib`, must be independently bound by the provenance
adapter. Built-file hashes and configured rpaths are not a claim about loaded
images. There is no live image inventory yet.

## Capture lifecycle

`overhead.ready` is an experimental bridge query for readiness, effective SDL
swap interval/limit, render resolution, configured renderer mode and camera
position/direction. The runner also checks ordinary bridge `state` for level,
loading, precache and pause. It loads an explicitly named private `.scop` save,
checks the scene/camera, seals, waits the declared settle time and rechecks.
No screenshot, key or mouse request is part of this runner.

The single arm command is identical across roles:

```text
overhead.arm <warmup> <measured> <run-index> <scene> <save> <level> <main-executable-sha256> <user-ltx-sha256> <machine>
```

Defaults are 120 warmup intervals and 4096 measured intervals. The one nothrow
allocation holds exactly warmup + measured + 1 timestamps. Maximum combined
intervals is 131071. One capture per process; no reset/reuse. The core's disarmed
branch has no clock, allocation, I/O, input or networking. Armed boundaries take
one steady-clock reading and write one timestamp, with no wrap or variance filter.

The existing bridge replies asynchronously. `XR_OVERHEAD_START_FD` is an inherited
pipe read end: after queuing the arm response, the main thread stays in the
excluded control iteration until the runner receives the response and writes `G`.
The wait has a 30-second timeout, outside the stamp window. The next outer loop
is stamp 0, immediately before ordinary `BeginLoop`, SDL pump and window events.
There are no new input/network/readback operations inside the timestamp window;
the engine's existing bridge thread and game input paths are otherwise unchanged.
The runner holds the bridge connection idle and waits only for output completion.

`on` alone starts the collector at control with capacity `measured + 1` and the
same warmup. On the closing reference stamp, the experimental session method
finalizes the last collector interval with that exact supplied timestamp,
discards its newly opened control interval and stops immediately. This avoids a
collector timestamp beyond the reference envelope and avoids saturation at exact
capacity. The ordinary following `BeginLoop` is disabled. `off` remains disabled;
`base` has no collector.

After the final boundary, auto-disarm is already effective. Export then publishes
`appdata/perf/overhead-<pid>.csv` through `O_EXCL` temporary creation and atomic
no-replace `linkat`, then removes the temporary name. This deliberately uses the
POSIX no-overwrite publication primitive instead of overwriting `rename`.
A failed export retains its temporary evidence and reports failure. The one
completion byte on `XR_OVERHEAD_COMPLETION_FD` is `1` for successful publication or
`0` for failure; no per-frame notification exists. No quit or settings rewrite
is triggered by completion.

The runner parses/copies the CSV, explicitly attests while the process is alive,
checks post-capture state, then issues ordinary bridge `cmd quit` and requires
exit 0. It records the selected save's hash at both bookends. A completion timeout
is an invalid run and leaves the owned PID for coordinator recovery rather than
sending input into an uncertain window. This failure-recovery path and all real
engine/pipe lifecycle behavior still require runtime validation.

The regular telemetry session is not exported by this experiment; its retained
records supply the bounded diagnostics in the probe CSV. Thus `on` can emit the
existing shutdown warning about an unexported regular capture. Do not mislabel it
as an active probe or missing reference export.

## Independent schema and provenance adapter

`tools/frame_overhead.py` parses **only** `xray-overhead-probe-v1`. It requires all
design metadata plus `source_snapshot_sha256` and explicit collector rows,
capacity, observed warmup, invalid/dropped counts, flags, not-ready calls and
start/end envelope. Unknown, duplicate or missing keys, bad integer/clock fields,
nonpositive/noncontiguous walls, wrong counts, wrong roles/revisions, collector
saturation/contamination or envelope mismatch fail closed. Warmup rows stay in
the raw CSV and are excluded only from the scored population. Raw asset fields
remain `content_manifest=unknown overlay=unknown`.

The existing full telemetry parser is incompatible and is never called here.
The main-repo provenance author is exposing reusable asset verification helpers;
the probe adapter is now implemented in `tools/frame_overhead_provenance.py`
(task `task_2ef5891b21eb`). `frame_overhead_runner.py --run ...` still
refuses to launch without an explicit adapter. Importing the runner or invoking
it with no flags only emits the predeclared 15-slot rotating order. It performs
no launch and no asset verification.

Adapter module protocol for the later owner:

- `schema = "xray-overhead-probe-v1"`.
- `seal(context)` returns `result="verified"`, `source_identity_verified=true`
  and `loaded_images_verified=true` only after doing the actual checks. Context
  contains the exact launcher record path, live PID, source identity JSON, config
  and private save hash. It must compare current source bytes and pinned refs to
  the generated identity, and bind the actual mapped images to this build.
- `attest(context, capture_path, evidence_path, seal)` rechecks assets/settings,
  source identity and loaded images while alive, invokes this strict probe parser,
  preserves both raw files byte-for-byte, and returns
  `format="openxviberay-overhead-provenance-v1"`, `probe_schema` above,
  `result="verified"`, both identity booleans above and `raw_capture_sha256`.
- Shared asset verification is permitted; skipping either schema or image/source
  checks is not. A filename, hash declaration or plain `verified` is insufficient.
  The new adapter implements these checks using the pinned dependency snapshot;
  independent review and its live probe lifecycle remain pending.

The fixture adapter used by unit tests attests only disposable synthetic files
and a fixture process. Those tests are not asset acceptance or game evidence.

## Aggregation and remaining gates

Five predeclared processes per role: `base,off,on`, then `off,on,base`, then
`on,base,off`, rotating for five cycles. No shader warm launch is currently
predeclared; add it explicitly to a campaign record before launch if desired.
`python3 tools/frame_overhead.py <selected CSVs...>` reports every run, nearest-rank
p50/p95, min/max, pairwise deltas and median paired deltas. Gate deltas are separately
computed as differences between medians of the five run statistics, as specified
by the design (these can differ from the median of paired differences).

Disabled thresholds: p50 `max(0.05 ms, 1% of base)` and p95 `max(0.10 ms, 3% of
base)`. Capture-active thresholds: 0.10 ms / 0.25 ms. Missing slots are inconclusive;
duplicate slots require explicit rerun selection. No long or short frame is
removed for variance. All attempts, including invalid ones, must be kept by the
campaign owner. Timing aggregation never grants campaign acceptance: verified
probe provenance and full-window functional/log/traffic/exit checks remain
independent gates. The base/off probe has no full-loop classification, so ready
bookends alone cannot rule out transient contamination; do not claim otherwise.

These are conditional common-probe wall deltas, not a bare baseline, GPU timing,
absolute probe cost or isolated CPU overhead. There is no limiter-byte subset.
The unchanged integer limiter uses `1000 / 501 == 1`, sleeping 1 ms only when the
integer work span is zero; added work crossing that bucket can remove the sleep
and reverse a small wall delta. Swap interval 0 is the SDL observation and cannot
distinguish disabled from indeterminate on its own. No improvement is claimed.


## Probe adapter follow-up (task_2ef5891b21eb)

Use `--adapter tools/frame_overhead_provenance.py`. The implementation and exact
trust boundary are documented in `docs/frame-overhead-provenance-adapter.md`.
It snapshots the approved provenance/staging/report/template dependencies at
90c6e57e697ce4fa16c306ab9a09f298c825f342, pins the previous three-arm build inventory,
and never passes a probe CSV to the regular reporter. The C++ probe, callsites,
identity generator and prepared binaries are unchanged; no rebuild is required.
The adapter writes `adapter-seal.json` and `adapter-attest.json` with exclusive
creation, separately from the runner's `seal.json` and `provenance.json`.
The runner now requires live, complete, unexpired results and rejects fixture
observations. The fixtures remain test evidence only; no live probe attestation
or overhead campaign has run in this follow-up.
