# ReloadUI repair re-review

2026-11-07 · task `task_1890b468aa80` · dispatch `ctx_f9cfb7d573ed`

**Approved.** The previous blocker is closed in this source. A failed reload now reverses only the pause, console, cursor, and registry effects that this activation actually took, and `Activate(false)` is not required for that cleanup. One low edge remains around a gamesave screenshot that has already re-added a level the menu left on `seqFrame`.

Base `90c6e57e697ce4fa16c306ab9a09f298c825f342`. Full patch `95f93411c7e8d71efa42e259e89599c16b244ff689496d96aea15a7a2c5f197d`. Repair patch `807276a2b5decc646f5870e65b9f22d8de43085d799afc15d49b01116a5e41ec`. The eight source hashes below matched `artifacts/frame-time/task-d09e1ea88ffc/report.json` before the reruns and again after them. Provenance files were left untouched. No production source, test source, author artifact, or experimental worktree was edited.

| File | SHA-256 |
|---|---|
| `src/xrEngine/IGame_Level.h` | `2abf2b31678da7905b2e8c418a7aedcb434352369db06b22a6a442ef35109ec3` |
| `src/xrEngine/IGame_Level.cpp` | `af43a1b37deaef50169641ebe77dce341974b4519db22e28bfa043734fc28947` |
| `src/xrEngine/pure.h` | `56df27a226dcae385b00a6405b968e1305336a6fc0f9e4b4a679737c977d406c` |
| `src/xrGame/MainMenu.cpp` | `9f70f406805fc201a29c840432504db8a8dfd427baaa0552f147ac559129eb99` |
| `src/xrGame/MainMenu.h` | `69c3076f33f8b0c410e7920591268482dbd70ca5b879c7c5fb039091e7dc70cc` |
| `src/xrGame/MainMenuReload.inl` | `ca696342b9d47ba69823341c80b7800680c6fdfb70a4f3c8f5d4d7d9f08e35f1` |
| `src/xrGame/tests/MainMenuReloadTests.cpp` | `491ea53570ad2e76b83d605fe093cd1a6feb2caacd311b6c39f281078493cb79` |
| `src/xrGame/CMakeLists.txt` | `569c726ffdf52526a295446b78eb08fc38a21480cde47b6fb0b0f4dd4349eac0` |

## Blocker closure

`ReloadUI` still rejects a null or failed cast through `DEL_INSTANCE` on the factory pointer and `xr_delete` on the old dialog. It then calls `DeactivateUI` (`MainMenuReload.inl:14–23`). That helper clears `flActive`, `flNeedChangeCapture`, and `flNeedUIRestart`, so the public `Activate(false)` equality and screenshot guards cannot swallow the cleanup (`MainMenuReload.inl:140–146`). A second `Activate(false)` returns immediately and does not unpause or unregister again.

`Activate(true)` records effects as it reaches them (`MainMenuReload.inl:58–105`):

- Sound pause is the call before `ReloadUI`. `soundPaused` follows the same benchmark and `GEnv.Sound` condition that `CRenderDevice::Pause` uses. Cleanup unpauses with `Pause(false, false, true)`, so the timer flag is left alone.
- Timer pause happens only after a successful reload, only for single-player, and only when the device was not already paused. `timerPaused` is the post-call `Device.Paused()` value, so a rejected pause or an external pause is not released later.
- Level removal is also after success. The id is copied from the current `g_pGameLevel`; the struct keeps no level pointer (`MainMenu.h:55–65`). Frame removal is recorded only when this activation is single-player and `seqFrame.Contains` is true. Render removal uses `seqRender.Contains`. Cleanup adds a list back only when that list was removed, the live level's id matches, `bReady` is true, and `Contains` is still false (`MainMenuReload.inl:162–171`).
- Menu render registration is one `seqRender.Add(this, 4)` after those steps. Cleanup removes the menu only when that add happened. `Remove` drops every entry for the pointer.

