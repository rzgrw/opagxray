# Open Agentic XRay — Claude Code Project Guide

## Project Overview

Open Agentic XRay is a **detached fork** of OpenXRay (X-Ray Engine for S.T.A.L.K.E.R.) being rebuilt with AI coding agents. The repository is [rzgrw/opagxray](https://github.com/rzgrw/opagxray) and is no longer in OpenXRay's GitHub fork network. OpenXRay (and IXRay) are **inspiration and foundation, not scripture**: there is no PR flow upstream; selective, reviewed intake of upstream fixes stays allowed with license notices preserved. Current execution status: [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md); the older roadmap retains the long-term vision.

- **Branches:** `main` is stable (GitHub default; last fast-forwarded to `dev` on 2026-11-08 by user request, see PLAN_STATUS) and moves only by fast-forward from `dev` after a recorded gate; `dev` is experimental integration. Feature work goes on short-lived branches that are merged then deleted, or archived as `archive/<name>` tags (`git fetch origin tag archive/<name>`, then `git switch -c <x> archive/<name>`). The `dev` README is the live one and becomes the `main` README on each fast-forward; between gates only descriptive `main` README sections may change, via a docs-only commit on `main` merged back into `dev` (keep the dev README) so fast-forward stays possible. Commit/push only when asked.
- **Platform:** development has moved from macOS to **Windows 11**. MSBuild x64 Release builds and runs CoC 1.4.22 on GL; Mac results are historical. Start from [docs/windows-dev-setup.md](docs/windows-dev-setup.md).

- **Runtime:** OpenGL is the working baseline and **stability and frame-time consistency are the current priority**. Native-renderer work is paused: Metal M-R0/menu shipped, M-R1 parked at `caa2e4953` (tag `archive/feat/metal-mr1-ingame`), Vulkan not started. Long-term direction remains Metal on macOS / Vulkan on Linux and Windows.
- **AI:** xrSim foundation, HTTP transport, thin actor harness, session packs, and async actor wakes are shipped. Gate 1 Task 1 canonical state is integrated at `04b4ef5b5`; further AI expansion follows the runtime comparison. Gate 1 is not complete. See the inventory before restarting old plans.
- **Agent bridge (shipped, Windows + POSIX):** launch with `-agent_bridge`, drive via `tools/agentctl.py` — Claude can run, control, and screenshot the game itself. Windows uses TCP on 127.0.0.1 (`-agent_bridge_port`, port written to `<appdata>/agent_bridge.port`); POSIX uses a Unix socket. Run test instances with `-agent_background` (no focus, off-screen, no audio) and a separate appdata each.
- **Frame-time telemetry (shipped, opt-in):** `FrameTimeCollector`/`FrameTimeSession`, bridge verbs `perf.start/stop/reset/status/export`, `tools/frame_time_report.py`, and `tools/frame_time_provenance.py` sidecars. Measurement infrastructure only — no performance gain has been measured. Export works on Windows and POSIX.

## Architecture

- **Engine core:** C++20 (`cmake/XRay.Build.cmake`; old C++17 plan code is a portable subset), CMake 3.23+, SDL2 for windowing/input, OpenAL for audio
- **Renderers:** OpenGL (working, the current runtime), Metal (M-R0/menu shipped; M-R1 parked, `XRAY_METAL=OFF`), Vulkan (PC renderer, not yet created), DX11 (Windows legacy, no investment)
- **Scripting:** LuaJIT + luabind for game logic (to be reduced by the AI rehaul)
- **Platform layer:** `src/Common/Platform.hpp` → `PlatformApple.inl` / `PlatformLinux.inl` / `PlatformWindows.inl`

## Key Directories

```
src/Layers/xrRenderPC_GL/    # OpenGL renderer (working baseline)
src/Layers/xrRenderGL/       # GL hardware abstraction
src/Layers/xrRenderMetal/    # Metal HAL — M-R0 shipped, M-R1 parked
src/Layers/xrRenderPC_Metal/ # Metal module (EXCLUDE_FROM_ALL; opt-in)
src/Layers/xrRender/         # Shared render code (USE_DX11/USE_OGL/USE_METAL branches)
src/Layers/xrRender_R2/      # Shared render logic (CRender, phases)
src/xrEngine/                # Core engine, SDL2, input, console, AgentBridge
src/xrCore/                  # Platform abstractions, file I/O, threading, debug
src/xrGame/                  # Game logic, ALife simulation, Lua bindings
src/xr_3da/                  # Main executable, entry_point.cpp
Externals/                   # LuaJIT, ODE, imgui, metal-cpp, glslang, SPIRV-Cross, ...
tools/                       # agentctl.py + bridge soak/acceptance scripts
```

## Build (Windows)

Verified route: `nuget restore src\engine.sln`, then MSBuild x64 Release (from Claude Code's shell: `env -u NoDefaultCurrentDirectoryInExePath MSBuild.exe ...`). Output `bin\x64\Release\xrEngine.exe`. New sources go into both CMake and the `.vcxproj`. CMake+Ninja on Windows is unverified. Details: [docs/windows-dev-setup.md](docs/windows-dev-setup.md). Never mix stock CoC `bin/` DLLs with repo-built modules.

## Build (macOS)

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries go to `bin/arm64/Release/` (not `build/bin/`).

On macOS Tahoe beta, also pass: `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`

`-DXRAY_METAL=ON` builds/links the parked Metal module; leave it off for GL/AI work.

## Critical macOS Constraints

- **XRAY_EXCEPTIONS=0 on Darwin** (`src/CMakeLists.txt`). C++ exceptions cannot propagate through LuaJIT's ARM64 trampoline. THROW compiles to VERIFY (fatal abort). Do NOT re-enable. Consequence for all new code: **failures must be values, never throws** (see the two-phase-validation pattern in the specs).
- **`sysctl -n hw.ncpu`** returns trailing space on Tahoe. Use `-j10`.
- **OpenGL 4.1** is the macOS ceiling — the reason for the renderer replacement (native Metal on Mac; Vulkan on PC).

## Testing — use the agent bridge

Bridge-driven testing works on Windows (TCP) and macOS/Linux (Unix socket); see the README and windows-dev-setup for the multi-instance launch. Use the exact primary IXRay CoC 1.4.12 + 1.4.22 archives and target GL overlay, with separate private user/test appdata and explicit paths from [macOS setup](docs/macos-dev-setup.md). Do not use the historical Downloads runtime default or assume save `1`. Launch with `-agent_bridge`, then:

```bash
python3 tools/agentctl.py <private-runtime>/appdata/agent_bridge.sock state
```

Verbs: `hello/cmd/lua/key/mouse/state/shot/bye`. A private-save scenario covering movement, save/load, UI, and quit is the regression gate before merging engine changes; adapt historical scripts that assume save `1`. Screenshots land in `appdata/screenshots/` and are readable by Claude. Run setup details: `docs/macos-dev-setup.md` and the CoC notes in project memory.

## Coding Conventions

- Match the style of the file you're editing (the codebase is not consistent)
- Platform code: `#ifdef XR_PLATFORM_APPLE` / `XR_PLATFORM_WINDOWS` / `XR_PLATFORM_POSIX`
- Renderer branches: `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)` — `USE_VULKAN` joins this pattern; keep GL/DX11 output unchanged when adding branches
- Each renderer module compiles with `RENDER_NAMESPACE=render_gl` / `render_metal` / (next) `render_vulkan`
- Assertions: `VERIFY`, `R_ASSERT`, `THROW` (= VERIFY on macOS). Do not change Windows exception policy or force `-fno-exceptions` globally to work around port errors.
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`

## Documentation

- [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md) — current inventory, shipped refs, recovery order, and outstanding acceptance; read first.
- [docs/HANDOVER.md](docs/HANDOVER.md) — runtime/AI context and parked Metal resume guide.
- [Engine roadmap v2](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md) — long-term vision; renderer-first execution order is superseded.
- [Authority spine](docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md) and [thin harness](docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md) refine the original agentic-Zone design.
- [docs/windows-dev-setup.md](docs/windows-dev-setup.md) — Windows toolchain, build route, assets, and bridge gaps (audited 2026-11-07).
- [docs/WINDOWS_HANDOFF.md](docs/WINDOWS_HANDOFF.md) — Windows transfer context, archive-tag recovery, next-session order.
- [macOS setup](docs/macos-dev-setup.md) — private runtime staging and explicit build/run/test paths.
- [Frame-time plan](docs/superpowers/plans/2026-11-07-frame-time-measurement.md) and the [OpenXRay performance](docs/OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md) / [physics](docs/OPENXRAY_PHYSICS_PERFORMANCE_AUDIT_2026-11-07.md) audits — current runtime track.

## Current State (2026-11-07)

- **Baseline:** `dev` at `950a7820b` (before this doc refresh). Since foundation `8984220b6`: Gate 1 Task 1 canonical state `04b4ef5b5`; frame-time recorder/reports `e3f273f04`; opt-in GL telemetry + bridge export `a389540d8`; frame readiness/window-visibility classification `456808010`; provenance sidecars `90c6e57e6`; menu reload failure-ownership fix `604fb0690`; Windows handoff `950a7820b`.
- **Evidence:** recorded test/build/live runs for these commits are macOS-only historical records (see PLAN_STATUS); nothing has been rerun on Windows. Telemetry captures still fail asset-provenance acceptance; no speedup is claimed.
- **Recovery:** unmerged Gate 1 source `1d951aa07` (tag `archive/codex/ai-authority-spine-gate1`) is partial recovery material; authority and strict codec/atomic commit remain queued behind the stability/performance comparison. Frame-overhead WIP (tags `archive/rzgrw/frame-overhead-base` / `-reference`) and the transfer evidence/provenance WIP (tag `archive/transfer/windows-2026-11-07`) are recovered selectively, never merged wholesale.
- **Open acceptance:** consistent Windows x64 build plus Windows bridge/export seams; hardened live non-coast prompt, current private packs, five-run GL/20-minute idle, direct empty-group path, sustained vsync-on gameplay; overhead reference and repeated performance captures; Gate 1 replay/soak/live gates.
- **Provider policy:** `claude-sonnet-5` (still the source default in `xrSimAgentProvider.h`) is a historical value requiring official availability verification before live use; no provider/model substitution is approved.
