# OpenXVibeRay — Claude Code Project Guide

## Project Overview

OpenXVibeRay is a fork of OpenXRay (X-Ray Engine for S.T.A.L.K.E.R.) being rebuilt with AI coding agents. The original engine is **inspiration and foundation, not scripture**. Current execution status: [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md); the older roadmap retains the long-term vision.

- **Runtime:** OpenGL is the working baseline and **stability and frame-time consistency are the current priority**. Native-renderer work is paused: Metal M-R0/menu shipped, M-R1 parked at `caa2e4953`, Vulkan not started. Long-term direction remains Metal on macOS / Vulkan on Linux and Windows.
- **AI:** xrSim foundation, HTTP transport, thin actor harness, session packs, and async actor wakes are shipped. Gate 1 Task 1 canonical state is integrated at `04b4ef5b5`; further AI expansion follows the runtime comparison. Gate 1 is not complete. See the inventory before restarting old plans.
- **Agent bridge (shipped):** launch with `-agent_bridge`, drive via `tools/agentctl.py` — Claude can run, control, and screenshot the game itself. This is the standard verification harness.

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

Use the exact primary IXRay CoC 1.4.12 + 1.4.22 archives and target GL overlay, with separate private user/test appdata and explicit paths from [macOS setup](docs/macos-dev-setup.md). Do not use the historical Downloads runtime default or assume save `1`. Launch with `-agent_bridge`, then:

```bash
python3 tools/agentctl.py <private-runtime>/appdata/agent_bridge.sock state
```

Verbs: `hello/cmd/lua/key/mouse/state/shot/bye`. A private-save scenario covering movement, save/load, UI, and quit is the regression gate before merging engine changes; adapt historical scripts that assume save `1`. Screenshots land in `appdata/screenshots/` and are readable by Claude. Run setup details: `docs/macos-dev-setup.md` and the CoC notes in project memory.

## Coding Conventions

- Match the style of the file you're editing (the codebase is not consistent)
- Platform code: `#ifdef XR_PLATFORM_APPLE` / `XR_PLATFORM_WINDOWS` / `XR_PLATFORM_POSIX`
- Renderer branches: `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)` — `USE_VULKAN` joins this pattern; keep GL/DX11 output unchanged when adding branches
- Each renderer module compiles with `RENDER_NAMESPACE=render_gl` / `render_metal` / (next) `render_vulkan`
- Assertions: `VERIFY`, `R_ASSERT`, `THROW` (= VERIFY on macOS)
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`

## Documentation

- [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md) — current inventory, shipped refs, recovery order, and outstanding acceptance; read first.
- [docs/HANDOVER.md](docs/HANDOVER.md) — runtime/AI context and parked Metal resume guide.
- [Engine roadmap v2](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md) — long-term vision; renderer-first execution order is superseded.
- [Authority spine](docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md) and [thin harness](docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md) refine the original agentic-Zone design.
- [macOS setup](docs/macos-dev-setup.md) — private runtime staging and explicit build/run/test paths.

## Current State (2026-11-07)

- **Baseline:** `dev` at `8984220b6` includes the IXRay foundation guards, UI comparator, private stager, and frame-wait harness.
- **Evidence:** 41 Python tests, two C++ targets, and the vsync-off Cordon gate are historical records; the original ignored artifacts/worktree are absent. They are not current reruns.
- **Recovery:** unmerged Gate 1 branch `1d951aa07` is partial recovery material; Task 1 is integrated at `04b4ef5b5`. Authority and strict codec/atomic commit remain queued behind the stability/performance comparison.
- **Open acceptance:** hardened live non-coast prompt, current private packs, five-run GL/20-minute idle, direct empty-group path, and sustained vsync-on gameplay; Gate 1 has further replay/soak/live gates.
- **Provider policy:** `claude-sonnet-5` is a historical value requiring official availability verification before live use; this reconciliation approves no provider/model substitution.
