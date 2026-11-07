# Runtime stability diagnosis: early menu crash and unacknowledged quit

2026-11-07 · task `task_ee7f793e5053` · dispatch `ctx_a446dc34388e`

The first attempt is a real engine crash with an established immediate null-dereference mechanism. The second attempt does **not** establish an engine shutdown hang: its cleanup opens a second connection while retaining the first, against a single-client bridge. Neither finding proves that the classification changes caused a regression. This study launched nothing, attached to no process, changed no source/config/runtime/build, and wrote only this report.

## Evidence and identity

Examined `task-28de519e371d/report.json`, both bridge transcripts and stdout logs, retained `smoke.py`, launcher record, seal result, vmmap, screenshots, build cache/flags and the IPS. Compared the successful `task-b0c500502419` process/bridge/capture records, screenshots and recorded source/binary/save hashes. Source references below were checked against commit `90c6e57e697ce4fa16c306ab9a09f298c825f342`; the relevant current files still match that commit.

- IPS SHA-256 verified: `072813af966cf7f8c78b323204c174b06d81c30e1db1ddec55adf3adeb342f07`.
- Retained executable SHA-256 verified: `6f5d390ab9e37fb80c2c3484130c0a8fc995e8072675d3fde9457f01055e0645`.
- `xrEngine.dylib`: `98b569cd423477f2eca65658b7fd13f239f4c71e9bdef8cdb1a9bf7c64d9a3a8`.
- `xrGame.dylib`: `2871489ef2f51e812fdeff13534680a0df3484e8ab7cf4e9fd6b7c92295b77bf`.
- `dwarfdump --uuid` matches IPS images: xrGame `A7276DAE-C4E3-3928-A19A-ADFFBEB0DA6D`, xrEngine `AADF7793-70BA-3498-82A9-80B60C712088`.
- Reported build identity: `git:90c6e57e697ce4fa16c306ab9a09f298c825f342,telemetry_sha256:49fcb676a98141a9ee6f03afd5cbf0d21447366ea3cb3faa2bdc6f84cecd6910,config:Release`. No runtime capture exists to cross-check this identity in-band.
- Retained retry harness SHA-256: `53238443e501e79d09702b1d86ac45a671fd67f6ca58662abdbc4038138bdf54`. It contains the retry UI flow, not the original crash-attempt implementation; the first attempt's exact requests survive in its transcript.

## Attempt 1: immediate failure mechanism is established

PID **72434** launched at Unix time `1794080897.897899`, from `build/runtime/frame-time-b0c5`, with the retained task binary and `-agent_bridge appdata/prov.sock`. The first recorded successful command is `cmd main_menu on`, returning at `1794080907.091447` after 1.7289 seconds. Then:

1. `shot ft28-menu` returned at `1794080907.1339571`.
2. `cmd start server(ft_b0c5_verified/single/alife/load) client(localhost)` returned at `1794080907.15605`.
3. `state` returned at `1794080907.917342`: `scene=game fps=44 frame=5 paused=1 loadscr=0 precache=0 actor=nil`.
4. Connection closed, process exited **-11 / SIGSEGV**; IPS records `EXC_BAD_ACCESS`, invalid address **0x78**, main thread.

The acknowledged start command proves dispatch to the console/deferred event path, not completed level startup. The actor-nil state is not readiness despite zero loadscr/precache fields.

The matching binary was inspected offline with:

```text
xcrun dwarfdump --uuid artifacts/frame-time/task-28de519e371d/bin/xrGame.dylib
xcrun llvm-objdump --disassemble --start-address=0x9c9f70 --stop-address=0x9ca090 artifacts/frame-time/task-28de519e371d/bin/xrGame.dylib
```

IPS frame 0 is `CMainMenu::ReloadUI()+228`, image offset **0x9ca054**. The relevant instructions are:

```text
0x9ca048  bl   ...              ; cast call, consistent with source/PURE_DYNAMIC_CAST
0x9ca04c  str  x0, [x19,#0xa8]  ; store m_startDialog
0x9ca050  mov  w8,#1
0x9ca054  strb w8,[x0,#0x78]    ; fault: m_bWorkInPause = true
```

IPS has **x0=0**, **x19=50667745792** (menu object), **x20=50680306992** (factory result retained across the cast), LR at the cast-return location, and an ESR identifying a byte-write translation fault. Thus this is not a null `this`, and not the already-handled null `NEW_INSTANCE` return. A non-null factory result fails the `CUIDialogWnd*` cast; the null result is dereferenced. `MainMenu.cpp:297–308` checks `dlg`, deletes the previous dialog, assigns `smart_cast<CUIDialogWnd*>(dlg)`, uses `VERIFY(m_startDialog)`, and writes the field. Release compile flags include `-DNDEBUG -DPURE_DYNAMIC_CAST -DXRAY_EXCEPTIONS=0`, without DEBUG; `xrDebug_macros.h:198` makes VERIFY a no-op. The disassembly confirms no release cast-result check.

