# Windows development setup

Audited on the development machine (Windows 11, checkout `C:\Users\izeon\orca\opagxray`) on 2026-11-07. **No one has built or run current `dev` on Windows yet**; every build step below is unverified until someone records a successful run. Transfer context and archive-tag recovery: [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md).

## Toolchain

State at the start of the audit: no Visual Studio/MSVC, MSBuild, NuGet, CMake or Ninja; Python absent (only Microsoft Store alias stubs, no `py`); git 2.56 present; all 9 submodules uninitialized.

Re-checked at 23:05 the same day, after installs had begun:

| Tool | Requirement | Found |
|---|---|---|
| Visual Studio 2022 "Desktop development with C++" (MSVC v143 + Windows SDK) | required | VS 2022 Build Tools 17.14, MSVC 14.44.35207 (`cl.exe` Hostx64/x64), Windows SDK 10.0.26100.0 |
| CMake ≥ 3.23 | required | 3.31.6-msvc6, bundled with Build Tools (on `PATH` only inside a Developer shell) |
| Ninja | required for the recommended route | 1.12.1, bundled with Build Tools (Developer shell only) |
| Python ≥ 3.11 with the `py` launcher | required (the stager uses `hashlib.file_digest`) | 3.15.2 via the Python Install Manager; `hashlib.file_digest` present |
| NuGet | only for the MSBuild route | absent |
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

**Recommended: CMake + Ninja (unverified).** The fork's recent sources (`AgentBridge`, `xrSim`, `FrameTime*` telemetry, the frame-time and menu-reload test targets) are wired only through CMake; no checked-in `.vcxproj` lists them (`src/xrEngine/xrEngine.vcxproj` has none). The Windows CMake CI entries are commented out at `.github/workflows/cibuild.yml:93-94`, so this route has no CI history either. Expect dependency discovery and MSVC-specific fixes on the first configure. Keep the first real compiler/linker log.

```powershell
# Developer PowerShell for VS 2022, x64
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**MSBuild / `src/engine.sln`:** the existing Windows CI recipe (`nuget restore src\engine.sln`, then `msbuild src\engine.sln /m /p:Configuration=Release /p:Platform=x64`). It is unusable for current `dev` until the project inventories are reconciled with CMake.

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

Keep this directory immutable. Obtain the separate 1.4.12 base and 1.4.22 patch archives, or split the install deliberately and record how, before staging. The GL shader overlay comes from the repository's `res/gamedata/shaders/gl`. Decide GL or DirectX explicitly; never launch a DirectX renderer with the GL-only overlay and call the runs equivalent. Keep user and test appdata separate, and never share IXRay saves.

## Agent bridge and telemetry on Windows

- `src/xrEngine/AgentBridge.cpp:6` wraps the whole bridge in `#ifndef XR_PLATFORM_WINDOWS`; the Windows branch (`:626-628`) only defines `g_agent_bridge = nullptr`, so the bridge is a stub.
- `tools/agentctl.py:11` uses `socket.AF_UNIX` unconditionally.
- `src/xrEngine/FrameTimeSession.cpp:213-214` returns `export_unavailable` on `_WIN32`.
- `tools/gl_macos_soak.py:23` hardcodes a historical Mac path (`/Users/rz/Downloads/...`); the script is macOS-only as written.

Suggested seam: Winsock `AF_UNIX` (Windows 10 1803+), so `agentctl.py` works unchanged; or a named pipe plus an `agentctl.py --pipe` option. Frame-time export needs the same exclusive, stopped-only, no-overwrite behavior as POSIX. Until a seam lands, the Windows baseline gate is manual: in Cordon, test movement, private save/reload, UI/inventory/menu and quit, and keep logs and inspected screenshots.
