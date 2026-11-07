# Windows transfer — runtime verification wrap-up

2026-11-07 · worker task `task_4fb44a3171e2` · source frozen at the eight-file snapshot approved in `artifacts/frame-time/task-1890b468aa80/reload-rereview.md`.

**Both owned processes exited normally; the exclusive runtime slot is released.** PID 16347 (early, 83.55 seconds) and PID 16783 (settled, 94.77 seconds) each acknowledged `quit` through its one retained bridge connection and exited **0**, about 1.04 and 0.99 seconds after ACK respectively. Final `pgrep -x xr_3da` returned 1/no process. No termination signals, timeout sampling, crashes, extra attempts, builds, or source edits occurred in this runtime task. No commit was made.

## Gates actually run

| Gate | Result |
|---|---|
| Reviewed 8 source / 6 binary hashes | Matched before and after both attempts; unchanged. |
| Fresh private staging | Two sibling runtimes from the exact documented CoC base + patch and target GL overlay; same default config, initialized muted sound. Only the known own `ft_b0c5_verified.scoc/.scop` save was copied, with original hashes verified. |
| Early menu-on → screenshot → original saved load | **Completed.** First screenshot is black, not visible-menu proof. Log contains one `MAIN_MNU factory did not create a main menu dialog`; execution continued to ready `l01_escape`, `paused=0`, advancing frames and movement. |
| Settled visible menu + advancing frames → same original saved load | **Completed.** Visible menu inspected after frames 708 → 894 → 1031; exactly the same `start server(ft_b0c5_verified/single/alife/load) client(localhost)` command. Reached Cordon; no reload-guard diagnostic in this attempt. |
| Movement/input recovery | **Passed in both.** Bridge-held movement changed actor position with advancing frames, including after each private reload. |
| Inventory/menu open-close | **Passed in both.** Screenshots inspected; menu states changed `paused=1` → `paused=0` on close. |
| Own save/reload | **Passed in both.** Saved/reloaded `ft_4fb4_early` and `ft_4fb4_settled` respectively; visible “Press any key to play” prompts were dismissed with targeted bridge Space, then `paused=0`, `loadscr=0`, `precache=0` and movement verified. |
| Normal shutdown | **Passed in both.** Send and ACK recorded separately; one connection per process, no reconnect or second cleanup client. |
| Failed attempts | None. The early attempt's handled factory/dialog rejection is retained as evidence, not hidden as a harness problem. |

The engine/GL screenshots show rendered UI/gameplay, not an OS-level occlusion or focus test. No other user apps/windows were controlled. Log scans retain missing logo/texture/config, duplicate-string/info-portion, OGG metadata, inventory-count, and GameSpy-unavailable warnings; no fatal/script-error/stack-trace scan hits were found. This is not a claim that the logs are error-free.

## Exact evidence paths

- Full runtime report, source/binary/settings/save/screenshot hashes, command/state/exit details: `/Users/rz/orca/opagxray/artifacts/frame-time/task-4fb44a3171e2/report.json` (SHA-256 `444011d4bd099a9b43ed768418f955b3e2cf5d404bcfcbb4a5f8b2ee0e170a7d`).
- Exact launch/control implementation: `artifacts/frame-time/task-4fb44a3171e2/driver.py`, `control.py`; staging commands: `stage.py`, `staging.json`. Each launch used `python3 -u .../driver.py early|settled`, spawning the reviewed `.../task-d09e1ea88ffc/bin/xr_3da -agent_bridge <private-external-socket>` with its own runtime cwd.
- Per-attempt sends/ACKs: `artifacts/frame-time/task-4fb44a3171e2/{early,settled}/events.jsonl`; ownership/exit: each `process.json`; complete stdout and engine logs: each `stdout.log`, `logs/`; warning scan: each `log-scan.json`; inspected images: each `screenshots/`.
- Settings/save/asset inputs: `input-early-user.ltx`, `input-settled-user.ltx`, `own-save-inputs.json`, and each attempt's `stage-manifest.json`; final private settings/files: each `final-user.ltx`, `final-private-file-hashes.json`.
- Freeze checks: `artifacts/frame-time/task-4fb44a3171e2/frozen-before.json`, `frozen-after.json`; no-process check: `process-exit-check.json`.
- Consistent reviewed GL binary set: `/Users/rz/orca/opagxray/artifacts/frame-time/task-d09e1ea88ffc/bin/`. Do not mix individual libraries with old engine/game modules: class layouts changed.
- Private runtimes retained: `/Users/rz/orca/opagxray/build/runtime/reload-4fb4-early` and `/Users/rz/orca/opagxray/build/runtime/reload-4fb4-settled`. Prior runtimes, original crash binaries/evidence, asset sources and user appdata were preserved.
- Prior implementation/build evidence: `artifacts/frame-time/task-d09e1ea88ffc/report.json`, `review.md` (40 CMake/ASan/UBSan cases, lifetime-ID checks, consistent GL build); independent approval: `artifacts/frame-time/task-1890b468aa80/reload-rereview.md`.

## Pending / not run

- **Dynamic-type/lifecycle cause remains unknown.** The early value-error path occurred and recovered, but its generic message does not distinguish null factory return from cast rejection, nor reveal the returned type. One early/settled pair does not prove causality or universal recovery.
- **Separate low follow-up: gamesave-screenshot registration balance.** The approved review documents duplicate `seqFrame` membership if a gamesave screenshot re-adds a level already present (notably multiplayer), then reload failure clears active before deferred removal. Not deliberately exercised or changed here.
- No audible sound validation: defaults mute effects/music. No audio-success claim from state or counters.
- No Windows build/runtime validation, long soak, performance/overhead campaign, provenance seal/attestation, or acceptance claim. No perf capture was started; FPS fields in control-state logs are incidental and support no performance conclusion.
- Coordinator owns checkpoint/transfer and any further review or Windows verification. Worker is stopped with no live game and no queued launch.
