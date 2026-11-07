# SP2: Metal Renderer Implementation Plan

> **PARKED (2026-11-07).** M-R0/menu shipped; M-R1 is unmerged at `caa2e4953` (tag `archive/feat/metal-mr1-ingame`). AI on GL is current, so Tasks 22–24 are not the active next milestone. Historical instructions below remain for renderer recovery; see [PLAN_STATUS](../../PLAN_STATUS.md) and the [Metal resume guide](../../HANDOVER.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace OpenGL with a native Metal renderer backend so S.T.A.L.K.E.R. runs on macOS with proper GPU support (beyond deprecated GL 4.1).

**Architecture:** Mirror the existing GL backend (`xrRenderGL/` + `xrRenderPC_GL/`) with Metal equivalents (`xrRenderMetal/` + `xrRenderPC_Metal/`). Use metal-cpp for C++ Metal API, SPIRV-Cross for shader translation (GLSL -> SPIR-V -> MSL). Reuse all shared render code from `xrRender/` and `xrRender_R2/` via `RENDER_NAMESPACE=render_metal`.

**Tech Stack:** metal-cpp (header-only C++ Metal wrapper), SPIRV-Cross, glslang, Metal framework, QuartzCore framework, SDL2 Metal view support

**Spec:** `docs/superpowers/specs/2026-03-29-macos-metal-port-design.md`

---

## File Structure

### New directories and files

```
src/Layers/xrRenderMetal/                    # Metal hardware abstraction (mirrors xrRenderGL/)
    CMakeLists.txt                            # Build config for Metal HAL
    CommonTypes.h                             # D3D type aliases -> Metal types
    metalHW.h                                 # CHW class: MTLDevice, command queue, layer
    metalHW.cpp                               # CHW implementation (C++ via metal-cpp)
    metalHW_Apple.mm                          # Thin ObjC++ bridge for SDL_Metal_CreateView()
    metalState.h                              # PSO cache + ID3DState typedef target
    metalState.cpp                            # State management implementation
    metalStateUtils.h                         # D3D state enum -> Metal enum converters
    metalStateUtils.cpp                       # Converter implementations
    metalR_Backend_Runtime.h                  # CBackend inline methods for Metal
    metalr_constants.cpp                      # Shader constant binding
    metalr_constants_cache.h                  # R_constants class for Metal uniform buffers
    metalBufferUtils.cpp                      # Vertex/index/constant buffer creation
    metalVertexInput.h                        # MTLVertexDescriptor from X-Ray vertex declarations
    metalVertexInput.cpp                      # Vertex input layout translation
    metalConstantBuffer.h                     # MTLBuffer-backed constant buffers
    metalConstantBuffer.cpp                   # Constant buffer management
    metalSH_Texture.cpp                       # CTexture Metal specialization (load, bind)
    metalSH_RT.cpp                            # Render target creation
    metalRenderPassManager.h                  # MTLRenderPassDescriptor management
    metalRenderPassManager.cpp                # Render pass lifecycle (begin/end/configure)
    metalTexture.cpp                          # Texture loading (DDS -> MTLTexture)
    metalTextureUtils.h                       # Texture format conversion helpers
    metalTextureUtils.cpp                     # DDS/format -> MTLPixelFormat mapping
    metalResourceManager_Resources.cpp        # Shader/resource creation for Metal
    metalResourceManager_Scripting.cpp        # Lua script resource bindings
    metalr_screenshot.cpp                     # Screenshot capture via Metal
    metalDetailManager_VS.cpp                 # Detail manager vertex setup
    metalHWCaps.cpp                           # Hardware capability detection
    Blender_Recorder_Metal.cpp                # Blender state recording for Metal
    metalOcclusionQuery.h                     # MTLVisibilityResultBuffer wrapper
    metalOcclusionQuery.cpp                   # Occlusion query implementation
    metalMinMaxSMBlender.cpp                  # MinMax shadow map blender
    metalMSAABlender.cpp                      # MSAA blender
    metalRainBlender.cpp                      # Rain effect blender

src/Layers/xrRenderPC_Metal/                 # Metal render module (mirrors xrRenderPC_GL/)
    CMakeLists.txt                            # Build config (~440 lines, includes shared sources)
    stdafx.h                                  # Precompiled header: USE_METAL, metal-cpp includes
    stdafx.cpp                                # PCH source
    xrRender_Metal.cpp                        # RMetalRendererModule + GetRendererModule() factory
    r2_test_hw.cpp                            # xrRender_test_hw() — probe for Metal device
    r2_R_sun.cpp                              # Sun shadow rendering (Metal-specific)
    metal_rendertarget.h                      # CRenderTarget for Metal (G-buffer, shadows, post-FX)
    metal_rendertarget.cpp                    # Render target creation and management
    metal_rendertarget_accum_direct.cpp       # Direct light accumulation
    metal_rendertarget_build_textures.cpp     # G-buffer texture creation
    metal_rendertarget_phase_accumulator.cpp  # Light accumulation phase setup
    metal_rendertarget_phase_bloom.cpp        # Bloom post-processing phase
    metal_rendertarget_phase_combine.cpp      # Final combine pass
    metal_rendertarget_phase_flip.cpp         # Present/flip
    metal_rendertarget_phase_ssao.cpp         # SSAO phase
    metal_rendertarget_u_set_rt.cpp           # Render target binding helpers
    dxImGuiRender.cpp                         # ImGui Metal backend integration

Externals/metal-cpp/                         # Header-only metal-cpp (Apple's C++ Metal wrapper)
    Metal/Metal.hpp                           # Main include
    Foundation/Foundation.hpp                 # NS:: types
    QuartzCore/QuartzCore.hpp                 # CA:: types
```

### Files to modify

```
src/Layers/CMakeLists.txt                    # Add xrRenderPC_Metal subdirectory
src/Include/xrRender/xrRender.h              # Add render_metal namespace + export

# Renderer array: std::array<RendererModule*, 2> -> std::span<RendererModule*>
src/xr_3da/entry_point.cpp                   # Add Metal module, change to std::span
src/xrEngine/EngineAPI.h                     # Change CreateRendererList signature
src/xrEngine/EngineAPI.cpp                   # Change CreateRendererList definition
src/xrEngine/Engine.h                        # Change CEngine::Initialize signature
src/xrEngine/Engine.cpp                      # Change CEngine::Initialize definition
src/xrEngine/x_ray.h                         # Change CApplication constructor
src/xrEngine/x_ray.cpp                       # Change CApplication constructor impl

# Friend class declarations (add render_metal namespace)
src/xrEngine/StatGraph.h
src/xrEngine/GameFont.h
src/xrEngine/Rain.h
src/xrEngine/thunderbolt.h
src/xrEngine/Environment.h
src/xrEngine/xr_efflensflare.h

# Shared render code: add #elif defined(USE_METAL) branches
src/Layers/xrRender/R_Backend.h              # ~22 ifdef sites
src/Layers/xrRender/R_Backend_Runtime.h      # ~8 ifdef sites
src/Layers/xrRender/R_Backend_Runtime.cpp    # ~4 ifdef sites
src/Layers/xrRender/SH_Atomic.h             # ~9 ifdef sites
src/Layers/xrRender/SH_Texture.h            # ~7 ifdef sites
src/Layers/xrRender/Shader.h                # ~1 ifdef site
src/Layers/xrRender/tss_def.h               # ~1 ifdef site
src/Layers/xrRender/tss_def.cpp             # ~4 ifdef sites
src/Layers/xrRender/ColorMapManager.cpp      # ~3 ifdef sites
src/Layers/xrRender/r_constants.cpp          # ~3 ifdef sites
src/Layers/xrRender/r_constants.h            # constant routing
src/Layers/xrRender/R_Backend.cpp            # backend init
src/Layers/xrRender/SH_Atomic.cpp           # shader destruction
src/Layers/xrRender/ResourceManager.cpp      # resource creation dispatch
src/Layers/xrRender/Debug/dxPixEventWrapper.h # GPU debug markers
```

---

## Phase 1: Build Infrastructure

### Task 1: Add metal-cpp headers to Externals

metal-cpp is Apple's official header-only C++ wrapper for Metal/Foundation/QuartzCore. It provides `MTL::Device`, `MTL::CommandQueue`, `MTL::Buffer`, etc. without requiring Objective-C++.

**Files:**
- Create: `Externals/metal-cpp/` (download from Apple)

**Reference:** https://developer.apple.com/metal/cpp/ — download the metal-cpp package

- [ ] **Step 1: Download and extract metal-cpp**

```bash
cd /Users/rz/OpenXVibeRay/Externals
curl -L -o metal-cpp.zip "https://developer.apple.com/metal/cpp/files/metal-cpp_macOS15.2_iOS18.2.zip"
unzip metal-cpp.zip -d metal-cpp-tmp
# The zip extracts to a directory like metal-cpp_macOS15.2_iOS18.2/
# Move contents to Externals/metal-cpp/
mv metal-cpp-tmp/metal-cpp/ metal-cpp
rm -rf metal-cpp-tmp metal-cpp.zip
```

- [ ] **Step 2: Verify the header structure exists**

```bash
ls Externals/metal-cpp/Metal/Metal.hpp
ls Externals/metal-cpp/Foundation/Foundation.hpp
ls Externals/metal-cpp/QuartzCore/QuartzCore.hpp
```

Expected: All three files exist.

- [ ] **Step 3: Commit**

```bash
git add Externals/metal-cpp/
git commit -m "feat(sp2): add metal-cpp headers (Apple's C++ Metal wrapper)"
```

---

### Task 2: Create xrRenderMetal stub directory and CMakeLists.txt

Create the Metal hardware abstraction layer directory with minimal stubs that compile. This mirrors `src/Layers/xrRenderGL/`.

**Files:**
- Create: `src/Layers/xrRenderMetal/CMakeLists.txt`
- Create: `src/Layers/xrRenderMetal/CommonTypes.h` (stub)
- Create: `src/Layers/xrRenderMetal/metalHW.h` (stub)
- Create: `src/Layers/xrRenderMetal/metalHW.cpp` (stub)
- Create: `src/Layers/xrRenderMetal/metalState.h` (stub)
- Create: `src/Layers/xrRenderMetal/metalState.cpp` (stub)
- Create: `src/Layers/xrRenderMetal/metalR_Backend_Runtime.h` (stub)
- Create: `src/Layers/xrRenderMetal/metalr_constants_cache.h` (stub)

**Context:** These are stubs — just enough to compile. Each will be filled in during later tasks.

**D3D9 compatibility:** The shared render code uses D3D9-style enums and types (D3DCULL_CCW, D3DBLEND_ONE, D3DCMP_LESSEQUAL, etc.) from `sdk/include/d3d9types.h` and related headers. The GL backend includes these via its dependency chain. The Metal stdafx.h must also include these D3D9 compatibility headers so the shared code compiles. Check what `src/Layers/xrRenderPC_GL/stdafx.h` pulls in and mirror those includes.

- [ ] **Step 1: Create CommonTypes.h stub**

This is the critical file that maps D3D types to Metal equivalents. Start with minimal types needed to compile shared code. Reference `src/Layers/xrRenderGL/CommonTypes.h` for the complete list of aliases needed.

The file must define at minimum:
- `D3D_VIEWPORT` struct
- `D3D_COMPARISON_FUNC` enum
- `D3D_CLEAR_FLAG` values
- `D3D_DEPTH_STENCIL_STATE` and `D3D_BLEND_STATE` structs
- `ID3DState` typedef (-> `metalState`)
- Buffer handle typedefs (`IndexBufferHandle`, `VertexBufferHandle`, `ConstantBufferHandle`)
- `VertexElement` / `InputElementDesc` types

For the stub, use placeholder types (void*, uint64_t, etc.) that will be replaced with actual Metal types once metalState and metalHW exist.

- [ ] **Step 2: Create metalHW.h stub**

Stub `CHW` class with the same public interface as `src/Layers/xrRenderGL/glHW.h`:
- `CreateDevice(SDL_Window*)`, `DestroyDevice()`, `Reset()`, `Present()`
- `SetPrimaryAttributes(u32&)`, `BeginScene()`, `EndScene()`
- `GetSurfaceSize()`, `GetDeviceState()`
- `BeginPixEvent()`, `EndPixEvent()`

All method bodies can be empty or return defaults.

- [ ] **Step 3: Create metalHW.cpp stub**

Empty implementations for all CHW methods. Include metalHW.h.

- [ ] **Step 4: Create metalState.h and metalState.cpp stubs**

`metalState` class inheriting from `pureState` (same base as `glState`):
- `static metalState* Create()`
- `void Apply()`
- `void Release()`
- `void UpdateRenderState(u32 name, u32 value)`
- `void UpdateSamplerState(u32 stage, u32 name, u32 value)`

All methods empty/default.

- [ ] **Step 5: Create metalR_Backend_Runtime.h stub**

Stub inline implementations of all CBackend methods that differ per-backend. Reference `src/Layers/xrRenderGL/glR_Backend_Runtime.h` for the complete list:
- `set_FB()`, `get_FB()`, `set_RT()`, `set_ZB()`, `get_RT()`, `get_ZB()`
- `ClearRT()`, `ClearZB()`, `ClearRTRect()`, `ClearZBRect()`
- `set_Format()`, `set_Vertices()`, `set_Indices()`
- `set_PS()`, `set_VS()`, `set_GS()`, `set_PP()`
- `Render()`, `submit()`

All can be empty stubs initially.

- [ ] **Step 6: Create metalr_constants_cache.h stub**

Stub `R_constants` class with empty `set()` and `flush()` methods. Reference `src/Layers/xrRenderGL/glr_constants_cache.h`.

- [ ] **Step 7: Create CMakeLists.txt**

This does NOT build a separate library — the Metal HAL sources are compiled directly into `xrRender_Metal` (same pattern as GL: `xrRenderGL/` sources are listed in `xrRenderPC_GL/CMakeLists.txt`). This CMakeLists.txt is a placeholder comment file documenting the directory's purpose.

- [ ] **Step 8: Verify directory structure**

```bash
ls src/Layers/xrRenderMetal/
```

Expected: CommonTypes.h, metalHW.h, metalHW.cpp, metalState.h, metalState.cpp, metalR_Backend_Runtime.h, metalr_constants_cache.h, CMakeLists.txt

- [ ] **Step 9: Commit**

```bash
git add src/Layers/xrRenderMetal/
git commit -m "feat(sp2): create xrRenderMetal stub directory with type aliases and HAL stubs"
```

---

### Task 3: Create xrRenderPC_Metal CMakeLists.txt and module stubs

Create the Metal render module directory. This is the main build target that compiles all Metal-specific sources plus shared sources from `xrRender/` and `xrRender_R2/`.

**Files:**
- Create: `src/Layers/xrRenderPC_Metal/CMakeLists.txt`
- Create: `src/Layers/xrRenderPC_Metal/stdafx.h`
- Create: `src/Layers/xrRenderPC_Metal/stdafx.cpp`
- Create: `src/Layers/xrRenderPC_Metal/xrRender_Metal.cpp`
- Create: `src/Layers/xrRenderPC_Metal/r2_test_hw.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget.h` (stub)
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget.cpp` (stub)
- Modify: `src/Layers/CMakeLists.txt` — add Metal subdirectory

**Template:** Copy `src/Layers/xrRenderPC_GL/CMakeLists.txt` and adapt:

- [ ] **Step 1: Create stdafx.h**

This is the precompiled header. It defines the backend identity and includes Metal headers:

```cpp
#pragma once

#define RENDER R_GL  // Use same render level as GL (R3 pipeline)
#define USE_METAL

// Metal C++ API
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

// Metal backend types and hardware
#include "Layers/xrRenderMetal/CommonTypes.h"
#include "Layers/xrRenderMetal/metalHW.h"

// Shared render infrastructure
#include "Layers/xrRender/R_Backend.h"
#include "Layers/xrRender/R_Backend_Runtime.h"
#include "Layers/xrRender_R2/r2.h"
#include "metal_rendertarget.h"

// Jitter helper (same as GL)
IC void jitter(CBlender_Compile& C)
{
    // TODO: implement jitter texture samplers for Metal
}
```

- [ ] **Step 2: Create stdafx.cpp**

```cpp
#include "stdafx.h"
```

- [ ] **Step 3: Create xrRender_Metal.cpp**

Module factory, mirroring `src/Layers/xrRenderPC_GL/xrRender_GL.cpp`:

```cpp
#include "stdafx.h"
#include "Include/xrRender/xrRender.h"

namespace xray::render::RENDER_NAMESPACE
{
class RMetalRendererModule final : public RendererModule
{
    xr_vector<std::pair<pcstr, int>> modes;

    const xr_vector<std::pair<pcstr, int>>& ObtainSupportedModes() override
    {
        if (modes.empty() && xrRender_test_hw())
            modes.emplace_back("renderer_r3", 4); // R3 pipeline level
        return modes;
    }

    bool CheckGameRequirements() override
    {
        return FS.exist("$game_shaders$", RImplementation.getShaderPath());
    }

    void SetupEnv(pcstr mode) override
    {
        ps_r2_sun_static = false;
        ps_r2_advanced_pp = true;
        GEnv.Render = &RImplementation;
        GEnv.RenderFactory = &RenderFactoryImpl;
        GEnv.DU = &DUImpl;
        GEnv.UIRender = &UIRenderImpl;
        GEnv.DRender = &DebugRenderImpl;
        xrRender_initconsole();
    }

    void ClearEnv() override
    {
        modes.clear();
        GEnv.Render = nullptr;
        GEnv.RenderFactory = nullptr;
        GEnv.DU = nullptr;
        GEnv.UIRender = nullptr;
        GEnv.DRender = nullptr;
    }
};

static RMetalRendererModule s_metal_module;

RendererModule* GetRendererModule()
{
    return &s_metal_module;
}
} // namespace xray::render::RENDER_NAMESPACE
```

- [ ] **Step 4: Create r2_test_hw.cpp**

```cpp
#include "stdafx.h"

bool xrRender_test_hw()
{
    // Check if Metal is available (any GPU that supports Metal)
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device)
        return false;
    device->release();
    return true;
}
```

- [ ] **Step 5: Create metal_rendertarget.h stub**

Stub `CRenderTarget` class. Reference `src/Layers/xrRenderPC_GL/gl_rendertarget.h`. Include all the same render target members (ref_rt, ref_shader, etc.) but use Metal types where GL-specific types appeared. This is a large class — start with the declaration and empty method bodies.

- [ ] **Step 6: Create metal_rendertarget.cpp stub**

Empty implementations for CRenderTarget methods.

- [ ] **Step 7: Create CMakeLists.txt**

Template from `src/Layers/xrRenderPC_GL/CMakeLists.txt` (439 lines). Key changes:
- Library name: `xrRender_Metal` (was `xrRender_GL`)
- Replace all `../xrRenderGL/` source paths with `../xrRenderMetal/` equivalents
- Replace GL-specific PC sources (`gl_rendertarget*.cpp`) with Metal equivalents
- Remove GLAD/GL sources (`glad/gl.c`, `glad/gl.h`, `KHR/khrplatform.h`)
- Add metal-cpp include path: `"${CMAKE_SOURCE_DIR}/Externals/metal-cpp"`
- Link Metal and QuartzCore frameworks: `"-framework Metal" "-framework QuartzCore" "-framework Foundation"`
- Compile definitions: `XRRENDER_METAL_EXPORTS`, `USE_METAL`, `RENDER_NAMESPACE=render_metal`
- Add `.mm` sources with OBJCXX language flag for metalHW_Apple.mm
- Guard entire file with `if(APPLE)` since Metal only exists on macOS

**IMPORTANT:** Include the exact same shared sources from `../xrRender/` and `../xrRender_R2/` as the GL CMakeLists does — these are the ~100 shared files that compile with RENDER_NAMESPACE.

- [ ] **Step 8: Update src/Layers/CMakeLists.txt**

Add Metal subdirectory, guarded by platform:

```cmake
add_subdirectory(xrAPI)