The stack is:

```text
CMainMenu::ReloadUI
CEventNotifier<2>::CCallbackStorage::ExecuteCallbacks
CALifeSimulator::CALifeSimulator
 game_sv_Single::Create -> xrServer::Connect -> CLevel::net_start2
 CRenderDevice::BeforeFrame -> ProcessFrame -> CApplication::Run
```

`CALifeSimulator` calls `ai().RestartScriptEngine()` unless `-keep_lua` is present. `ai_space.cpp:152–169` fires `EVENT_SCRIPT_ENGINE_RESET` **before** `SetupScriptEngine`. The menu's reset callback (`MainMenu.cpp:72–83`) calls ReloadUI if `IsActive()`, otherwise destroys internals. This ties the fault to an active-menu/script-reset lifecycle path during real loading; it is not a collector sample failure.

### What remains a hypothesis

The cast failure is established; **why that factory object was not castable at that moment is not**. There is no core or recorded factory dynamic type, old/new dialog ownership trace, Lua factory return trace, or intro state. Wrong/partially initialized scripted UI, lifetime damage, and state/order problems cannot be separated from these artifacts.

Early startup/menu state is a justified repro variable. `IGame_Persistent::OnEvent(eStart)` attempts `main_menu off` before CreateLevel; the reset callback nevertheless took its active branch. `Activate` has early-return guards, and `GamePersistent` starts an asynchronous logo intro whose completion calls `Activate(true)` (`GamePersistent.cpp:396–424`). Intro reactivation or a refused/ineffective deactivation is plausible, but neither exact transition was logged. Do not claim an intro race, change callback order, disable Lua restart, or attribute the crash to telemetry on this evidence alone.

## Readiness and comparison with the successful run

`AgentBridge.cpp:537–539` defines `scene=menu` solely as **g_pGameLevel == nullptr**. It does not report `MainMenu()->IsActive()`, menu-dialog validity, or intro completion. `cmd main_menu on` returning OK also does not report whether Activate's guards allowed a transition.

| Dimension | Successful PID48003 | Crash PID72434 | Retry PID73498 |
|---|---|---|---|
| Initial menu evidence | First state at frame517, ~23 seconds after launch; explicit menu-on and separately retained visible-menu image | Menu-on, shot, direct saved-game start on first frames; actor=nil at frame5 | Four scene polls at frames1/30/61/92, then retained visible menu and Rookie-page response |
| Initial entry path | Ordinary Rookie/Loner/Rookie Village new game | Direct load of `ft_b0c5_verified` | Ordinary new game; later reload of newly saved `ft_28de_live` |
| Runtime state | Fresh sibling stage | Same runtime reused after previous run | Same reused runtime after crash |
| Exit handling | Request on a client without another retained connection; quit ACK, exit0 | Actual SIGSEGV -11 | New cleanup client while original remains open; no quit ACK; SIGTERM -15 |

The retry `ft28-menu.jpg` visibly contains the menu; `ft28-newgame-1.jpg` visibly contains the Rookie difficulty page. These support actual UI readiness for the retry **in addition to** its weak four-poll predicate. Both attempts used the same `ft28-menu` name, and only one retained image is present; it is not independent image proof of the crash attempt's initial state. Future attempts need distinct screenshot names.

All implementation hashes in the successful run's two source-hash groups match the files at `90c6e57e6`; only its measurement-plan hash differs. MainMenu, AI restart, persistent startup and bridge implementations also have no diff from the earlier checkpoint. Nevertheless, all five compared executable/engine/game/core/GL binary hashes differ between the runs; the newer build identifies itself as CI rather than Custom, with different embedded commit identity and isolated output path. This is not a same-binary controlled A/B. Both recorded configurations are arm64 Release, SDK15.4, unity ON, normal Darwin `XRAY_EXCEPTIONS=0`.

Both runs select renderer_r3 on Apple M4/GL4.1. The original runtime used generated private defaults; after normal shutdown it contains a full saved config. New startup logs additionally execute `rspec_default.ltx` and report some invalid multiplayer cvar values; cached-file counts differ (39772 vs39810, both 50 archives). No complete immutable prelaunch config/asset comparison proves these differences harmless or causal. The two current `ft_b0c5_verified` save files still match hashes recorded after the successful save/reload, but that is a current comparison, not a missing attempt-1 prelaunch seal. The failed seal is not a verified asset-equivalence result.

