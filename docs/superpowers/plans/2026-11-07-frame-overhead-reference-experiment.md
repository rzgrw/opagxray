# Experimental common-probe implementation plan

Task `task_5feca4024e59`, dispatch `ctx_a19ef2b2e6da`. Uncommitted review material only.

Goal: prepare opt-in reference builds at baseline e3f273f04 and reviewed 456808010,
without running the game or changing runtime content. C++20 no-exception core,
POSIX export/completion adapter, Python stdlib strict parser and runner.

- [x] Test then implement a bounded common timestamp buffer. One nothrow allocation
  before stamp 0; exactly warmup + measured + 1 stamps; no clock/I/O disarmed.
- [x] Add opt-in bridge arm control and pre-pump boundary call. At closing stamp,
  finalize collector at that exact timestamp, stop and export after the window,
  notify once through an inherited pipe. Do not quit or change settings.
- [x] Test strict independent probe schema and paired five-cycle aggregation.
  Retain all rows including stalls; incomplete/invalid slots remain inconclusive.
- [x] Prepare runner lifecycle: private save load, readiness/camera check, seal,
  settle, recheck readiness, arm, blocking completion read with no bridge traffic,
  evidence copy/attest while alive, normal quit and exit-status check.
- [x] Build common same-config Release binaries in isolated directories; retain
  source diffs, core/callsite identities and binary hashes for review.

The parent owns independent review and provenance integration. No helper agents,
commits, runtime launches, GitHub writes, or provider calls are authorized here.
The task's explicit post-capture attestation supersedes the design's quit-first
wording. The current reviewed ref supersedes its a389540d8 checkpoint.

Implementation decisions: no optional limiter byte; report the 501 integer limiter
limitation. A separate parser never routes probe CSV through the full collector
parser. Asset verification must be adapted explicitly; missing adapter fails
closed. Loaded-image verification remains an external attestation requirement.


Verification: 78 Python tests pass; CMake probe tests on both source revisions,
collector boundary integration test, three existing collector/session tests and
ASan/UBSan probe/session tests pass. Three same-config Release GL builds complete.
No game/runtime launch or live provenance claim. Final exact hashes, patches,
build logs and remaining gates are in
`artifacts/frame-overhead/task-5feca4024e59/report.json` and `report.md`.

Observed and fixed before handoff: asynchronous arm response now has a pre-window
pipe fence; Release unit tests explicitly keep assertions; connection retries
close failed sockets; actual macOS hw.model commas are accepted and the runner
checks the declared machine against sysctl. No independent reviewer was spawned
because this dispatch explicitly prohibits helpers; parent owns review.
