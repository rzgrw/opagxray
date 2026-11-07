# Common-reference preparation review

**Verdict:** Approve this preparation snapshot for a later provenance adapter and a live campaign. This is not a mapped-image result, a game run, or a performance result.

**Base HEAD:** `e3f273f04bf85b4b265004cbbc8a35f19796cd31`
**Reference HEAD:** `45680801007e8c5a3fab7555eb4e4ed17a35ffe3`
**Design SHA-256:** `36ff35a26765a863d846b09b8bc30460b15fc857d443f7aea88ec6e470ccb4f2`
**Probe core SHA-256:** `4ff8d18a809c884866d991c3b2a161e87da826d381de910e28b794216623fc8a`
**Base callsite SHA-256:** `678e168be22319f0f42e20a5e561ceeaa05db29910b0172b8907346c8965be9c`
**Off/on callsite SHA-256:** `847f1be2658a4c522a5127766b5b72b468ca20343c0f956e1e6b5b59e69d90f7`
**Base source snapshot:** `712c21360bd52c209451e277d5e936bbe2808a092c7a6b10cdce4494573d9adb`
**Off/on source snapshot:** `e1e14726380a3fa0bd1e710d41e2175f097f8e0d1aab82aaf7455c903d03b198`

`review-base.patch` SHA-256 `d54c1f91074910e72fabcfb1ed57a814569302a3414a17c7915ead3b9bf86b9f` matches the baseline worktree. `review-off.patch` SHA-256 `e7d98539035c74d7c7306aa42de2787b7a5d129f4ef50ecb7b04f088edc2d046` matches the reference worktree. Both `git apply --reverse --check` runs succeeded. Author patches and `task-5feca4024e59` evidence were left unchanged.

## Identity

`OverheadProbe.h` and `OverheadProbe.cpp` are byte-identical in both worktrees. Re-running `prepare_overhead_identity.py` into a new temp directory reproduced the role, revision, core, callsite, and source values embedded in each build. Every file hash and size in `report.json` matches the built images, including:

| Arm | xr_3da | xrEngine.dylib |
|---|---|---|
| base | `d0bf129891f2b8017876cbca4eb477b131c30d1a534a1243fc05f10079786074` | `f68e44b42fe73bde9087df7bc4de51168b1f04cbe2f89e80bb53f08cb41cc37b` |
| off | `3dfa7f90dc4644e4c9f9f6fed71a39c32f9dbdc2612fc0df650f396bb1237ba9` | `27c38db328f4e47c9dcdd8bed436ce8dffbe16adc75eac59b30592a2d7894439` |
| on | `c65826cca19b7a12bf134bc171d05de173895decdf7e594e37b41db130ac6202` | `6d333875c69a37686a81c6cc47c7a6599da18e44bc469a9ed33322eb7ce32fe5` |

Those strings are present in the matching `xrEngine.dylib`. `XR_OVERHEAD_CAPTURE` is 1 only for on. The string `collector_start_failed_process_unusable` is present only in the on image.

## Control boundaries

`Boundary()` is the first call of the outer loop, before `BeginLoop` on the reference builds and before the frame body on the baseline. `overhead.arm` runs later, from main-thread `OnFrame`. `Respond` queues the bridge line, and `AwaitRunner` then blocks that same iteration on `XR_OVERHEAD_START_FD` for up to 30 seconds. The socket thread can flush the queued reply while the main thread waits. The next loop records stamp 0. The fence wait sits in the preceding control iteration.

Probe capacity is `warmup + measured + 1`. The closing `Boundary()` records that last stamp, disarms, and only then exports and writes one completion byte. Disarmed `Boundary()` returns before the clock. A fresh compile of the probe tests, plus the author's probe binary, both passed.

On `Control` calls `CFrameTimeSession::Control("perf.start", "<measured+1> <warmup>")` after the probe arms and before the fence returns. Off `Control` calls `GetStatus` and `Probe::Arm` only. Disassembly of off `Boundary` has no call to `ExperimentalCloseAt`; on `Boundary` does. The collector test, rerun here, stores 3 loops in capacity 4 after warmup 2, with `truncated` clear and `capture_end_ns` equal to the supplied close stamp. The close path finalizes the last measured interval at that stamp, stops, and the following `BeginLoopAt` on `StopPending` discards the newly opened interval without storing it. The ordinary `BeginLoop` after a successful close sees the session stopped.

Export runs after disarm. Publication is `O_EXCL` plus `linkat` without replacement. A failed publication keeps the temporary name and completes with byte `0`.

## Parser, runner, and checks rerun here

`tools.tests.test_frame_overhead`: 11 tests, OK. Unknown roles, a target role with the baseline revision, an off file with collector rows, an on file whose capacity equals its row count, duplicate slots, and mismatched probe cores are rejected. A 9-second final interval stays in the scored population. The author's later Python log reports 78 tests OK; an earlier log in the same directory reports 77, before `test_nearest_rank_and_paired_median_are_not_conflated` existed. This review did not rerun the other 67 tests.

Rerun C++ binaries, all exit 0: base and on `xrOverheadProbeTests`, on `xrOverheadCollectorTests`, on collector, boundary, and session tests (session outputs under `/tmp/oh-review-25cd21a2f0a7`), plus `probe-sanitized` and `collector-sanitized`. Off has no separate probe-test binary; the probe sources are the shared ones exercised above.

`frame_overhead_runner.py --run` with a config that names the on `xr_3da` and no `--adapter` exits 2 with `probe provenance adapter is not implemented` and does not start that executable. A one-second fixture timeout returns `completion timeout; retain owned live process for operator recovery`, records `process_alive` true, leaves the fixture settings file unchanged, and does not send `cmd quit`. An existing evidence directory is rejected with `FileExistsError` before launch. No `xr_3da` was started.

## Bounds that stay open

`campaign_accepted` remains false even when thresholds pass. A campaign whose hashes are internally consistent and not the prepared snapshot can still report `timing_pass`. The live runner binds the CSV to the configured identity and binary hash. The standalone parser checks shape, pinned revisions, collector pairing, and campaign conditions; it does not carry an allow-list of the built inventory.

`tools/frame_overhead.py` and `tools/frame_overhead_runner.py` are outside the embedded source snapshot. The snapshot pins the compiled tree and `prepare_overhead_identity.py`.

The off image still contains uncalled `ExperimentalCloseAt` and the ordinary `perf.start` bridge verb. The experiment arm path does not call it. A `perf.start` injected on that socket would start the collector; the probe parser then rejects a non-zero off collector header.

Base and off probe rows are `loop_seq,start_ns,wall_ns` only. Readiness checks are bookends. They do not show a flag that appears and clears inside the window. The integer limiter is unchanged: `updateDelta = 1000 / ps_fps_limit`, and at 501 the sleep runs only for a 0 ms span, with argument 1. Large and short walls are retained. There is no limiter-byte subset.

The asset adapter, loaded-image binding, private-runtime game lifecycle, and the five-run campaign are still open. Author builds were not repeated.