## Attempt 2: quit delivery is unproven, with a concrete connection defect

The retry reached ready Cordon, moved, saved/reloaded, and last answered at `1794081021.2244868`, frame854, ready/unpaused. vmmap completed at `1794081022.6031291`. Seal rejected the active `appdata/prov.sock` as nonregular at `1794081023.7425768`; no capture started. That scanner issue is separate work.

Cleanup then took ~10 seconds to `TimeoutError('timed out')` at `1794081033.743806`, waited another ~20 seconds, and SIGTERM produced **-15** at `1794081053.877086`. It was neither a second SIGSEGV nor a clean quit.

`smoke.py:287–303` creates `late = Bridge(..., timeout=10)` and calls quit on it, without closing the original local `bridge`; the retained script contains no `.close(` call. `AgentBridge.cpp:76–141` accepts a new connection only when `s_clientFd < 0`. Otherwise it polls/reads the existing client indefinitely; there is no idle disconnect. Thus an open idle first client prevents the second client's quit from reaching the server request queue in the ordinary execution path. A successful Unix socket connect/send can merely leave data on a pending connection; it does not prove application receipt.

The transcript records requests only **after a response** (`smoke.py::call`), and its single exception handler covers both construction and request. There is no separate connect/send/recv trace, quit response, server dispatch marker, or post-failure state request on the original client. We cannot prove the precise syscall that timed out, nor rule out a concurrent engine stall. But the retained connection ownership supplies a sufficient, high-confidence non-hang explanation. Do not label this an engine shutdown hang until quit delivery is established. `vmmap.txt` is a pre-timeout mapping inventory, **not a thread sample**; no timeout-time sample/spindump/core is retained.

## Smallest justified changes and next exclusive runtime plan

No fix was applied here. The established engine defect justifies evaluating a **release runtime check of the dialog cast before dereference**, returning failure as a value, clearing failed activation consistently and handling the factory-owned candidate through the proper destruction path. Validate ownership and failed-reload state before choosing exact placement; preserve the successful path and test null/non-dialog factory returns without exceptions. Simply adding another VERIFY, converting it to an unconditional abort, or swallowing the crash as a harness fault is not a remedy. Such a guard contains the observed null write but does not explain or repair the underlying factory/lifecycle failure.

For the harness, reuse the existing bridge for quit; if reconnection is necessary, explicitly close the old socket first. Log connection establishment, request ID/send and response separately. This is a targeted client-lifecycle correction, not a reason to move OnFrame or redesign production bridge scheduling.

Next authorized runtime slot should be a small **non-measurement** repro, after the adapter task releases the machine:

1. Preserve this crash/attempt evidence. Use the retained matching binary, fresh private appdata and controlled copies of the known save/config/assets, hashing all inputs before each attempt. Give every PID unique stdout, transcript and screenshot paths; retain loaded-image identity. Do not reuse user playtest data.
2. With one bridge owner, run one bounded original-sequence attempt: early menu-on → screenshot → the exact saved-game start above. Record command-stage timestamps and available frame state without silently turning it into the delayed UI route. Stop after one reproduced crash and retain its IPS plus matching binaries; no automatic crash loop.
3. Compare one settled-menu **same saved-game command** attempt, rather than changing both timing and entry path to new-game UI. Require a visible menu screenshot outside measurements and advancing frames with no level before start; scene=menu alone is insufficient. Existing Lua exports do not expose CMainMenu::IsActive, so this visual/frame gate is an operational precaution, not proof that every intro callback is finished or that the defect is fixed. Retain the ordinary UI route as a separate known-working control only if needed.
4. If the null cast recurs, the next narrowly authorized diagnostic should record the factory result's type/ownership and active/intro transition at reset, before destroying old UI. Do not change reset ordering on speculation. Use a value-return guard only with a focused regression and independent review.
5. On success or seal failure, send quit on the original connection, record its ACK, and wait a bounded interval for actual exit. If it does not exit/respond, retain a PID-specific short `sample`, thread stacks and socket ownership before terminating only that owned process. Distinguish “no connection/dispatch,” “dispatch without response,” and “ACK but shutdown stuck.” No timeout-time evidence means no confirmed hang location.

No performance/overhead conclusion or provenance acceptance follows from either attempt. The crash remains an open stability defect; the retry's cleanup delivery defect and missing hang evidence are separate findings.
