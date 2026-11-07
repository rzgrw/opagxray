# Windows development setup

Audited on the development machine (Windows 11, checkout `C:\Users\izeon\orca\opagxray`) on 2026-11-07. **First Windows build and playtest: 2026-11-08** — x64 Release via MSBuild (`src/engine.sln`), CoC 1.4.22 played on GL (`renderer_rgl`, GL 4.1 context, RTX 3080 Laptop); user verdict "playtest OK" with two crashes found and fixed (see [Known Windows issues](#known-windows-issues)). The CMake + Ninja route is still unverified. Transfer context and archive-tag recovery: [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md).

## Toolchain

State at the start of the audit: no Visual Studio/MSVC, MSBuild, NuGet, CMake or Ninja; Python absent (only Microsoft Store alias stubs, no `py`); git 2.56 present; all 9 submodules uninitialized.

Re-checked at 23:05 the same day, after installs had begun:

| Tool | Requirement | Found |
|---|---|---|
| Visual Studio 2022 "Desktop development with C++" (MSVC v143 + Windows SDK) | required | VS 2022 Build Tools 17.14, MSVC 14.44.35207 (`cl.exe` Hostx64/x64), Windows SDK 10.0.26100.0 |
| CMake ≥ 3.23 | required | 3.31.6-msvc6, bundled with Build Tools (on `PATH` only inside a Developer shell) |
| Ninja | required for the recommended route | 1.12.1, bundled with Build Tools (Developer shell only) |
| Python ≥ 3.11 with the `py` launcher | required (the stager uses `hashlib.file_digest`) | 3.15.2 via the Python Install Manager; `hashlib.file_digest` present |
| NuGet | only for the MSBuild route | absent at audit; installed via winget for the 2026-11-08 playtest build |
| git | required | 2.56.0.windows.4 |
| Submodules | required | still all 9 uninitialized |

Verify from a **Developer PowerShell for VS 2022** (x64):

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cl            # prints the MSVC banner (19.4x for v143)
cmake --version   # >= 3.23
ninja --version
py -0p            # lists installed Pythons; need 3.11+
py -c "import hashlib; print(hasattr(hashlib, 'file_digest'))"
git --version
```

Install Python from python.org or the Python Install Manager, and make sure `py` works. The `python`/`python3` Store alias stubs do not count. Use a short local NTFS path and avoid OneDrive-synced folders.

## Submodules

```powershell
git config core.autocrlf false
git submodule update --init --recursive
git submodule status --recursive   # no leading '-' entries
```

Initialize the exact recorded gitlinks, not their latest branches.

## Build route

**Verified: MSBuild / `src/engine.sln`** (2026-11-08, `dev` from `bec540223`). `4555aa76a` added the CMake-only `xrEngine` sources (`AgentBridge`, `FrameTime*` with `ENGINE_API` export, `xrSim/*`) to `xrEngine.vcxproj`; `bec14accc` and `bec540223` fixed the two MSVC compile errors. xrSim builds without `XRAY_AGENT_HTTP_CURL` (no curl on Windows yet), so the live HTTP provider is unavailable. Do not add `OverheadProbe*`/`OverheadExperiment*` to the vcxproj; they belong to the off-by-default `XRAY_OVERHEAD_PROBE` CMake option.

```powershell
# Developer PowerShell for VS 2022, x64; output: bin\x64\Release\
nuget restore src\engine.sln
msbuild src\engine.sln -m -p:Configuration=Release -p:Platform=x64
```

Gotcha: Claude Code's shell sets `NoDefaultCurrentDirectoryInExePath=1`, which makes xrCore's pre-build `call .GitInfo.cmd` fail. From that shell run `env -u NoDefaultCurrentDirectoryInExePath MSBuild.exe src/engine.sln -m -p:Configuration=Release -p:Platform=x64`.

New CMake-only sources must also be added to the matching `.vcxproj` until one route is retired.

**CMake + Ninja (unverified).** The canonical route on macOS/Linux and the only one that knows the standalone test targets (`tools/xrsim_standalone` builds and passes under MSVC; the full engine has never been configured on Windows). The Windows CMake CI entries are commented out at `.github/workflows/cibuild.yml:93-94`, so this route has no CI history either. Expect dependency discovery and MSVC-specific fixes on the first configure. Keep the first real compiler/linker log.

```powershell
# Developer PowerShell for VS 2022, x64
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Rules either way:
- Rebuild all engine/game modules together: `IGame_Level` and `MainMenu` layouts changed in `604fb0690`.
- Never mix stock CoC `bin/` DLLs (`xrGame.dll`, `xrCore.dll`, `xrRender_R*.dll`, …) with repo-built modules.
- Do not change the Windows exception policy, or force `-fno-exceptions` globally, to work around port errors. `XRAY_EXCEPTIONS=0` is a Darwin-only rule.

## Game data

`C:\Users\izeon\Downloads\stalkercocassets` (8.1 GB) is the only CoC copy on this machine. It is a **single pre-merged stock install**: most likely the CoC 1.4.12 base with 1.4.22 patch content merged in. `database/patch/` has `xpatch_meshes/sounds/textures.db`, and `database/config/{configs,scripts}.db`, `xr.db` and the xpatch files carry transfer-time mtimes, while untouched archives keep their 2016 dates. Its `bin/` holds stock DX-era engine DLLs.

It is not direct stager input:
- `tools/stage_coc_runtime.py:192-193` requires separate `--base` and `--patch` directories.
- `tools/stage_coc_runtime.py:103-106` requires `patch/database/config/{scripts,configs}.db` in the patch tree.
- It has not been compared with the Mac stage manifests. Tag `archive/transfer/windows-2026-11-07` (`docs/transfer/2026-11-07/runtime-report.json`) records only their SHA-256; the manifest files themselves stayed on the Mac.

Keep this directory immutable. Obtain the separate 1.4.12 base and 1.4.22 patch archives, or split the install deliberately and record how, before staging. The 2026-11-08 playtest staged it with `--base <dir> --patch <same dir>` into the private runtime `build\runtime\coc` of the playtest worktree (main menu shows 1.4.22). That runs, but it voids the stager's base/patch precedence and provenance meaning, so do not use such a runtime for measurements. On Windows set `renderer renderer_rgl` in `user.ltx`: `renderer_r3` loads DX11 there. The GL shader overlay comes from the repository's `res/gamedata/shaders/gl`. Decide GL or DirectX explicitly; never launch a DirectX renderer with the GL-only overlay and call the runs equivalent. Keep user and test appdata separate, and never share IXRay saves.

## Agent bridge and telemetry on Windows

- `src/xrEngine/AgentBridge.cpp:6` wraps the whole bridge in `#ifndef XR_PLATFORM_WINDOWS`; `g_agent_bridge` is defined once for all platforms at the top of the file (a duplicate in the old Windows branch was removed in `bec14accc`), so on Windows the bridge is a stub. Without it, GUI automation fell back to foreground-window + `mouse_event`/`keybd_event` input; Orca synthetic clicks and `SetCursorPos` are ignored by SDL.
- `tools/agentctl.py:11` uses `socket.AF_UNIX` unconditionally.
- `src/xrEngine/FrameTimeSession.cpp:213-214` returns `export_unavailable` on `_WIN32`.
- `tools/gl_macos_soak.py:23` hardcodes a historical Mac path (`/Users/rz/Downloads/...`); the script is macOS-only as written.

Suggested seam: Winsock `AF_UNIX` (Windows 10 1803+), so `agentctl.py` works unchanged; or a named pipe plus an `agentctl.py --pipe` option. Frame-time export needs the same exclusive, stopped-only, no-overwrite behavior as POSIX. Until a seam lands, the Windows baseline gate is manual: in Cordon, test movement, private save/reload, UI/inventory/menu and quit, and keep logs and inspected screenshots.

## Known Windows issues

From the 2026-11-08 playtest (CoC 1.4.22 on GL, about an hour of play plus a Zaton session):

- **Crash 1, `no static 'DIK_NUMPADENTER' in class 'key_bindings'`** (`axr_keybind.script` via `bind_stalker_ext.script`). Shipped CoC's luabind was built with `LUABIND_NO_ERROR_CHECKING`, so CoC scripts expect missing statics to read as nil. Fixed in `482a142de`: `key_bindings`, `DIK_keys` and `key_bindings_context` are now plain Lua tables. luabind itself and its upstream gitlink are unchanged, so other classes still raise on typos. **Build-verified only, not yet re-run in game.** If other classes hit `no static` in CoC scripts, revisit a global option.
- **Crash 2, `CSafeMapIterator::add` "already found in the registry"** on the ALife graph vertex registry, reached through `se_monster:on_unregister` → `CSE_ALifeOnlineOfflineGroup::unregister_member`. Fixed defensively in `215ec351a`: the duplicate is logged (`! ALife: object [..] already at graph vertex [..] during add`) and skipped, and `unregister_member` logs the squad/member state when it happens. Root cause unconfirmed; `register_member` was ruled out (it matches upstream xray-16 and deregisters via `switch_online`). **Build-verified only.**
- **Open, unowned:** after a resolution change to 2453x1380 the loading screen rendered into a 1280x720 corner (viewport/backbuffer not resized). Relaunching with that saved `vid_mode` produced a 199x34 window.

Pending in-game check for both fixes: rebind a key, open the options key-binding page, hold keys including numpad Enter, play until squads go offline; then grep the log for `no static`, `already at graph vertex` and `unregisters member`.
