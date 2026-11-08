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

**Transport (verified 2026-11-08, menu scene only).** Same protocol, queues and verbs as POSIX (`src/xrEngine/AgentBridge.cpp`); only a small socket shim differs. Windows uses Winsock TCP bound to `127.0.0.1` only (`SO_EXCLUSIVEADDRUSE`, non-inheritable socket, `TCP_NODELAY`). Python on Windows has no `socket.AF_UNIX`, so Winsock `AF_UNIX` was not used. Options:

- `-agent_bridge` turns the bridge on. `-agent_bridge_port <n>` (0..65535) picks the port. Without it, or with 0, the OS picks a free port.
- The bound port is written atomically (temp file + `MoveFileEx`) to `<appdata>\agent_bridge.port`. The file is deleted at startup (stale) and again on clean exit. A bind failure (for example a port in use, `10048`) or an invalid port logs `! agent_bridge: ...` and the game runs without a bridge; it never asserts.
- `tools/agentctl.py TARGET ...` takes a Unix socket path, `tcp:HOST:PORT`, an `agent_bridge.port` file, or an appdata dir containing one. `--wait S` retries until the instance is up. Tests: `py -m unittest tools.tests.test_agentctl`.

**Background mode: `-agent_background`** (off unless passed; any platform, verified on Windows). For unattended instances next to a user's desktop:
- The window is shown without activation (`SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN`, no `SDL_RaiseWindow`, no `SW_RESTORE`). It is a `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` window (no taskbar or Alt+Tab entry, never topmost), always windowed (fullscreen is forced to borderless windowed), and parked just left of the leftmost display. It is never minimized. GL keeps rendering there and `shot` images are correct.
- The main window is treated as permanently active: no task-scheduler pause and no input reset on focus loss, and `rs_always_active` is set. There is no OS input grab, cursor hide or cursor warp. Bridge `key`/`mouse` events go straight into SDL's queue and need no focus (verified: menu navigation, console toggle, cursor motion).
- No audio device is opened (same path as `-nosound`; `snd_device ?`). There is no splash window, the system sticky-keys settings are left alone, and error/assert dialogs are suppressed (BugTrap save-report mode; asserts log and abort).

**Several instances.** Give each instance its own appdata through its own fsgame file, and its own port (or 0). Logs, screenshots, `perf/`, `imgui.ini`, the shader cache, `user.ltx`, saves and the port file then stay in that appdata. Example, from the repo root in Git Bash, with a runtime staged at `build/runtime/coc`:

```bash
RT=build/runtime/coc
mkdir -p $RT/appdata_a && printf 'renderer renderer_rgl\nvid_mode 1024x576\n' > $RT/appdata_a/user.ltx
sed 's/^\$app_data_root\$.*/$app_data_root$ = true| false| $fs_root$| appdata_a\\/' $RT/fsgame.ltx > $RT/fsgame_a.ltx
bin/x64/Release/xrEngine.exe -fsltx "$(cygpath -w $RT/fsgame_a.ltx)" \
    -agent_bridge -agent_bridge_port 0 -agent_background -nointro &
py tools/agentctl.py --wait 90 $RT/appdata_a hello
py tools/agentctl.py $RT/appdata_a state
```

Two instances, one on 47101 and one on an OS-chosen port, answered interleaved `state`/`shot` calls from two threads over simultaneous connections. A 60 s poll of the foreground window during launch and driving never found either instance in the foreground. Not yet exercised on Windows: in-level gameplay (movement, save/load, `agent.*`/`ai.*` verbs) and long soaks.

**Frame-time export** works on Windows (`perf.export` → `perf/capture-<pid>-<n>.csv`, LF bytes). It follows the POSIX contract: stopped-only, exclusive `CREATE_NEW`, never overwrites, no reparse-point traversal, deletes only its own failed output, and keeps the appdata/perf directories pinned with open handles. `tools/frame_time_report.py` parses the output; asset-provenance acceptance is still the known gap.

Still POSIX-only: `tools/gl_macos_soak.py` (hardcoded Mac path, AF_UNIX) and the `tools/frame_time_provenance.py` socket-identity checks (AF_UNIX `proc_pidfdinfo`).

## Known Windows issues

From the 2026-11-08 playtest (CoC 1.4.22 on GL, about an hour of play plus a Zaton session):