if (WIN32)
    add_subdirectory(xrRenderPC_R4)
endif()

add_subdirectory(xrRenderPC_GL)

if (APPLE)
    add_subdirectory(xrRenderPC_Metal)
endif()
```

- [ ] **Step 9: Commit**

```bash
git add src/Layers/xrRenderPC_Metal/ src/Layers/CMakeLists.txt
git commit -m "feat(sp2): create xrRenderPC_Metal build target with module stubs"
```

---

## Phase 2: Engine Integration

### Task 4: Change renderer array from std::array to std::span

The engine currently hardcodes `std::array<RendererModule*, 2>` in 6 files. Change to `std::span<RendererModule*>` so adding/removing backends doesn't require updating the count.

**Files:**
- Modify: `src/xr_3da/entry_point.cpp:24-30`
- Modify: `src/xrEngine/EngineAPI.h:81`
- Modify: `src/xrEngine/EngineAPI.cpp:126`
- Modify: `src/xrEngine/Engine.h:36`
- Modify: `src/xrEngine/Engine.cpp:64`
- Modify: `src/xrEngine/x_ray.h:44`
- Modify: `src/xrEngine/x_ray.cpp` (constructor)

- [ ] **Step 1: Read all 6 files to find exact signatures**

Read each file and note the exact line numbers and signatures containing `std::array<RendererModule*, 2>`.

- [ ] **Step 2: Add `<span>` include where needed**

Each file that uses the type needs `#include <span>`. Check what's already included.