Initial failure therefore releases the one sound pause and restores the pause label and cursor, and it does not invent a timer or registry change that has not happened. An active reset or frame failure runs the same helper with the effects already recorded, puts back the level that this activation removed, drops the menu render entry, and releases one sound pause and the timer pause this activation acquired. `UpdateUIRestart` sends `MAIN_MENU_RELOADED` only when `ReloadUI` returns true.

`MessageRegistry::Contains` is const, allocates nothing, and ignores `REG_PRIORITY_INVALID` (`pure.h:45–51`). A level re-registered by another path is left as the one valid entry. A replacement constructed at the same address gets a new id from `AllocateLevelLifetimeId` (`IGame_Level.cpp:18–33`), which starts at 1, never returns 0, and aborts instead of wrapping. Copy and move of `IGame_Level` are deleted (`IGame_Level.h:102–105`). A ready level that this activation did not remove is not inserted by cleanup, so its own `Load` registration stays the only one (`IGame_Level.cpp:174–179` sets `bReady` and then adds). `g_pGameLevel == nullptr` skips the id read.

Successful open still hides the console, pauses a single-player timer that was running, registers the menu once, and leaves `flNeedChangeCapture` set so the next `OnFrame` captures. Successful close goes through `DeactivateUI` and restores those effects. An external timer pause stays paused. That differs from the old close, whose final `Pause(false, true, true)` always cleared the timer; the new pairing is the preservation the tests lock in. Input release is once inside `DeactivateUI`, and the capture flag is cleared, so the following `OnFrame` does not release again. The permanent dummy receiver still makes a top-of-stack pop safe; a covered or absent menu is unchanged from `CInput::iRelease`.

The dialog pointer is null before `DeactivateUI` on the failure path, and the hide is guarded by `m_startDialog` (`MainMenuReload.inl:158`). Success still hides and cleans without deleting the dialog, matching the old close.

## Low — gamesave screenshot can leave a duplicate level frame entry

`CMainMenu::Screenshot` adds the level to `seqFrame` and `seqRender` whenever the menu is active, with no `Contains` test (`MainMenu.cpp:439–442`). The matching remove runs on a later frame only while `flActive` is still true (`MainMenu.cpp:398–401`). A reload failure clears `flActive` before that frame.

Single-player in-game open removes the level first, so the screenshot add leaves one entry and cleanup keeps that one. Multiplayer open does not remove `seqFrame`. The screenshot add then makes a second frame entry. After the failure, cleanup will not remove it, and the deferred screenshot remove no longer runs. `Process` will call that level twice per frame until some other `Remove`. The same window exists if a single-player level was absent at activation, registered itself, and then a gamesave screenshot added it again before the failure. The diagnosed load fault happens before `Load` registers the level and does not take this screenshot path.

## Checks

`bin/xrMainMenuReloadTests` (`1244bca3d9d2f8cd4f9a12eef5e795a0f2035c6a29b1a9c1aa14b50c9d37d285`), `final-green` (`46d00decfda52cfbec1c92850c67e1902c56a882ecc928711719f1224d9b5ec6`), and `sanitized-final-tests` (`b8dfc14f27b610904c9d00521eee233e4ed37d8e4ed42eab1d5546d42e6524c6`) each exited 0 with the same 40 `PASS` lines. The production lifetime binary (`62f97c4ef0c1b72e4a17a3fedc02c87aa34904e6fb8b07e521bef33f63560a7c`) passed its constructor, same-address, copy/move, and 512-id checks. `exhaust` exited 134, which is `SIGABRT` from `std::abort`; the author's Python harness records that signal as `-6`. Logs are in this directory. No `xr_3da` rebuild: the reviewed behavior is in the eight sources and these focused binaries.

## Remaining limits

Live Lua, ALife, the intro, SDL callbacks, real emitter playback, and a running level were not exercised. The original failed-cast dynamic type and the release null-client write in `xrFactory_Create` stay outside this guard. The lifetime harness compiles the production allocator, constructor, and identity declarations with camera, spatial, audio, and render stubs; it is not a live `CLevel`. `Device::Pause` still shares one process-wide sound counter. The level and menu layouts changed, so a runtime slot has to use this isolated binary set.