- **Crash 1, `no static 'DIK_NUMPADENTER' in class 'key_bindings'`** (`axr_keybind.script` via `bind_stalker_ext.script`). Shipped CoC's luabind was built with `LUABIND_NO_ERROR_CHECKING`, so CoC scripts expect missing statics to read as nil. Fixed in `482a142de`: `key_bindings`, `DIK_keys` and `key_bindings_context` are now plain Lua tables. luabind itself and its upstream gitlink are unchanged, so other classes still raise on typos. **Build-verified only, not yet re-run in game.** If other classes hit `no static` in CoC scripts, revisit a global option.
- **Crash 2, `CSafeMapIterator::add` "already found in the registry"** on the ALife graph vertex registry, reached through `se_monster:on_unregister` → `CSE_ALifeOnlineOfflineGroup::unregister_member`. Fixed defensively in `215ec351a`: the duplicate is logged (`! ALife: object [..] already at graph vertex [..] during add`) and skipped, and `unregister_member` logs the squad/member state when it happens. Root cause unconfirmed; `register_member` was ruled out (it matches upstream xray-16 and deregisters via `switch_online`). **Build-verified only.**
- **Resolution change: render in a 1280x720 corner, odd mode 2453x1380, "199x34 window".** Root-caused and fixed 2026-11-08; it was three separate things:
  1. *Corner render (GL only, every platform):* the GL context outlives `CHW::Reset`, and so does its viewport, which GL sets to the drawable size when the context is created. The menu, loading screen and UI never call `glViewport`, so after any reset they kept drawing into the old-size corner (GL origin, bottom-left) of correctly resized targets. DX11 is unaffected because `CBackend::OnFrameBegin` calls `rmNormal` every frame. Upstream xray-16 `dev` has the same `CHW::Reset` (checked by reading the code, not run). Fixed in `eade89a4d`: `CHW::Reset` sets the viewport to the new surface size.
  2. *2453x1380:* nobody picked it; the engine produced it. The fork's window-fit clamp (`b4de01e9d`) scaled the requested 2560x1440 window to the 2560x1380 work area (2560*1380/1440 = 2453). `SDL_WINDOWEVENT_SIZE_CHANGED` then treated that as a user resize: it overwrote `psDeviceMode`, ran a second reset, and `cfg_save` stored the result. The exe manifest is `dpiAware`, so all sizes are physical pixels; DPI scaling does not cause it. Fixed in `c9923c9a3`: the event matching the engine's own scaled size is ignored (all platforms), and on Windows the clamp is measured against the display bounds instead of the work area, because Windows doesn't clamp windows.
  3. *199x34:* this is a **minimized** window's rect (`GetWindowRect` gives `(-32000,-32000) 199x34`, client 0x0, at 125% scale), not a parsed `vid_mode`. Relaunching with a saved 2453x1380 opens a normal 2453x1380 window, borderless or bordered. The likely cause is `CHW::OnAppDeactivate`, which minimizes fullscreen styles on focus loss, or the playtest's focus juggling. Not reproduced: fullscreen tests were out of scope.

  Verified 2026-11-08 on this laptop (RTX 3080 Laptop, GL, CoC 1.4.22, windowed and borderless only, `-nosound`, PrintWindow captures plus engine log). The display changed mid-session from 2560x1440@170 at 125% to 2560x1600@165 at 150%, so the runs cover both. Before the fix: 1280x720 → `vid_mode 2560x1440 (170Hz)` + `vid_restart` gave the corner render, two RESETs, and saved 2453x1380; 1280x720 → 1920x1080 also gave the corner. After the fix: 1920x1080, 2560x1440 and 2560x1600 render full-window with one RESET and save the requested mode; an oversized 3840x2160 scales the window to the display but keeps and saves 3840x2160 with one RESET; an external window resize adopts the new size and renders full-window; startup at 2560x1600 bordered and with a legacy saved 2453x1380 is fine. In-session changes were driven by a temporary, uncommitted frame-scheduled console hook (no Windows bridge yet), not through the options UI. **Unverified:** exclusive fullscreen and borderless-fullscreen changes, the 199x34 minimize path, in-level loading after a change (the CoC loading-style screen was seen full-window, but no save was loaded), macOS/Linux (the clamp-event fix is shared code), DX11 runtime (built only), and multi-monitor.

Pending in-game check for both fixes: rebind a key, open the options key-binding page, hold keys including numpad Enter, play until squads go offline; then grep the log for `no static`, `already at graph vertex` and `unregisters member`.