- [ ] **Step 3: Change EngineAPI.h**

```cpp
// Before:
void CreateRendererList(const std::array<RendererModule*, 2>& modules);
// After:
void CreateRendererList(std::span<RendererModule* const> modules);
```

- [ ] **Step 4: Change EngineAPI.cpp**

Update the function definition signature to match the header.

- [ ] **Step 5: Change Engine.h and Engine.cpp**

```cpp
// Before:
void Initialize(GameModule* game, const std::array<RendererModule*, 2>& modules);
// After:
void Initialize(GameModule* game, std::span<RendererModule* const> modules);
```

- [ ] **Step 6: Change x_ray.h and x_ray.cpp**

```cpp
// Before:
CApplication(pcstr commandLine, GameModule* game, const std::array<RendererModule*, 2>& modules);
// After:
CApplication(pcstr commandLine, GameModule* game, std::span<RendererModule* const> modules);
```

- [ ] **Step 7: Change entry_point.cpp**

```cpp
// Before:
std::array<RendererModule*, 2> s_render_modules = { ... };
// After:
std::array s_render_modules = {
#ifdef XR_PLATFORM_WINDOWS
    xray::render::render_r4::GetRendererModule(),
#endif
#ifdef XR_PLATFORM_APPLE
    xray::render::render_metal::GetRendererModule(),
#endif
    xray::render::render_gl::GetRendererModule(),
};
```

Note: `std::array` with CTAD deduces the size automatically. Metal is listed before GL (preferred on macOS). The `std::span` parameter accepts any size array.

- [ ] **Step 8: Build to verify compilation**

```bash
cmake --build build -j10 2>&1 | head -50
```

Expected: Compiles without errors (Metal module not yet linked, but the span/array change should be clean).

- [ ] **Step 9: Commit**

```bash
git add src/xr_3da/entry_point.cpp src/xrEngine/EngineAPI.h src/xrEngine/EngineAPI.cpp \
        src/xrEngine/Engine.h src/xrEngine/Engine.cpp src/xrEngine/x_ray.h src/xrEngine/x_ray.cpp
git commit -m "refactor: change renderer module array from std::array<*,2> to std::span"
```

---

### Task 5: Add render_metal namespace to xrRender.h and friend declarations

Register the Metal renderer in the engine's header and add friend class access for the Metal namespace.

**Files:**
- Modify: `src/Include/xrRender/xrRender.h`
- Modify: `src/xrEngine/StatGraph.h`
- Modify: `src/xrEngine/GameFont.h`
- Modify: `src/xrEngine/Rain.h`
- Modify: `src/xrEngine/thunderbolt.h`
- Modify: `src/xrEngine/Environment.h`
- Modify: `src/xrEngine/xr_efflensflare.h`

- [ ] **Step 1: Read all 7 files to find exact patterns**

Read each file and note the existing `render_gl` namespace/friend patterns.

- [ ] **Step 2: Update xrRender.h**

Add Metal export macro and namespace:

```cpp
#ifdef XRRENDER_METAL_EXPORTS
#    define XRRENDER_METAL_API XR_EXPORT
#else
#    define XRRENDER_METAL_API XR_IMPORT
#endif

// Inside namespace xray::render:
#ifdef XR_PLATFORM_APPLE
namespace render_metal
{
XRRENDER_METAL_API RendererModule* GetRendererModule();
}
#endif
```

- [ ] **Step 3: Add friend declarations to all 6 engine headers**

For each file (StatGraph.h, GameFont.h, Rain.h, thunderbolt.h, Environment.h, xr_efflensflare.h), add the Metal namespace forward declaration and friend class. Follow the exact pattern used for `render_gl`. Example for StatGraph.h:

```cpp
// Add to namespace forward declarations:
namespace render_metal
{
class dxStatGraphRender;
}

// Add to friend list:
friend class xray::render::render_metal::dxStatGraphRender;
```

