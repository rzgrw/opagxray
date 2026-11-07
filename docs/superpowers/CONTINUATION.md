> **SUPERSEDED (status reconciled 2026-11-07).** Historical SP1→SP2 guide. Metal M-R0/menu shipped and M-R1 is parked; Vulkan has not started. GL stability/frame-time work is current, with AI expansion queued behind it. Development has moved to Windows 11 ([WINDOWS_HANDOFF](../WINDOWS_HANDOFF.md)); the repository is now the detached fork `https://github.com/rzgrw/opagxray` with only `main`/`dev` branches, so the `OpenXVibeRay` URL, `/Users/rz/...` paths and branch references below are historical. Use [PLAN_STATUS](../PLAN_STATUS.md) and [HANDOVER](../HANDOVER.md), not the historical next steps below.

# Viberay macOS Port — Continuation Guide

## Current State (2026-03-29)

**SP1: macOS Baseline — COMPLETE** (tagged `sp1-macos-baseline`)

The engine builds and runs Call of Chernobyl on macOS Apple Silicon (M4) with the OpenGL renderer. All commits are pushed to `https://github.com/rzgrw/OpenXVibeRay` on the `dev` branch.

### Commits Made (oldest to newest)

| Commit | Description |
|---|---|
| `c2b6af804` | Design spec for macOS native port with Metal renderer |
| `563ae711f` | Spec review feedback: critical fixes for Metal port design |
| `5634af116` | Fix renderer array size: 6 files need updating |
| `4ea7551a0` | SP1 implementation plan |
| `d434e283d` | **FIX:** LuaJIT CMakeLists hardcoded Xcode sysroot |
| `3a527cbc4` | **FIX:** ALife graph registry remove() non-fatal |
| `8b0a72df2` | **FIX:** All ALife remove() throws through LuaJIT |
| `8ca961fc3` | **FIX:** Harden remaining ALife registry remove() calls |
| `b9b78a979` | **FIX:** Disable XRAY_EXCEPTIONS on macOS (root cause fix) |
| `585dda21f` | macOS development setup guide |
| `0e2cea7a9` | **FIX:** Improve macOS log quality (splash, user.ltx, snd_device) |

### Key Engine Fixes for macOS

1. **`XRAY_EXCEPTIONS=0` on Darwin** (`src/CMakeLists.txt`) — C++ exceptions can't propagate through LuaJIT's ARM64 assembly trampoline. THROW becomes VERIFY (fatal abort with message) instead of throw.

2. **ALife `no_assert=true`** (`src/xrGame/alife_graph_registry.cpp`) — CoC ALife objects can be in inconsistent state; missing-entry during remove is logged as warning, not fatal.

3. **LuaJIT sysroot** (`Externals/LuaJIT-proj/CMakeLists.txt`) — Conditional SDK fallback for CommandLineTools-only installs.

4. **`_sopen` macro** (`src/Common/Platform{Apple,Linux,BSD}.inl`) — Variadic macro to drop Windows share-mode arg on POSIX.

---

## What's Next: SP2 (Metal Renderer)

### Design Spec

Read: `docs/superpowers/specs/2026-03-29-macos-metal-port-design.md`

This is a comprehensive design doc covering:
- New `xrRenderMetal/` and `xrRenderPC_Metal/` directories
- metal-cpp for C++ Metal API (no ObjC++ needed except one bridge file)
- SPIRV-Cross shader pipeline (GLSL → SPIR-V → MSL)
- SDL_Metal_CreateView() for window setup
- CommonTypes.h, metalState, occlusion queries
- Build system integration (RENDER_NAMESPACE, friend declarations, array size changes)

### SP2 Implementation Steps (high level)

1. **Set up Metal build infrastructure**
   - Create `src/Layers/xrRenderMetal/CMakeLists.txt` (template: xrRenderPC_GL's CMakeLists)
   - Create `src/Layers/xrRenderPC_Metal/CMakeLists.txt`
   - Add metal-cpp headers to `Externals/`
   - Add SPIRV-Cross + glslang as dependencies

2. **Create `metalHW.h/cpp`** — Metal device, command queue, layer setup
   - Use `SDL_Metal_CreateView()` for CAMetalLayer
   - One thin `.mm` bridge file for ObjC++ interop

3. **Create `CommonTypes.h`** — Type aliases for shared render code
   - Map D3D types to Metal equivalents
   - `ID3DState` typedef to `metalState`

4. **Create `metalState.h/cpp`** — PSO cache + state management
   - Hash: vertex shader + fragment shader + vertex layout + blend + pixel format
   - Lazy PSO creation, cached for reuse

5. **Create shader pipeline** — Build-time GLSL → MSL compilation
   - X-Ray shader preprocessor → glslang → SPIRV-Cross → metallib
   - CMake custom commands

6. **Add `#elif defined(USE_METAL)` branches** to `R_Backend.h` and related files
   - Systematic audit of all `USE_OGL`/`USE_DX11` ifdefs

7. **Register Metal module** in `entry_point.cpp`
   - Change array to `std::span` (6 files need updating — see spec)
   - Add friend declarations in 6 engine headers

8. **Implement render passes** — Map X-Ray phases to Metal render pass descriptors
   - G-buffer, shadow maps, light accumulation, post-processing, final combine

9. **Test and iterate** — Visual comparison with GL renderer, fix shader issues

### SP3 (macOS Polish) — After SP2

- `.app` bundle via CMake `MACOSX_BUNDLE`
- DMG installer with drag-and-drop
- Code signing and notarization (optional for dev builds)
- Menu bar integration, Retina/HiDPI support

---

## Known Issues to Address

### Shadow/Lighting Visual Glitches (OpenGL)
- Top priority for Metal renderer — Apple's deprecated GL 4.1 has shadow map issues
- Metal renderer will have proper depth buffer handling for Apple Silicon's TBDR architecture

### DDS Texture Loading Failures
- Some textures fail with `GL_INVALID_OPERATION (0x502)`
- Likely DDS formats unsupported by GLI → GL conversion on macOS
- May need format validation/fallback in `glTexture.cpp`
- Metal renderer should handle these via MTLTexture with proper format support

### CoC Mod Bugs (not engine — don't fix)
- `! alife():object(id): invalid id[65535]` — CoC script using invalid ID
- `~ slots_count = 14, but real slots count is 13` — CoC inventory config mismatch
- `! id for info_portion don't set` — CoC XML data issue
- Missing bump maps — CoC texture data incomplete

---

## Build Commands Reference

```bash
# First time setup
git submodule update --init --recursive
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo

# Configure (macOS Tahoe beta needs explicit SDK)
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk

# Build
cmake --build build -j10

# Run with Call of Chernobyl
/Users/rz/viberay/run-coc.sh
```

## Key Files Reference

| File | Purpose |
|---|---|
| `docs/superpowers/specs/2026-03-29-macos-metal-port-design.md` | Full Metal renderer design spec |
| `docs/superpowers/plans/2026-03-29-sp1-macos-baseline.md` | SP1 implementation plan (completed) |
| `docs/macos-dev-setup.md` | macOS development setup guide |
| `src/CMakeLists.txt:4-6` | XRAY_EXCEPTIONS=0 on Darwin |
| `src/xrGame/alife_graph_registry.cpp` | ALife no_assert fixes |
| `src/Layers/xrRenderPC_GL/CMakeLists.txt` | GL renderer build (template for Metal) |
| `src/Layers/xrRenderGL/` | GL hardware abstraction (template for Metal) |
| `src/xrEngine/EngineAPI.h` | RendererModule interface |
| `src/xr_3da/entry_point.cpp` | Renderer registration |