Guard with `#ifdef XR_PLATFORM_APPLE` if you want, but the friend declarations are harmless even on other platforms (the class just won't exist).

- [ ] **Step 4: Build to verify**

```bash
cmake --build build -j10 2>&1 | head -50
```

Expected: Clean compilation. Friend declarations for non-existent classes are valid C++.

- [ ] **Step 5: Commit**

```bash
git add src/Include/xrRender/xrRender.h src/xrEngine/StatGraph.h src/xrEngine/GameFont.h \
        src/xrEngine/Rain.h src/xrEngine/thunderbolt.h src/xrEngine/Environment.h \
        src/xrEngine/xr_efflensflare.h
git commit -m "feat(sp2): register render_metal namespace and add friend declarations"
```

---

## Phase 3: Shared Code USE_METAL Branches

This is the largest phase. The shared `xrRender/` code has ~70 `#ifdef USE_DX11 / USE_OGL` sites that need `USE_METAL` branches. Without these, the shared code won't compile for the Metal backend.

**Strategy:** For most sites, Metal behaves like OpenGL (uses the same type patterns from CommonTypes.h). Many `USE_METAL` branches will be identical or near-identical to `USE_OGL`. The key differences are:
- Metal uses `MTL::Texture*` instead of `GLuint` for render targets
- Metal uses `MTL::RenderCommandEncoder*` for draw recording
- Metal has no program pipeline concept (`pp` in GL) — uses PSOs instead
- Metal has no separate shader objects — shaders are baked into PSOs

### Task 6: Add USE_METAL branches to R_Backend.h

This is the most critical file — the unified backend class that all rendering goes through.

**Files:**
- Modify: `src/Layers/xrRender/R_Backend.h` (~22 ifdef sites)

**Context:** Read the full file first. For each `#ifdef USE_DX11 / #elif defined(USE_OGL)` block, add `#elif defined(USE_METAL)`. Reference `src/Layers/xrRenderGL/CommonTypes.h` for how GL maps types, and mirror that approach with Metal types from `src/Layers/xrRenderMetal/CommonTypes.h`.

- [ ] **Step 1: Read R_Backend.h in full**

Identify every `#ifdef` site. The key areas are:
1. **Includes** (line ~10-23): Backend-specific headers
2. **R_LOD member** (~74): DX11 only
3. **Constant buffer arrays** (~78-93): DX11 only (ref_cbuffer)
4. **Render target storage** (~96-105): DX11=ID3D*, OGL=GLuint, Metal=MTL::Texture*
5. **Shader storage** (~119-147): DX11=ID3D*, OGL=GLuint+pp, Metal=PSO handles
6. **Texture arrays** (~174-178): DX11 extra stages (Hull/Domain/Compute)
7. **Method signatures** (~252-524): set_RT, set_ZB, Clear*, set_PS/VS/GS, etc.
8. **DX11 internals** (~584-603): StateManager, ApplyVertexLayout, etc.

- [ ] **Step 2: Add Metal includes block**

At the top where backend headers are included:
```cpp
#elif defined(USE_METAL)
#include "Layers/xrRenderMetal/metalR_Backend_Runtime.h"
```

- [ ] **Step 3: Add Metal render target members**

```cpp
#elif defined(USE_METAL)
    MTL::Texture*               pRT[4];
    MTL::Texture*               pZB;
    MTL::RenderCommandEncoder*  pEncoder;      // Active encoder
    MTL::CommandBuffer*         pCommandBuffer; // Current frame
```

- [ ] **Step 4: Add Metal shader members**

Metal doesn't have separate shader objects like GL. Shaders are compiled into PSOs. But the shared code expects `ps`, `vs`, `gs` members. Use opaque handles:

```cpp
#elif defined(USE_METAL)
    u32 ps;     // Fragment function index
    u32 vs;     // Vertex function index
    u32 gs;     // Geometry function index (unused on Metal, always 0)
```

- [ ] **Step 5: Add Metal method signatures**

For `set_RT`, `set_ZB`, `ClearRT`, `ClearZB`, etc., add Metal signatures using `MTL::Texture*`:

```cpp
#elif defined(USE_METAL)
    void set_RT(MTL::Texture* RT, u32 ID = 0);
    MTL::Texture* get_RT(u32 ID = 0);
    void set_ZB(MTL::Texture* ZB);
    MTL::Texture* get_ZB();
    void ClearRT(MTL::Texture* rt, const Fcolor& color);
    void ClearZB(MTL::Texture* zb, float depth);
    void ClearZB(MTL::Texture* zb, float depth, u8 stencil);
    // ... etc
```

- [ ] **Step 6: Handle shader setter signatures**

```cpp
#elif defined(USE_METAL)
    void set_PS(u32 _ps, LPCSTR _n = nullptr);
    void set_VS(u32 _vs, LPCSTR _n = nullptr);
    void set_GS(u32 _gs, LPCSTR _n = nullptr);
```

- [ ] **Step 7: Build to verify R_Backend.h compiles**

The Metal target won't fully link yet, but should parse without errors:

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -20
```

Expected: GL target still compiles cleanly (Metal branches are `#elif` and don't affect GL).

- [ ] **Step 8: Commit**

```bash
git add src/Layers/xrRender/R_Backend.h
git commit -m "feat(sp2): add USE_METAL branches to R_Backend.h"
```

---

### Task 7a: Add USE_METAL branches to shader and texture type headers

These files define how shader handles and texture resources are stored per-backend. Metal uses opaque u32 indices for shader functions (similar to GL) but needs Metal-specific texture handle types.

**Files:**
- Modify: `src/Layers/xrRender/SH_Atomic.h` (~9 ifdef sites)
- Modify: `src/Layers/xrRender/SH_Atomic.cpp` (~4 ifdef sites)
- Modify: `src/Layers/xrRender/SH_Texture.h` (~7 ifdef sites)
- Modify: `src/Layers/xrRender/Shader.h` (~1 ifdef site)

**Key design decisions for Metal:**

- [ ] **Step 1: Read SH_Atomic.h in full**

This defines `SVS`, `SPS`, `SGS`, `SHS`, `SDS` — shader storage types. Each has a backend-specific member:
- DX11: `ID3DVertexShader* sh;`
- OGL: `GLuint sh;`

Metal uses opaque u32 handles (function indices into a `MTL::Library`):
```cpp
#elif defined(USE_METAL)
    u32 sh;  // Metal shader function index
```

Also note the `SInputSignature` struct (DX11-only) and whether Metal needs an equivalent.

- [ ] **Step 2: Add USE_METAL to all 5 shader types in SH_Atomic.h**

For each of SVS, SPS, SGS, SHS, SDS, add the `u32 sh` member under `#elif defined(USE_METAL)`. SHS and SDS are unused on Metal (no tessellation) but must exist for shared code compilation.

- [ ] **Step 3: Update SH_Atomic.cpp — shader destruction**

Metal's shader "destruction" releases the function reference. Since we use u32 indices, destruction is a no-op (the MTL::Library owns the functions):
```cpp
#elif defined(USE_METAL)
    sh = 0;  // No GPU resource to release
```

- [ ] **Step 4: Read SH_Texture.h in full**

This defines `CTexture` with backend-specific members. Key differences:
- **Max texture counts:** DX11 uses 256-unit spacing, OGL uses actual counts. Metal follows OGL pattern.
- **ResourceShaderType enum:** Stage offsets differ per backend.
- **surface_set/surface_get:** DX11=`ID3DBaseTexture*`, OGL=`GLenum target + GLuint`. Metal=`MTL::Texture*`.
- **GetImTextureID():** Returns the handle for ImGui.

- [ ] **Step 5: Add USE_METAL branches to SH_Texture.h**

```cpp
#elif defined(USE_METAL)
    // Texture handle
    MTL::Texture* pSurface = nullptr;

    // Stage counts (same as OGL)
    enum MaxTextures {
        mtMaxPixelShaderTextures = 16,
        mtMaxVertexShaderTextures = 4,
        mtMaxGeometryShaderTextures = 16,
        mtMaxCombinedShaderTextures = 36,
    };

    // Surface access
    void surface_set(MTL::Texture* surf);
    MTL::Texture* surface_get() const;

    // ImGui integration
    ImTextureID GetImTextureID() { return pSurface; }
```

- [ ] **Step 6: Update Shader.h — SPass references**

Metal doesn't have GL's program pipeline (`pp`). The PSO is resolved at draw time from (vs + ps + state). For SPass:
```cpp
#elif defined(USE_METAL)
    // No ref_pp — PSO lookup happens in CBackend::Render()
```

- [ ] **Step 7: Build GL target to verify no regressions**

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -20
```

- [ ] **Step 8: Commit**

```bash
git add src/Layers/xrRender/SH_Atomic.h src/Layers/xrRender/SH_Atomic.cpp \
        src/Layers/xrRender/SH_Texture.h src/Layers/xrRender/Shader.h
git commit -m "feat(sp2): add USE_METAL branches to shader/texture type headers"
```

---

### Task 7b: Add USE_METAL branches to backend runtime and state files

These files handle per-frame rendering operations — state application, constant routing, backend initialization.

**Files:**
- Modify: `src/Layers/xrRender/R_Backend_Runtime.h` (~8 ifdef sites)
- Modify: `src/Layers/xrRender/R_Backend_Runtime.cpp` (~4 ifdef sites)
- Modify: `src/Layers/xrRender/R_Backend.cpp`
- Modify: `src/Layers/xrRender/tss_def.h` (~1 ifdef site)
- Modify: `src/Layers/xrRender/tss_def.cpp` (~4 ifdef sites)

- [ ] **Step 1: Read R_Backend_Runtime.h in full**

This file includes the backend-specific runtime header and dispatches several methods. Add:
```cpp
#elif defined(USE_METAL)
#include "Layers/xrRenderMetal/metalR_Backend_Runtime.h"
```

- [ ] **Step 2: Add Metal branches to set_States() dispatch**

```cpp
#elif defined(USE_METAL)
IC void CBackend::set_States(ID3DState* _state)
{
    if (state != _state) { state = _state; state->Apply(); stat.states++; }
}
```

Metal's `Apply()` records state for the next PSO lookup (doesn't issue GPU commands directly like GL).

- [ ] **Step 3: Add Metal branches to set_Pass() dispatch**

Metal's set_Pass sets vs/ps (no pp, no hs/ds/cs):
```cpp
#elif defined(USE_METAL)
    set_VS(pass.vs->sh);
    set_PS(pass.ps->sh);
    // No pp, hs, ds, cs on Metal
```

- [ ] **Step 4: Update R_Backend_Runtime.cpp — Invalidate()**

Add Metal branch to initialize Metal-specific members:
```cpp
#elif defined(USE_METAL)
    pEncoder = nullptr;
    pCommandBuffer = nullptr;
    for (auto& rt : pRT) rt = nullptr;
    pZB = nullptr;
```

- [ ] **Step 5: Update R_Backend.cpp**

Add Metal branches to `OnFrameBegin()`, `OnFrameEnd()`, `SetupStates()`, `OnDeviceCreate()`, `OnDeviceDestroy()`.

- [ ] **Step 6: Update tss_def.h — include Metal state header**

```cpp
#elif defined(USE_METAL)
#include "Layers/xrRenderMetal/metalState.h"
```

- [ ] **Step 7: Update tss_def.cpp — state creation**

Metal's `ID3DState::Create()` pattern (matches GL: no arguments):
```cpp
#elif defined(USE_METAL)
    ID3DState::Create();
```

Also add Metal branches for `SimulatorStates` if DX11-only code blocks exist.

- [ ] **Step 8: Build GL target to verify no regressions**

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -20
```

- [ ] **Step 9: Commit**

```bash
git add src/Layers/xrRender/R_Backend_Runtime.h src/Layers/xrRender/R_Backend_Runtime.cpp \
        src/Layers/xrRender/R_Backend.cpp src/Layers/xrRender/tss_def.h src/Layers/xrRender/tss_def.cpp
git commit -m "feat(sp2): add USE_METAL branches to backend runtime and state files"
```

---

### Task 7c: Add USE_METAL branches to resource management and remaining files

Simple files where Metal mostly follows the OGL pattern.

**Files:**
- Modify: `src/Layers/xrRender/ColorMapManager.cpp` (~3 ifdef sites)
- Modify: `src/Layers/xrRender/r_constants.cpp` (~3 ifdef sites)
- Modify: `src/Layers/xrRender/r_constants.h`
- Modify: `src/Layers/xrRender/ResourceManager.cpp` (~2 ifdef sites)
- Modify: `src/Layers/xrRender/Debug/dxPixEventWrapper.h` (~1 ifdef site)

- [ ] **Step 1: Update ColorMapManager.cpp**

Texture surface binding. 3 sites that call `surface_set()` — Metal uses `MTL::Texture*`:
```cpp
#elif defined(USE_METAL)
    e0->surface_set(t0->surface_get());
```
(Same pattern as OGL but with Metal texture types via CommonTypes.h)

- [ ] **Step 2: Update r_constants.cpp and r_constants.h**

Constant routing. Metal routes to fragment/vertex shaders like GL. The key difference: OGL uses `C->ps.location` / `C->vs.location` / `C->pp` for program pipeline. Metal uses `C->ps` / `C->vs` (no pp):
```cpp
#elif defined(USE_METAL)
    // Route like OGL but without pp (program pipeline)
    if (C->destination & RC_dest_pixel) set(C, C->ps, A);
    if (C->destination & RC_dest_vertex) set(C, C->vs, A);
```

- [ ] **Step 3: Update ResourceManager.cpp**

Resource creation dispatch. Metal-specific shader/resource creation paths.

- [ ] **Step 4: Update dxPixEventWrapper.h**

GPU debug markers. Metal uses `MTL::CommandBuffer::pushDebugGroup()` / `popDebugGroup()`:
```cpp
#elif defined(USE_METAL)
    // Metal GPU debug markers via command buffer
    HW.BeginPixEvent(name);
    // ...
    HW.EndPixEvent();
```

- [ ] **Step 5: Build GL target to verify no regressions**

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
git add src/Layers/xrRender/ColorMapManager.cpp src/Layers/xrRender/r_constants.cpp \
        src/Layers/xrRender/r_constants.h src/Layers/xrRender/ResourceManager.cpp \
        src/Layers/xrRender/Debug/dxPixEventWrapper.h
git commit -m "feat(sp2): add USE_METAL branches to resource management and debug markers"
```

---

## Phase 4: Metal Hardware Abstraction

### Task 8: Implement CommonTypes.h with Metal type mappings

Fill in the real type aliases mapping D3D types to Metal equivalents.

**Files:**
- Modify: `src/Layers/xrRenderMetal/CommonTypes.h`

**Reference:** `src/Layers/xrRenderGL/CommonTypes.h` for the complete list of types needed.

- [ ] **Step 1: Read GL CommonTypes.h in full**

Note every typedef, struct, and enum. These are the exact types the shared code expects.

- [ ] **Step 2: Implement D3D_VIEWPORT struct**

```cpp
struct D3D_VIEWPORT
{
    float TopLeftX;
    float TopLeftY;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
};
```

- [ ] **Step 3: Implement comparison and state enums**

Map D3D comparison functions to `MTL::CompareFunction` values:
```cpp
enum D3D_COMPARISON_FUNC
{
    D3D_COMPARISON_NEVER = 1,        // MTL::CompareFunctionNever
    D3D_COMPARISON_LESS = 2,         // MTL::CompareFunctionLess
    D3D_COMPARISON_EQUAL = 3,        // MTL::CompareFunctionEqual
    D3D_COMPARISON_LESS_EQUAL = 4,   // MTL::CompareFunctionLessEqual
    // ... etc
};
```

- [ ] **Step 4: Implement state structs**

`D3D_DEPTH_STENCIL_STATE` and `D3D_BLEND_STATE` with the same member layout as the GL version.

- [ ] **Step 5: Implement buffer handle typedefs**

```cpp
using IndexBufferHandle = MTL::Buffer*;
using VertexBufferHandle = MTL::Buffer*;
using ConstantBufferHandle = MTL::Buffer*;
```

- [ ] **Step 6: Implement ID3DState typedef**

```cpp
class metalState; // forward declaration
using ID3DState = metalState;
```

- [ ] **Step 7: Implement vertex element types**

Use the same `D3DVERTEXELEMENT9`-compatible struct as GL.

- [ ] **Step 8: Build GL target to verify no regressions**

CommonTypes.h is Metal-only (only included when USE_METAL is defined), but verify the GL target still compiles:

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -10
```

- [ ] **Step 9: Commit**

```bash
git add src/Layers/xrRenderMetal/CommonTypes.h
git commit -m "feat(sp2): implement Metal CommonTypes.h with D3D type aliases"
```

---

### Task 9: Implement metalHW — Metal device and presentation

Fill in the `CHW` class with real Metal device initialization.

**Files:**
- Modify: `src/Layers/xrRenderMetal/metalHW.h`
- Modify: `src/Layers/xrRenderMetal/metalHW.cpp`
- Create: `src/Layers/xrRenderMetal/metalHW_Apple.mm`

**Reference:** `src/Layers/xrRenderGL/glHW.h` and `glHW.cpp` for the interface contract.

- [ ] **Step 1: Read glHW.h and glHW.cpp in full**

Understand every method and member. The Metal CHW needs the same public interface.

- [ ] **Step 2: Implement CHW members**

```cpp
class CHW : public pureAppActivate, public pureAppDeactivate
{
public:
    MTL::Device*        m_device;
    MTL::CommandQueue*  m_commandQueue;
    CA::MetalLayer*     m_metalLayer;
    MTL::CommandBuffer* m_commandBuffer;    // Per-frame
    CA::MetalDrawable*  m_currentDrawable;  // Per-frame

    SDL_MetalView       m_metalView;        // SDL Metal view handle
    SDL_Window*         m_window;

    CHWCaps Caps;
    u32 BackBufferCount = 2;
    u32 CurrentBackBuffer = 0;
    pcstr AdapterName = "Unknown Metal Device";

    void CreateDevice(SDL_Window* hWnd);
    void DestroyDevice();
    void Reset(SDL_Window* hWnd);
    void Present();
    static void SetPrimaryAttributes(u32& windowFlags);
    void BeginScene();
    void EndScene();
    static std::pair<u32, u32> GetSurfaceSize();
    DeviceState GetDeviceState();
    void BeginPixEvent(pcstr name);
    void EndPixEvent();

    void OnAppActivate() override;
    void OnAppDeactivate() override;
};
```

- [ ] **Step 3: Implement CreateDevice()**

```cpp
void CHW::CreateDevice(SDL_Window* hWnd)
{
    m_window = hWnd;
    m_device = MTL::CreateSystemDefaultDevice();
    R_ASSERT(m_device);

    m_commandQueue = m_device->newCommandQueue();
    R_ASSERT(m_commandQueue);

    AdapterName = m_device->name()->utf8String();
    Msg("* Metal device: %s", AdapterName);

    // Create Metal view via SDL
    m_metalView = SDL_Metal_CreateView(hWnd);
    R_ASSERT(m_metalView);

    // Get CAMetalLayer from SDL view (via ObjC++ bridge)
    m_metalLayer = GetMetalLayer(m_metalView); // Implemented in metalHW_Apple.mm

    // Configure layer
    m_metalLayer->setDevice(m_device);
    m_metalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    m_metalLayer->setFramebufferOnly(false); // Allow readback for screenshots

    // Query capabilities
    Caps.max_texture_size = 16384;
    Caps.msaa_supported = true;
    Caps.depth32_supported = true;
}
```

- [ ] **Step 4: Implement Present(), BeginScene(), EndScene()**

```cpp
void CHW::BeginScene()
{
    m_commandBuffer = m_commandQueue->commandBuffer();
    m_currentDrawable = m_metalLayer->nextDrawable();
}

void CHW::EndScene()
{
    // Render pass encoding is done by backend
}

void CHW::Present()
{
    if (m_currentDrawable)
        m_commandBuffer->presentDrawable(m_currentDrawable);
    m_commandBuffer->commit();
    m_commandBuffer = nullptr;
    m_currentDrawable = nullptr;
}
```

- [ ] **Step 5: Create metalHW_Apple.mm**

The thin ObjC++ bridge for getting CAMetalLayer from SDL:

```objc
#include "metalHW.h"
#import <SDL_metal.h>
#import <QuartzCore/CAMetalLayer.h>

CA::MetalLayer* GetMetalLayer(SDL_MetalView view)
{
    // SDL_Metal_GetLayer returns a CAMetalLayer* as void*
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
    return (__bridge CA::MetalLayer*)layer;
}
```

- [ ] **Step 6: Implement remaining CHW methods**

`DestroyDevice()`, `Reset()`, `SetPrimaryAttributes()`, `GetSurfaceSize()`, etc.

- [ ] **Step 7: Commit**

```bash
git add src/Layers/xrRenderMetal/metalHW.h src/Layers/xrRenderMetal/metalHW.cpp \
        src/Layers/xrRenderMetal/metalHW_Apple.mm
git commit -m "feat(sp2): implement Metal hardware abstraction (CHW)"
```

---

### Task 10: Implement metalState — PSO cache and state management

This is one of the most architecturally important components. Metal bakes all render state into Pipeline State Objects (PSOs), unlike GL's stateful API.

**Files:**
- Modify: `src/Layers/xrRenderMetal/metalState.h`
- Modify: `src/Layers/xrRenderMetal/metalState.cpp`
- Create: `src/Layers/xrRenderMetal/metalStateUtils.h`
- Create: `src/Layers/xrRenderMetal/metalStateUtils.cpp`

- [ ] **Step 1: Read glState.h and glState.cpp**

Understand how GL manages state and what `Apply()` does.

- [ ] **Step 2: Design PSO hash key**

```cpp
struct PSOKey
{
    u32 vertexFunction;
    u32 fragmentFunction;
    u32 vertexDescriptorHash;
    MTL::PixelFormat colorFormats[4];
    MTL::PixelFormat depthFormat;
    // Blend state
    bool blendEnabled;
    MTL::BlendFactor srcRGB, dstRGB, srcAlpha, dstAlpha;
    MTL::BlendOperation blendOpRGB, blendOpAlpha;
    u32 colorWriteMask;

    size_t hash() const;
    bool operator==(const PSOKey&) const;
};
```

- [ ] **Step 3: Implement metalState class**

```cpp
class metalState
{
public:
    // Render state (D3D-compatible, same as glState)
    D3DCULL rasterizerCullMode;
    D3D_DEPTH_STENCIL_STATE m_pDepthStencilState;
    D3D_BLEND_STATE m_pBlendState;
    float m_uiMipLODBias;

    // PSO cache (Metal-specific)
    static std::unordered_map<size_t, MTL::RenderPipelineState*> s_psoCache;
    static std::unordered_map<size_t, MTL::DepthStencilState*> s_dssCache;

    static metalState* Create();
    void Apply();
    void Release();
    void UpdateRenderState(u32 name, u32 value);
    void UpdateSamplerState(u32 stage, u32 name, u32 value);

    // Metal-specific: get or create PSO for current state + shader combo
    MTL::RenderPipelineState* GetOrCreatePSO(
        MTL::Function* vertexFunc,
        MTL::Function* fragmentFunc,
        MTL::VertexDescriptor* vertexDesc,
        MTL::RenderPassDescriptor* rpDesc);

    MTL::DepthStencilState* GetOrCreateDSS();
};
```

- [ ] **Step 4: Implement PSO cache lookup**

```cpp
MTL::RenderPipelineState* metalState::GetOrCreatePSO(...)
{
    PSOKey key = BuildKey(vertexFunc, fragmentFunc, vertexDesc, rpDesc, m_pBlendState);
    size_t h = key.hash();

    auto it = s_psoCache.find(h);
    if (it != s_psoCache.end())
        return it->second;

    // Create new PSO
    auto desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vertexFunc);
    desc->setFragmentFunction(fragmentFunc);
    desc->setVertexDescriptor(vertexDesc);
    // Configure color attachments, blend state from m_pBlendState
    // ...

    NS::Error* error = nullptr;
    auto pso = HW.m_device->newRenderPipelineState(desc, &error);
    R_ASSERT2(pso, error ? error->localizedDescription()->utf8String() : "PSO creation failed");

    s_psoCache[h] = pso;
    desc->release();
    return pso;
}
```

- [ ] **Step 5: Implement metalStateUtils**

Convert D3D enums to Metal enums:
- `D3DCULL` -> `MTL::CullMode`
- `D3DCMP_*` -> `MTL::CompareFunction`
- `D3DSTENCILOP_*` -> `MTL::StencilOperation`
- `D3DBLEND_*` -> `MTL::BlendFactor`
- `D3DBLENDOP_*` -> `MTL::BlendOperation`

- [ ] **Step 6: Commit**

```bash
git add src/Layers/xrRenderMetal/metalState.h src/Layers/xrRenderMetal/metalState.cpp \
        src/Layers/xrRenderMetal/metalStateUtils.h src/Layers/xrRenderMetal/metalStateUtils.cpp
git commit -m "feat(sp2): implement Metal state management and PSO cache"
```

---

### Task 11: Implement metalR_Backend_Runtime.h — backend inline methods

Fill in all the CBackend inline methods that are Metal-specific.

**Files:**
- Modify: `src/Layers/xrRenderMetal/metalR_Backend_Runtime.h`

**Reference:** `src/Layers/xrRenderGL/glR_Backend_Runtime.h` for the complete method list.

- [ ] **Step 1: Read glR_Backend_Runtime.h in full**

Note every inline method and what it does.

- [ ] **Step 2: Implement render target methods**

Metal render targets are `MTL::Texture*` pointers. Setting them configures the next render pass descriptor:

```cpp
IC void CBackend::set_RT(MTL::Texture* RT, u32 ID)
{
    if (pRT[ID] != RT)
    {
        pRT[ID] = RT;
        stat.target_rt++;
    }
}

IC MTL::Texture* CBackend::get_RT(u32 ID) { return pRT[ID]; }
IC void CBackend::set_ZB(MTL::Texture* ZB) { if (pZB != ZB) { pZB = ZB; stat.target_zb++; } }
IC MTL::Texture* CBackend::get_ZB() { return pZB; }
```

- [ ] **Step 3: Implement clear methods**

Metal clears happen via render pass load actions, not explicit clear calls. Store the clear values and apply them when the next render pass begins:

```cpp
IC void CBackend::ClearRT(MTL::Texture* rt, const Fcolor& color)
{
    // Metal: clear is configured on the render pass descriptor loadAction
    // Store clear color for next render pass that uses this RT
    // Implementation deferred to render pass setup
}
```

- [ ] **Step 4: Implement buffer binding methods**

```cpp
IC void CBackend::set_Vertices(MTL::Buffer* _vb, u32 _vb_stride)
{
    if (vb != _vb || vb_stride != _vb_stride)
    {
        vb = _vb;
        vb_stride = _vb_stride;
        stat.vb++;
    }
}
```

- [ ] **Step 5: Implement shader setting methods**

Metal's shader setting just records the function index; actual binding happens at draw time via PSO:

```cpp
IC void CBackend::set_PS(u32 _ps, LPCSTR _n)
{
    if (ps != _ps) { ps = _ps; stat.ps++; }
}
```

- [ ] **Step 6: Implement Render() — the draw call**

This is the most complex method. It must:
1. Ensure a render command encoder is active
2. Build/lookup the PSO from current state + shaders + vertex layout
3. Set the PSO on the encoder
4. Bind vertex/index buffers
5. Issue the draw call

```cpp
IC void CBackend::Render(D3DPRIMITIVETYPE T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
    // Get or create PSO for current state
    // pEncoder->setRenderPipelineState(pso);
    // pEncoder->setVertexBuffer(vb, 0, 0);
    // pEncoder->drawIndexedPrimitives(...)
    stat.render.calls++;
    stat.render.polys += PC;
    stat.render.verts += countV;
}
```

- [ ] **Step 7: Commit**

```bash
git add src/Layers/xrRenderMetal/metalR_Backend_Runtime.h
git commit -m "feat(sp2): implement Metal backend runtime inline methods"
```

---

### Task 12: Implement Metal constants, buffers, and vertex input

**Files:**
- Modify: `src/Layers/xrRenderMetal/metalr_constants_cache.h`
- Create: `src/Layers/xrRenderMetal/metalr_constants.cpp`
- Create: `src/Layers/xrRenderMetal/metalBufferUtils.cpp`
- Create: `src/Layers/xrRenderMetal/metalVertexInput.h`
- Create: `src/Layers/xrRenderMetal/metalVertexInput.cpp`
- Create: `src/Layers/xrRenderMetal/metalConstantBuffer.h`
- Create: `src/Layers/xrRenderMetal/metalConstantBuffer.cpp`

- [ ] **Step 1: Implement R_constants for Metal**

Metal uses buffer bindings for shader constants (not individual uniform calls like GL). The `R_constants` class manages a per-frame constant buffer:

```cpp
class ECORE_API R_constants
{
public:
    void set(R_constant* C, R_constant_load& L, const Fmatrix& A);
    void set(R_constant* C, R_constant_load& L, const Fvector4& A);
    void set(R_constant* C, R_constant_load& L, float x, float y, float z, float w);
    void set(R_constant* C, R_constant_load& L, float A);
    void set(R_constant* C, R_constant_load& L, int A);

    // Metal: flush writes the dirty constant buffer region to the encoder
    void flush();
};
```

- [ ] **Step 2: Implement metalr_constants.cpp**

Constant setting writes to a CPU-side staging buffer. `flush()` calls `setVertexBytes` / `setFragmentBytes` or binds the constant buffer to the encoder.

- [ ] **Step 3: Implement metalBufferUtils.cpp**

Buffer creation utilities mirroring `src/Layers/xrRenderGL/glBufferUtils.cpp`:
- `CreateVertexBuffer(MTL::Buffer**, void* data, u32 size)`
- `CreateIndexBuffer(MTL::Buffer**, void* data, u32 size)`
- `DestroyVertexBuffer(MTL::Buffer*)`
- `DestroyIndexBuffer(MTL::Buffer*)`

- [ ] **Step 4: Implement metalVertexInput.h/cpp**

Translates X-Ray vertex declarations (`D3DVERTEXELEMENT9` arrays) to `MTL::VertexDescriptor`. This is required for PSO creation — Metal must know the vertex layout at pipeline creation time.

```cpp
class MetalVertexInput
{
public:
    // Convert X-Ray vertex declaration to Metal vertex descriptor
    static MTL::VertexDescriptor* CreateVertexDescriptor(
        const D3DVERTEXELEMENT9* elements, u32 stride);

    // Hash a vertex descriptor for PSO cache key
    static u32 HashVertexDescriptor(const D3DVERTEXELEMENT9* elements);
};
```

Maps D3DDECLUSAGE (POSITION, NORMAL, TEXCOORD, etc.) to Metal vertex attribute formats and buffer layouts.

- [ ] **Step 5: Implement metalConstantBuffer.h/cpp**

MTLBuffer-backed constant buffers for shader uniforms:

```cpp
class MetalConstantBuffer
{
    MTL::Buffer* m_buffer;
    u8* m_cpuData;       // CPU-side staging
    u32 m_size;
    bool m_dirty;

public:
    void Create(u32 size);
    void Destroy();
    void Write(u32 offset, const void* data, u32 size);
    void Bind(MTL::RenderCommandEncoder* encoder, u32 index, bool vertex);
};
```

- [ ] **Step 6: Commit**

```bash
git add src/Layers/xrRenderMetal/metalr_constants_cache.h \
        src/Layers/xrRenderMetal/metalr_constants.cpp \
        src/Layers/xrRenderMetal/metalBufferUtils.cpp \
        src/Layers/xrRenderMetal/metalVertexInput.h \
        src/Layers/xrRenderMetal/metalVertexInput.cpp \
        src/Layers/xrRenderMetal/metalConstantBuffer.h \
        src/Layers/xrRenderMetal/metalConstantBuffer.cpp
git commit -m "feat(sp2): implement Metal constants, buffers, and vertex input"
```

---

## Phase 5: Texture and Resource Management

### Task 13: Implement Metal texture loading and binding

**Files:**
- Create: `src/Layers/xrRenderMetal/metalSH_Texture.cpp`
- Create: `src/Layers/xrRenderMetal/metalTexture.cpp`
- Create: `src/Layers/xrRenderMetal/metalTextureUtils.h`
- Create: `src/Layers/xrRenderMetal/metalTextureUtils.cpp`

**Reference:** `src/Layers/xrRenderGL/glSH_Texture.cpp`, `glTexture.cpp`, `glTextureUtils.h/cpp`

- [ ] **Step 1: Read GL texture code**

Understand how DDS textures are loaded, decoded (via GLI library), and bound to GL texture objects.

- [ ] **Step 2: Implement metalTextureUtils — format mapping**

Map DDS/DXGI formats to `MTL::PixelFormat`:
- `DXGI_FORMAT_R8G8B8A8_UNORM` -> `MTL::PixelFormatRGBA8Unorm`
- `DXGI_FORMAT_BC1_UNORM` (DXT1) -> `MTL::PixelFormatBC1_RGBA`
- `DXGI_FORMAT_BC3_UNORM` (DXT5) -> `MTL::PixelFormatBC3_RGBA`
- `DXGI_FORMAT_D24_UNORM_S8_UINT` -> `MTL::PixelFormatDepth32Float_Stencil8` (Apple Silicon doesn't support 24-bit depth)
- etc.

- [ ] **Step 3: Implement metalTexture.cpp — texture loading**

Load DDS via GLI (same library as GL), create `MTL::Texture` via `MTL::TextureDescriptor`:

```cpp
MTL::Texture* CreateTextureFromDDS(MTL::Device* device, const gli::texture& tex)
{
    auto desc = MTL::TextureDescriptor::alloc()->init();
    desc->setPixelFormat(MapFormat(tex.format()));
    desc->setWidth(tex.extent().x);
    desc->setHeight(tex.extent().y);
    desc->setMipmapLevelCount(tex.levels());
    // ...
    auto mtlTex = device->newTexture(desc);
    // Upload mip levels via replaceRegion
    // ...
    return mtlTex;
}
```

- [ ] **Step 4: Implement metalSH_Texture.cpp — CTexture Metal specialization**

Metal-specific implementations of:
- `CTexture::surface_set(MTL::Texture*)` — store texture handle
- `CTexture::surface_get()` — return texture handle
- `CTexture::apply_load()` — bind texture to encoder via `setFragmentTexture`
- `CTexture::apply_normal()` — same
- `CTexture::Load()` — load from disk
- `CTexture::Unload()` — release MTL::Texture

- [ ] **Step 5: Commit**

```bash
git add src/Layers/xrRenderMetal/metalSH_Texture.cpp src/Layers/xrRenderMetal/metalTexture.cpp \
        src/Layers/xrRenderMetal/metalTextureUtils.h src/Layers/xrRenderMetal/metalTextureUtils.cpp
git commit -m "feat(sp2): implement Metal texture loading and binding"
```

---

### Task 14: Implement Metal resource management and render targets

**Files:**
- Create: `src/Layers/xrRenderMetal/metalResourceManager_Resources.cpp`
- Create: `src/Layers/xrRenderMetal/metalResourceManager_Scripting.cpp`
- Create: `src/Layers/xrRenderMetal/metalSH_RT.cpp`
- Create: `src/Layers/xrRenderMetal/metalHWCaps.cpp`

- [ ] **Step 1: Read GL equivalents**

Read `glResourceManager_Resources.cpp`, `glSH_RT.cpp`, `glHWCaps.cpp`.

- [ ] **Step 2: Implement metalResourceManager_Resources.cpp**

Shader and resource creation dispatch for Metal. Creates `MTL::Library` from .metallib files, extracts functions, creates textures/buffers.

- [ ] **Step 3: Implement metalSH_RT.cpp**

Render target creation. Creates `MTL::Texture` objects with appropriate usage flags (`MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead`).

- [ ] **Step 4: Implement metalHWCaps.cpp**

Query Metal device capabilities:
- Max texture size
- MSAA support
- Feature set / GPU family
- Recommended working set size

- [ ] **Step 5: Implement metalResourceManager_Scripting.cpp**

Lua script bindings for Metal resources (likely empty — just needs to compile).

- [ ] **Step 6: Commit**

```bash
git add src/Layers/xrRenderMetal/metalResourceManager_Resources.cpp \
        src/Layers/xrRenderMetal/metalResourceManager_Scripting.cpp \
        src/Layers/xrRenderMetal/metalSH_RT.cpp \
        src/Layers/xrRenderMetal/metalHWCaps.cpp
git commit -m "feat(sp2): implement Metal resource management and render targets"
```

---

## Phase 6: Shader Pipeline

### Task 15: Set up SPIRV-Cross and glslang dependencies

Add SPIRV-Cross and glslang to the build for the shader compilation pipeline: X-Ray GLSL -> preprocessed GLSL -> SPIR-V (via glslang) -> MSL (via SPIRV-Cross) -> .metallib (via Metal compiler).

**Files:**
- Create or modify: `Externals/` — add SPIRV-Cross and glslang
- Modify: `src/Layers/xrRenderPC_Metal/CMakeLists.txt` — link dependencies

- [ ] **Step 1: Add SPIRV-Cross as a git submodule or bundled source**

```bash
cd /Users/rz/OpenXVibeRay/Externals
git submodule add https://github.com/KhronosGroup/SPIRV-Cross.git
```

Or if submodules are not preferred, download and bundle.

- [ ] **Step 2: Add glslang as a dependency**

```bash
cd /Users/rz/OpenXVibeRay/Externals
git submodule add https://github.com/KhronosGroup/glslang.git
```

- [ ] **Step 3: Add CMake integration**

Add `add_subdirectory()` calls in `Externals/CMakeLists.txt` for SPIRV-Cross and glslang. Configure as static libraries.

- [ ] **Step 4: Update Metal CMakeLists.txt to link**

```cmake
target_link_libraries(xrRender_Metal
    PRIVATE
    spirv-cross-msl
    glslang
    SPIRV
    ...
)
```

- [ ] **Step 5: Verify build**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j10 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
git add Externals/ src/Layers/xrRenderPC_Metal/CMakeLists.txt
git commit -m "feat(sp2): add SPIRV-Cross and glslang for shader pipeline"
```

---

### Task 16: Implement shader compilation pipeline

The shader pipeline has two paths: **build-time** (primary, for production) and **runtime** (development convenience). Build-time is the priority per the design spec.

Pipeline: X-Ray GLSL -> engine preprocessor -> standard GLSL -> glslang (SPIR-V) -> SPIRV-Cross (MSL) -> Metal compiler (.metallib)

**Files:**
- Create: `src/Layers/xrRenderMetal/metalShaderCompiler.h`
- Create: `src/Layers/xrRenderMetal/metalShaderCompiler.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_shaders.cpp`
- Modify: `src/Layers/xrRenderPC_Metal/CMakeLists.txt` — add custom commands for build-time compilation

**Context:** X-Ray shaders in `res/gamedata/shaders/gl/*.glsl` use a custom preprocessor with `#include` directives and engine macros. These are **not standard GLSL** — `glslang` cannot consume them directly. The engine already has a shader preprocessor for GL.

- [ ] **Step 1: Map the existing GL shader loading pipeline**

Read these files to understand how GL preprocesses and compiles shaders:
- `src/Layers/xrRenderPC_GL/rgl_shaders.cpp` — main shader loading entry point
- `src/Layers/xrRenderGL/glResourceManager_Resources.cpp` — shader resource creation
- Search for `#include` handling in shader code, `CShaderPreprocessor` or similar class names
- Trace the path from shader file name -> preprocessed GLSL source -> compiled GL shader object

Document: What function takes a shader filename and returns preprocessed GLSL? This is the function we need to call before passing to glslang.

- [ ] **Step 2: Implement metalShaderCompiler — runtime SPIRV-Cross path**

```cpp
class MetalShaderCompiler
{
public:
    // Compile preprocessed GLSL to MSL via SPIRV-Cross
    static std::string CompileGLSLToMSL(
        const std::string& glslSource,
        EShLanguage stage,  // vertex or fragment
        std::string& errorLog);

    // Load pre-compiled .metallib from bundle
    static MTL::Library* LoadMetalLib(
        MTL::Device* device,
        const char* path);

    // Create MTL::Function from library
    static MTL::Function* GetFunction(
        MTL::Library* library,
        const char* name);
};
```

- [ ] **Step 3: Implement CompileGLSLToMSL()**

```cpp
std::string MetalShaderCompiler::CompileGLSLToMSL(
    const std::string& glslSource, EShLanguage stage, std::string& errorLog)
{
    // 1. glslang: GLSL -> SPIR-V
    glslang::TShader shader(stage);
    // Configure shader, set source, parse...
    // Link into program, get SPIR-V binary

    // 2. SPIRV-Cross: SPIR-V -> MSL
    spirv_cross::CompilerMSL msl(spirvBinary);
    spirv_cross::CompilerMSL::Options opts;
    opts.platform = spirv_cross::CompilerMSL::Options::macOS;
    opts.set_msl_version(2, 4); // Metal 2.4 (macOS 12+)
    msl.set_msl_options(opts);

    return msl.compile();
}
```

- [ ] **Step 4: Implement metal_shaders.cpp**

Metal-specific shader loading. Tries pre-compiled .metallib first, falls back to runtime compilation:

```cpp
// In shader loading path:
// 1. Check for pre-compiled .metallib in bundle
// 2. If not found, use engine preprocessor + CompileGLSLToMSL()
// 3. Create MTL::Library from MSL source string
// 4. Extract MTL::Function by name
```

- [ ] **Step 5: Implement build-time shader compilation (primary path)**

Create a CMake custom command that runs the full pipeline at build time:

```cmake
# For each shader in res/gamedata/shaders/gl/:
# 1. Run engine preprocessor (may need a standalone tool or script)
# 2. glslang: preprocessed GLSL -> SPIR-V
# 3. SPIRV-Cross: SPIR-V -> MSL
# 4. xcrun metal: MSL -> .air
# 5. xcrun metallib: .air -> .metallib
add_custom_command(...)
```

The `.metallib` files are bundled with the app. Start with a few key shaders (the main deferred pass vertex + fragment) and expand once the pipeline is validated.

**Note:** If the engine preprocessor is deeply integrated into runtime code and hard to extract as a standalone tool, implement runtime compilation first (Step 2-4) and defer build-time to a follow-up commit. Document the decision.

- [ ] **Step 6: Commit**

```bash
git add src/Layers/xrRenderMetal/metalShaderCompiler.h \
        src/Layers/xrRenderMetal/metalShaderCompiler.cpp \
        src/Layers/xrRenderPC_Metal/metal_shaders.cpp
git commit -m "feat(sp2): implement GLSL->MSL shader compilation pipeline"
```

---

## Phase 7: Render Target Implementation

### Task 17: Implement CRenderTarget for Metal

The render target class manages all G-buffer textures, shadow maps, and post-processing targets.

**Files:**
- Modify: `src/Layers/xrRenderPC_Metal/metal_rendertarget.h`
- Modify: `src/Layers/xrRenderPC_Metal/metal_rendertarget.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_build_textures.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_u_set_rt.cpp`

**Reference:** `src/Layers/xrRenderPC_GL/gl_rendertarget.h` and related `gl_rendertarget_*.cpp` files.

- [ ] **Step 1: Read GL render target code**

Understand all render targets created, their formats, and how they're bound.

- [ ] **Step 2: Implement metal_rendertarget_build_textures.cpp**

Create all G-buffer textures as `MTL::Texture` objects:
- Position (RGBA32F or RGBA16F)
- Normal (RGBA16F)
- Color/albedo (RGBA8)
- Depth (Depth32Float_Stencil8 — Apple Silicon doesn't support D24S8)
- Accumulation buffers
- Shadow map atlas (Depth32Float)
- Bloom textures
- SSAO textures

- [ ] **Step 3: Implement metal_rendertarget_u_set_rt.cpp**

Helper to configure `MTL::RenderPassDescriptor` with the correct color/depth attachments and load/store actions:

```cpp
void CRenderTarget::u_setrt(
    CBackend& cmd_list,
    MTL::Texture* rt0, MTL::Texture* rt1, MTL::Texture* rt2, MTL::Texture* rt3,
    MTL::Texture* zb)
{
    cmd_list.set_RT(rt0, 0);
    cmd_list.set_RT(rt1, 1);
    cmd_list.set_RT(rt2, 2);
    cmd_list.set_RT(rt3, 3);
    cmd_list.set_ZB(zb);
}
```

- [ ] **Step 4: Commit**

```bash
git add src/Layers/xrRenderPC_Metal/metal_rendertarget*.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget.h
git commit -m "feat(sp2): implement Metal render targets and G-buffer creation"
```

---

### Task 18: Implement render phases (accumulation, combine, bloom, SSAO)

**Files:**
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_accum_direct.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_accumulator.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_bloom.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_combine.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_flip.cpp`
- Create: `src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_ssao.cpp`
- Create: `src/Layers/xrRenderPC_Metal/r2_R_sun.cpp`

**Reference:** Corresponding `gl_rendertarget_*.cpp` and `r2_rendertarget_phase_*.cpp` files.

- [ ] **Step 1: Read GL render phase code**

Understand how each phase sets up render targets, shaders, and draw calls. Key files:
- `gl_rendertarget_accum_direct.cpp` — direct light accumulation
- `r2_rendertarget_phase_accumulator.cpp` — light accumulation phase setup
- `r2_rendertarget_phase_bloom.cpp` — bloom post-processing
- `r3_rendertarget_phase_ssao.cpp` — SSAO
- `gl_rendertarget_phase_combine.cpp` — final combine

- [ ] **Step 2: Implement light accumulation phase**

`metal_rendertarget_phase_accumulator.cpp` — sets up the accumulation render pass.
`metal_rendertarget_accum_direct.cpp` — reads G-buffer, computes lighting, writes to accumulation buffer.

- [ ] **Step 3: Implement bloom phase**

`metal_rendertarget_phase_bloom.cpp` — downscale, blur, and composite bloom passes.

- [ ] **Step 4: Implement SSAO phase**

`metal_rendertarget_phase_ssao.cpp` — screen-space ambient occlusion from depth/normal buffers.

- [ ] **Step 5: Implement final combine and present**

`metal_rendertarget_phase_combine.cpp` — composites all accumulation passes.
`metal_rendertarget_phase_flip.cpp` — copies final result to Metal drawable.

- [ ] **Step 6: Implement sun shadow rendering**

`r2_R_sun.cpp` — Metal-specific sun cascade shadow map rendering.

- [ ] **Step 7: Commit**

```bash
git add src/Layers/xrRenderPC_Metal/metal_rendertarget_accum_direct.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_accumulator.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_bloom.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_combine.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_flip.cpp \
        src/Layers/xrRenderPC_Metal/metal_rendertarget_phase_ssao.cpp \
        src/Layers/xrRenderPC_Metal/r2_R_sun.cpp
git commit -m "feat(sp2): implement Metal render phases (lighting, bloom, SSAO, combine, shadows)"
```

---

## Phase 8: Remaining Metal HAL Files

### Task 19: Implement remaining Metal HAL specializations and ImGui

Fill in the remaining Metal-specific files that mirror the GL backend, plus ImGui Metal integration.

**Files:**
- Create: `src/Layers/xrRenderMetal/metalDetailManager_VS.cpp`
- Create: `src/Layers/xrRenderMetal/metalr_screenshot.cpp`
- Create: `src/Layers/xrRenderMetal/Blender_Recorder_Metal.cpp`
- Create: `src/Layers/xrRenderMetal/metalMinMaxSMBlender.cpp`
- Create: `src/Layers/xrRenderMetal/metalMSAABlender.cpp`
- Create: `src/Layers/xrRenderMetal/metalRainBlender.cpp`
- Create: `src/Layers/xrRenderMetal/metalOcclusionQuery.h`
- Create: `src/Layers/xrRenderMetal/metalOcclusionQuery.cpp`
- Create: `src/Layers/xrRenderPC_Metal/dxImGuiRender.cpp`
- Create: `src/Layers/xrRenderMetal/metalRenderPassManager.h`
- Create: `src/Layers/xrRenderMetal/metalRenderPassManager.cpp`

**Reference:** Corresponding GL files in `src/Layers/xrRenderGL/`. Dear ImGui ships `imgui_impl_metal.mm` as a reference backend.

- [ ] **Step 1: Implement metalDetailManager_VS.cpp**

Detail manager vertex setup for Metal. Sets vertex buffers for terrain detail rendering.

- [ ] **Step 2: Implement metalr_screenshot.cpp**

Capture current frame by reading back from the Metal drawable texture:

```cpp
// Create a blit command encoder
// Copy drawable texture to a readable buffer
// Read back pixels
```

- [ ] **Step 3: Implement Blender_Recorder_Metal.cpp**

State recording for blender system. Records Metal render state changes.

- [ ] **Step 4: Implement Metal blenders (MinMaxSM, MSAA, Rain)**

Backend-specific blender implementations. These set up shaders and state for specific effects.

- [ ] **Step 5: Implement metalOcclusionQuery**

Metal occlusion queries via `MTLVisibilityResultBuffer`:

```cpp
class metalOcclusionQuery
{
    MTL::Buffer* m_visibilityBuffer;
    u32 m_queryCount;

public:
    void Create(u32 maxQueries);
    void Begin(u32 queryIndex, MTL::RenderCommandEncoder* encoder);
    void End(u32 queryIndex, MTL::RenderCommandEncoder* encoder);
    u64 GetResult(u32 queryIndex);
    void Destroy();
};
```

- [ ] **Step 6: Implement ImGui Metal backend**

Create `src/Layers/xrRenderPC_Metal/dxImGuiRender.cpp` — Metal-specific ImGui rendering. Dear ImGui ships `imgui_impl_metal.mm` as a reference. The engine's `dxImGuiRender` class (in `src/Layers/xrRender/dxImGuiRender.h`) has a backend-specific implementation per renderer.

Read the GL implementation in `src/Layers/xrRender/dxImGuiRender.cpp` first, then create the Metal version that uses `MTL::RenderCommandEncoder` for draw submission and `MTL::Texture` for the font atlas.

- [ ] **Step 7: Implement metalRenderPassManager**

Manages `MTL::RenderPassDescriptor` lifecycle — configuring color/depth attachments, load/store actions, beginning and ending render command encoders. This is used by CBackend and CRenderTarget.

- [ ] **Step 8: Commit**

```bash
git add src/Layers/xrRenderMetal/ src/Layers/xrRenderPC_Metal/dxImGuiRender.cpp
git commit -m "feat(sp2): implement remaining Metal HAL (detail, screenshot, blenders, occlusion, ImGui)"
```

---

## Phase 9: First Compile and Integration Test

### Task 20: Get the Metal target to compile

Wire everything together and fix compilation errors.

**Files:**
- Modify: `src/Layers/xrRenderPC_Metal/CMakeLists.txt` — ensure all source files are listed
- Various fixes across Metal files

- [ ] **Step 1: Regenerate CMake**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
```

Expected: CMake configures successfully, shows xrRender_Metal target.

- [ ] **Step 2: Attempt build**

```bash
cmake --build build --target xrRender_Metal -j10 2>&1 | head -100
```

Expected: Many errors. This is the iteration loop.

- [ ] **Step 3: Fix compilation errors iteratively**

Work through errors one file at a time. Common error categories:
1. Missing includes — add to stdafx.h or individual files
2. Type mismatches (CommonTypes.h gaps) — add missing types to CommonTypes.h
3. Missing method implementations — add stubs to metalR_Backend_Runtime.h
4. Missing USE_METAL branches in shared code — revisit Tasks 6-7c
5. Metal API usage errors — fix metal-cpp calls

Each fix should be small and targeted. Commit after each batch of related fixes.

**Decision point:** If >100 unique errors remain after 2 hours, revisit Tasks 7a-7c for missing `#ifdef` branches — this is the most likely root cause of bulk compilation failures.

- [ ] **Step 4: Verify GL target still works**

```bash
cmake --build build --target xrRender_GL -j10 2>&1 | tail -10
```

Expected: GL target compiles cleanly (no regressions).

- [ ] **Step 5: Verify Metal target compiles**

```bash
cmake --build build --target xrRender_Metal -j10 2>&1 | tail -10
```

Expected: Metal target compiles (may have linker warnings but no errors).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(sp2): Metal renderer compiles (first successful build)"
```

---

### Task 21: Full build and link test

Build the entire engine with the Metal renderer module included.

**Files:**
- May need: various link-time fixes

- [ ] **Step 1: Full build**

```bash
cmake --build build -j10 2>&1 | tail -30
```

- [ ] **Step 2: Fix linker errors**

Common issues:
- Missing symbol exports
- Duplicate symbols (RENDER_NAMESPACE collision)
- Missing framework links
- Undefined Metal API symbols

- [ ] **Step 3: Verify engine binary exists**

```bash
ls -la bin/arm64/Release/
```

Expected: Engine binary present with both xrRender_GL and xrRender_Metal libraries.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(sp2): full engine build with Metal renderer module"
```

---

## Phase 10: Runtime Testing

### Task 22: Launch test — Metal renderer initializes

**Files:**
- May need: runtime fixes in metalHW, module registration

- [ ] **Step 1: Launch engine with Metal renderer**

```bash
cd bin/arm64/Release
./xr_3da -renderer renderer_r3
```

Or let the engine auto-select Metal (it's first in the array on macOS).

- [ ] **Step 2: Check console output**

Look for:
- `* Metal device: Apple M4` (or similar)
- No crashes during initialization
- Renderer module loads successfully

- [ ] **Step 3: Fix runtime crashes**

Common issues:
- Null MTL::Device (Metal not available)
- SDL_Metal_CreateView failure
- Missing shader files
- Render target creation failures

- [ ] **Step 4: Get to a clear screen**

First goal: engine opens a window and shows a solid color (Metal clear color). This means CHW::CreateDevice, BeginScene, Present all work.

- [ ] **Step 5: Commit any runtime fixes**

```bash
git add -A
git commit -m "fix(sp2): runtime fixes for Metal renderer initialization"
```

---

### Task 23: Render first geometry

Get triangles appearing on screen.

- [ ] **Step 1: Verify shader loading**

Check that at least one vertex + fragment shader pair compiles to MSL and creates a valid PSO.

- [ ] **Step 2: Verify vertex buffer creation**

Check that geometry data loads into Metal buffers.

- [ ] **Step 3: Debug draw calls**

Use Metal GPU debugger (Xcode GPU Frame Capture) or add logging to track:
- PSO creation success
- Draw call parameters
- Encoder state

- [ ] **Step 4: Iterate until geometry appears**

This is the critical milestone. Once triangles render, the rest is filling in features.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(sp2): Metal renderer draws first geometry"
```

---

### Task 24: Achieve full scene rendering

Iterate on render features until a full S.T.A.L.K.E.R. scene renders.

- [ ] **Step 1: G-buffer rendering**

Verify position/normal/color textures are populated correctly.

- [ ] **Step 2: Commit G-buffer milestone**

```bash
git add -A
git commit -m "feat(sp2): Metal G-buffer rendering works"
```

- [ ] **Step 3: Shadow mapping**

Verify depth-only shadow pass produces valid shadow maps.

- [ ] **Step 4: Deferred lighting**

Verify light accumulation reads G-buffer and produces lit scene.

- [ ] **Step 5: Commit lighting milestone**

```bash
git add -A
git commit -m "feat(sp2): Metal deferred lighting and shadows work"
```

- [ ] **Step 6: Post-processing**

Verify bloom, SSAO, and tone mapping work.

- [ ] **Step 7: HUD and UI**

Verify game UI renders correctly (text, menus, HUD elements). Verify ImGui debug overlay renders.

- [ ] **Step 8: Commit post-processing milestone**

```bash
git add -A
git commit -m "feat(sp2): Metal post-processing and UI rendering work"
```

- [ ] **Step 9: Visual comparison with GL**

Screenshot the same scene with GL and Metal renderers. Compare for obvious differences. Fix any visual regressions. Use the spec's feature parity checklist:
- [ ] G-buffer rendering (position, normal, color)
- [ ] Shadow mapping (cascaded sun shadows)
- [ ] Deferred lighting (point, spot, sun)
- [ ] Bloom post-processing
- [ ] SSAO
- [ ] Particle effects
- [ ] Fog/volumetrics
- [ ] HUD/UI rendering

- [ ] **Step 10: Performance check**

Verify stable 60fps at 1080p on Apple Silicon. Metal should be faster than GL.

- [ ] **Step 11: Stability test**

Play for 30 minutes without crashes. Test:
- Level transitions
- Save/load
- Various weather conditions
- Combat (particles, effects)

- [ ] **Step 12: Final commit**

```bash
git add -A
git commit -m "feat(sp2): Metal renderer achieves visual parity with GL"
```

---

## Summary

| Phase | Tasks | Goal |
|-------|-------|------|
| 1: Build Infrastructure | 1-3 | Metal target exists in CMake with stubs |
| 2: Engine Integration | 4-5 | Renderer array uses std::span, Metal registered |
| 3: Shared Code Branches | 6, 7a-7c | All USE_METAL #ifdef branches in place |
| 4: Metal HAL | 8-12 | Device, state, backend, constants, vertex input |
| 5: Textures & Resources | 13-14 | Textures load and bind, render targets created |
| 6: Shader Pipeline | 15-16 | GLSL -> MSL compilation works (build-time primary) |
| 7: Render Targets | 17-18 | G-buffer, shadows, bloom, SSAO, combine phases |
| 8: Remaining HAL | 19 | Detail, screenshot, blenders, occlusion, ImGui |
| 9: First Compile | 20-21 | Everything compiles and links |
| 10: Runtime Testing | 22-24 | Engine runs with Metal, achieves visual parity |

**Critical path:** Tasks 1-12 are sequential (each builds on the previous). Tasks 13-19 have internal dependencies (15->16, 13->14->17->18) but can be interleaved after Task 12. Tasks 20-24 are sequential (integration and testing).

**Biggest risks:**
1. Shared code ifdef audit (Tasks 7a-7c) — easy to miss sites, causing compile errors in Task 20
2. Shader compilation (Task 16) — X-Ray's non-standard GLSL needs preprocessing before glslang can consume it
3. PSO cache design (Task 10) — wrong hash keys cause visual glitches or stalls
4. D3D9 compatibility types — shared code uses D3D9 enums extensively; Metal stdafx.h must include same compat headers as GL
