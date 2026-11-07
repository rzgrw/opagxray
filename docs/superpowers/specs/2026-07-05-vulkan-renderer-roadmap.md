# Vulkan-Everywhere Renderer Roadmap (Epic V-R) — Design Spec

> **PARKED / NOT STARTED (2026-11-07).** Long-term scope is native Vulkan on Linux/Windows and native Metal on Mac; MoltenVK is a development vehicle only. AI on GL is current. Vulkan-everywhere and primary-Mac claims below are historical and superseded; see [PLAN_STATUS](../../PLAN_STATUS.md).

**Date:** 2026-07-05
**Status:** Design approved in direction (rz) — **supersedes the R workstream** ("native Metal TBDR → GPU-driven → RT") in `2026-07-04-engine-v2-roadmap.md`. Reverses that roadmap's Guiding Rule #1 ("native over portable").
**Decisions (rz 2026-07-05):** platform-agnostic renderer (macOS/Linux/Windows); **Vulkan everywhere** (native on Linux/Windows, MoltenVK on macOS); **Mac primary-tuned, others must-run**.
**Provenance:** 11-agent research + adversarial-verify + synthesis pass, grounded in this repo and current (2025-2026) MoltenVK/Vulkan sources. Findings in the session scratchpad.

## The verdict in one paragraph — read this first

Vulkan-via-MoltenVK is **sound as the primary Mac renderer for raster / compute / GPU-driven**, and a single Vulkan codebase across the three desktop OSes (with real validation-layer tooling everywhere) is the right cross-platform bet — it will beat today's glitchy macOS OpenGL 4.1 path. **But MoltenVK cannot do ray tracing on Apple Silicon** (`VK_KHR_acceleration_structure` / `ray_tracing_pipeline` / `ray_query` unimplemented, open with no committed date; the blocker is architectural, not backlog), and it **cannot express TBDR tile-memory / memoryless G-buffer** attachments. So: **Vulkan is the primary cross-platform renderer; the parked native-Metal target (`xrRenderMetal`/`xrRenderPC_Metal`) is retained as the insurance path for Mac ray tracing + TBDR** — the only route to hardware RT on Apple Silicon. RT is therefore **native on PC (Linux/Windows Vulkan) first**, and on Mac only later via the Metal path — consistent with the earlier "RT-ready, defer depth" stance. This does **not** affect near-term work: V-R0 (first triangle) through V-R2 (GPU-driven) are identical regardless. Big SP2 salvage: the vendored **glslang GLSL→SPIR-V pipeline is Vulkan's native shader path** (drop only the SPIRV-Cross→MSL step), and the `USE_METAL` shared-code branches become the `USE_VULKAN` template.

---

# REVISED RENDERER ROADMAP — Vulkan-Everywhere Pivot (Epic V-R)

**Supersedes:** Epic R (native-Metal TBDR → GPU-driven → RT) in `docs/superpowers/specs/2026-07-04-engine-v2-roadmap.md`.
**Status:** Draft brief, replaces the R workstream. Reconcile with the 2026-07-05 agentic-zone revision already in that doc (Epics T and A are backend-agnostic and stay). North Star (line 9) and Guiding Rule 1 (line 14, "Native over portable / Metal idioms directly") are the two lines this pivot directly reverses — rewrite both.
**Date basis:** 2026-07-05. All MoltenVK/KosmicKrisp facts below carry a "re-check before each milestone" flag because this substrate is moving fast.

---

## 1. Verdict — Is Vulkan-via-MoltenVK sound as the PRIMARY Mac renderer?

**Yes for raster/compute. No for ray tracing. Native Metal stays parked as the RT + TBDR fast-path, not deleted.**

Plainly:

- **Rasterization / compute / GPU-driven: sound, ship it.** MoltenVK is a production-grade, nearly-conformant Vulkan 1.4 layer over Metal (Vulkan 1.4 core since 1.4.0, 2025-08-20; 1.4.1 on 2025-12-01 added maintenance extensions). It covers all Apple Silicon. For a game currently on **macOS OpenGL 4.1 with known shadow/lighting glitches**, a Vulkan+MoltenVK forward/deferred renderer will almost certainly be **faster and more correct than today's baseline**. The decisive win is a single modern renderer codebase across macOS/Linux/Windows with **real Vulkan validation layers on all three** — tooling parity native Metal (Xcode-only) never gave us.

- **Ray tracing: not available on Mac through any Vulkan-on-Metal layer, no committed date.** MoltenVK ships none of `VK_KHR_acceleration_structure` / `ray_tracing_pipeline` / `ray_query`. Issues #427, #1956, #2384 are all OPEN. The prerequisite plumbing (`VK_KHR_deferred_host_operations`, `VK_KHR_buffer_device_address`) **is landed** (PR #1954) — so the honest framing is *"prereqs done, the acceleration-structure implementation itself is greenfield and unscheduled upstream,"* not *"even the prereqs aren't ready."* The blocker is architectural (device-address requirements are hard for layered impls), not mere backlog. KosmicKrisp lists RT only as *"potentially"* (aspirational, XDC 2025), not committed, and requires Metal 4 / macOS 26+.

- **Where native Metal stays on the table (two hard technical reasons, not sentiment):**
  1. **RT.** The only real path to hardware ray tracing on Apple Silicon is `MTLAccelerationStructure` via the parked `xrRenderMetal`/`xrRenderPC_Metal` target — unless upstream MoltenVK #1956 lands or we carry a fork (pablode/MoltenVK-rayQuery is ray_query-only, WIP, unmerged — verify before betting on it).
  2. **TBDR / tile-memory.** Vulkan-on-Metal **cannot express** memoryless (`MTLStorageModeMemoryless`) / imageblock on-chip G-buffer attachments or per-resource storage modes — MoltenVK picks storage modes for you. If a deferred design genuinely depends on on-chip tile memory, that optimization is only reachable via native Metal.

**Decision:** MoltenVK/Vulkan is the **PRIMARY cross-platform raster renderer**. Native Metal is the **documented insurance policy and eventual Mac RT+TBDR fast-path**. We do **not** bet the RT epic on any Vulkan-on-Metal layer.

**One honesty guardrail:** No specific performance-overhead percentage enters this roadmap as fact. There is no clean 2025–2026 MoltenVK-vs-native-Metal head-to-head for our workload; LunarG's Jan-2026 white paper publishes no overhead figure. Known signals are directional only (~9% CPU to draw a triangle, issue #1409; M1 Max CPU-bound reports, #1749; the scary ~50% argument-buffer regression is a **DXVK/CrossOver translated-DX11 artifact**, issue #2530 — *not* a native-Vulkan penalty). Perf targets get set from **on-device profiling**, not assumptions.

---

## 2. Renderer Architecture — one Vulkan codebase beside GL and parked Metal

**New backend pair, mirroring the proven pattern:**

```
src/Layers/xrRenderVulkan/       # HW abstraction (VkInstance/Device/Swapchain, VMA, sync) — analog of xrRenderGL / xrRenderMetal
src/Layers/xrRenderPC_Vulkan/    # renderer module: USE_VULKAN, RENDER_NAMESPACE=render_vulkan — analog of xrRenderPC_GL / xrRenderPC_Metal
```

Shared render logic in `src/Layers/xrRender` + `xrRender_R2` stays single-source; the Vulkan backend joins via a parallel `USE_VULKAN` ifdef arm.

**Core design commitments (all confirmed portable across NVIDIA / AMD-RADV / Intel-ANV / MoltenVK):**

- **Dynamic rendering as the baseline** — `VK_KHR_dynamic_rendering` (Vulkan 1.3 core, MoltenVK-supported; `dynamic_rendering_local_read` present in the 1.4.x line — verify exact landing vs MoltenVK `Whats_New.md`). **No legacy `VkRenderPass`/`VkFramebuffer` graph.** This maps directly onto the SP2 render-pass manager's deferred-begin design (`vkCmdBeginRendering` + `VkRenderingInfo` + `LOAD_OP_CLEAR`).
- **Bindless via descriptor indexing** — `VK_EXT_descriptor_indexing`. On Apple Silicon this maps to Metal **argument buffers (default-on since MoltenVK 1.2.10)** on Tier-2 hardware, giving **~1,000,000 bindless resources per stage** — effectively desktop-class headroom. The stale "Tier-1 96/128 texture cap" only ever applied to legacy Intel Macs we are dropping. **Avoid geometry shaders** (weak/emulated on Metal); avoid hard reliance on tessellation.
- **Frame graph on timeline semaphores + synchronization2** from day one — both are Vulkan 1.2/1.3 core, hence necessarily present under MoltenVK's 1.4 conformance. Uniform across all four ICDs.
- **Value-based capability probing** (see §5) — RT, DGC, mesh shaders become queried caps bits at device-create, branched at runtime. This is exactly what `XRAY_EXCEPTIONS=0` requires and it makes the Mac RT stub *automatic*: MoltenVK simply won't enumerate `VK_KHR_acceleration_structure`, so the probe cleanly returns "no RT on Mac" with zero special-casing.

**How the three backends sit together (`src/Layers/CMakeLists.txt`):**

| Backend | Subdir gate | Role |
|---|---|---|
| `xrRenderPC_R4` (DX11) | `if(WIN32)` | Windows legacy, no new investment |
| `xrRenderPC_GL` | unconditional | **Fallback/reference during Vulkan bring-up; deprecated after V-R1 exit gate** |
| `xrRenderPC_Vulkan` | unconditional (or `WITH_VULKAN` option) | **PRIMARY, all platforms** |
| `xrRenderPC_Metal` | `if(APPLE)`, `EXCLUDE_FROM_ALL` | **PARKED** — reference + future RT/TBDR fast-path. Does **not** build end-to-end today (SP2 Task 20 open); it is a *structural* template, not a proven one. |

Renderer selected via the existing console `renderer` verb (add `renderer_vk` alongside `renderer_r4`/`renderer_metal`; `Engine.cpp:43` boot default). The AgentBridge harness (`AgentBridge.cpp:247` verb dispatch → `VerbShot`/`Screenshot` at ~487/491) is backend-agnostic and needs no change — it becomes the automated cross-platform smoke gate.

---

## 3. SP2 Reuse — keep / adapt / drop

The SP2 Metal effort was **not wasted**. Concrete audit (all line refs verified against the tree):

| Component | Verdict | Notes | Code reuse |
|---|---|---|---|
| **glslang GLSL→SPIR-V** (`xrRenderMetal/metalShaderCompiler.cpp:137-181`) | **KEEP ~100%** | Already emits Vulkan-flavored SPIR-V 1.3 (`EShClientVulkan`, `EShTargetSpv_1_3`, Vulkan rules relaxed). Vulkan consumes it natively. Caveat: currently `disableOptimizer=true`, `validate=false` (lines 169,172) — fine for layer intake, but **revisit before shipping render_vk** for correctness/perf. | ~100% |
| **SPIRV-Cross → MSL** (`metalShaderCompiler.cpp:11,276-339`) | **DROP for VK** | MoltenVK/KosmicKrisp do SPIR-V→MSL internally. render_vk never produces MSL. Keep `spirv-cross-msl` linked **only** in the parked Metal target. | 0% |
| **Shader reflection** (`get_automatic_msl_resource_binding` etc., 209-380) | **ADAPT** | Binding slots are MSL `[[texture(n)]]` — meaningless for Vulkan. Re-derive from SPIR-V decorations (glslang can emit reflection directly, or use spirv-reflect). The `RC_float/int/bool` type-mapping and globals-block logic are reusable structure re-pointed at SPIR-V. **Hidden cost — budget it.** | ~40% |
| **uint64_t opaque-handle contract** (`SH_Atomic.h`, `SH_RT.h`, 50 files) | **KEEP ~90%** | Vulkan non-dispatchable handles *are* `uint64_t` (`VK_DEFINE_NON_DISPATCHABLE_HANDLE`) — better fit than for Metal, no reinterpret gymnastics. `USE_METAL` → parallel `USE_VULKAN` arm (note many sites are `#elif defined(USE_OGL) || defined(USE_METAL)`, e.g. `Shader.h:98` — add VK as its own arm, don't overload). | ~90% |
| **Shared-code MTL:: leaks** (`r2_rendertarget.cpp:752,765,770` — `reinterpret_cast<MTL::Texture*>->release()`) | **ADAPT + fix smell** | These are a pre-existing abstraction leak in shared code. For VK it's `vkDestroyImage`+free (VMA). **Route through a backend free-function** or the VK arm accretes parallel scattered ifdefs and the 50-file surface grows. | n/a |
| **Render-pass manager** (`metalRenderPassManager.cpp` deferred-begin / `EnsureEncoder` / pending clears) | **ADAPT** | Natural 1:1 to `VK_KHR_dynamic_rendering`. Design transfers; recording code rewrites. | ~50% |
| **PSO cache** (`metalRenderPassManager.cpp:578-645`) | **ADAPT** | Concept transfers. VK version needs **added `VkPipelineLayout` + descriptor-set layouts** in the key, and can back a disk `VkPipelineCache`. | ~40% |
| **CommonTypes.h** (`14,21-59,112-128`) | **KEEP ~70%** | `D3D_COMPARISON_FUNC` 0..7 maps 1:1 to `VkCompareOp`; `XR_METAL_VIEWPORT` matches `VkViewport`. Drop `#include <Metal/Metal.hpp>` and the `MTL::Buffer*` typedefs; swap enum values for Vk equivalents. | ~70% |
| **RENDER_NAMESPACE pattern** (`xrRenderPC_GL/CMakeLists.txt:426`, `xrRenderPC_Metal:446`) | **KEEP ~95%** | `render_vk` drop-in is straightforward. | ~95% |
| **Device / HW layer** (`metalHW.cpp:75,83,91,212`) | **WRITE-FRESH ~15%** | `VkInstance`/`PhysicalDevice`/`Device`/`SDL_Vulkan_CreateSurface`/`VkSwapchainKHR`. SDL2 windowing flow reuses. **Biggest net-new item: per-frame semaphores/fences/swapchain lifecycle — Metal auto-managed all of it, Vulkan has zero analog.** | ~15% |

**Rough salvage: ~40–50% of code/design.** **Explicit caveat that must ship with this number:** this is *code-reuse*, not *effort*. The hardest, highest-risk Vulkan work — explicit memory (VMA), per-frame synchronization, descriptor-set/pipeline-layout management, swapchain lifecycle — has **zero SP2 analog** and dominates calendar time. **Effort salvage is lower than 40–50%.**

---

## 4. Cross-Platform Feature / RT Matrix — honest per platform

"RT-ready, defer depth" must mean *different things per platform*. This is the load-bearing honesty table.

| Feature | Linux native (RADV/ANV/NVIDIA) | Windows native (NVIDIA/AMD/Intel) | **Mac via MoltenVK (PRIMARY)** | Mac via KosmicKrisp (track) | Mac native Metal (parked) |
|---|---|---|---|---|---|
| Forward/deferred raster | ✅ | ✅ | ✅ | ✅ (AS-only, macOS 26+) | ✅ (unbuilt) |
| Dynamic rendering | ✅ | ✅ | ✅ | ✅ | ✅ |
| Bindless / descriptor indexing | ✅ | ✅ | ✅ (~1M/stage, Tier-2 arg buffers) | ✅ | ✅ |
| Timeline semaphores + sync2 | ✅ | ✅ | ✅ | ✅ | n/a |
| Draw-indirect-count (GPU-driven core) | ✅ | ✅ | ✅ | ✅ | ✅ |
| Device-generated commands (DGC) | ✅ (opt) | ✅ (opt) | ❌ → degrade to CPU draw-indirect | ❌ | (ICB, not surfaced) |
| Mesh shaders | ✅ | ✅ | ❌ | ❌ ("potential") | ✅ (native) |
| **Hardware ray tracing** | ✅ **mature** (NVIDIA GA; RADV 2025 RT gains, AMDVLK discontinued Sept 2025 → RADV; ANV DG2+ since 2022) | ✅ **mature** | ❌ **none, no committed date** (#1956 greenfield) | ❌ "potentially", not shipping | ✅ **only real Mac RT path** (`MTLAccelerationStructure`) |
| TBDR / memoryless tile-mem G-buffers | n/a (IMR) | n/a (IMR) | ❌ not expressible via Vulkan | ❌ | ✅ (`imageblock`/`memoryless`) |
| HDR/EDR presentation | ✅ | ✅ | ⚠️ `swapchain_maintenance1` present; `hdr_metadata`/`swapchain_colorspace` **UNVERIFIED** — check before committing EDR | ⚠️ | ✅ (CAMetalLayer EDR) |
| Present-timing / ProMotion pacing | ✅ | ✅ | ⚠️ `present_wait`/`present_id2` on MoltenVK **UNVERIFIED**; SDL2→CAMetalLayer = two layers to ProMotion — plan a direct `VK_EXT_metal_surface` path | ⚠️ | ✅ |

**Reading of the matrix:** RT is **built and validated on native PC first** (all three vendors mature), then shipped Mac-side as a **caps-gated stub that lights up only if** upstream #1956 lands, we carry a fork, or we route Mac RT to the parked native-Metal backend. **Do not schedule "V-R3 RT on Mac via Vulkan" as a firm dependency** — it currently has no substrate.

---

## 5. Build / Windowing / Deps

**Windowing (net-new — grep confirms ZERO `SDL_Vulkan_*` / `SDL_WINDOW_VULKAN` in `src/` today):**
Add the surface bring-up in `src/xrEngine/Device_Initialize.cpp` (today it only extracts native handles under Windows/Cocoa at lines 96-98 — no Vulkan path). SDL 2.32.10 has full Vulkan support. Three-call sequence (SDL2 signatures, not SDL3):
1. `SDL_WINDOW_VULKAN` window flag
2. `SDL_Vulkan_GetInstanceExtensions(window, &count, names)` **before** `VkInstance` creation (auto-selects Wayland vs X11 on Linux)
3. `SDL_Vulkan_CreateSurface`; `SDL_Vulkan_GetDrawableSize` for Retina swapchain extent

**Dependencies:**

| Dep | Status | Notes |
|---|---|---|
| glslang | ✅ vendored (`Externals/glslang`, `BUILD_EXTERNAL OFF`, `ENABLE_OPT OFF`) | Keep — GLSL→SPIR-V front half |
| SPIRV-Cross | ✅ vendored | Keep linked **only** in parked Metal target |
| Vulkan-Headers | **add** | vendor + `find_package(Vulkan)` first |
| **volk** | **add** | meta-loader, removes per-call dispatch overhead; define `VK_NO_PROTOTYPES` for the VK target |
| **VMA** | **add** | header-only allocator, single `VMA_IMPLEMENTATION` TU |
| **vk-bootstrap** | **add** | value-returning builders (`vkb::Result<T>`) — no exceptions, perfect for our build |
| **MoltenVK** | **vendor + bundle** | not from Externals build; ship the ICD |

**CMake target:** mirror `xrRenderPC_Metal/CMakeLists.txt` — `USE_VULKAN`, `RENDER_NAMESPACE=render_vulkan`, `PURE_DYNAMIC_CAST`, same `../xrRender` + `../xrRender_R2` source list, D3D-free (no `xrRenderPC_R4`, no fxc/d3dcompiler). Reuse the **UNITY/PCH skip idiom** verified at `xrRenderPC_Metal/CMakeLists.txt:459-466` for the `VMA_IMPLEMENTATION` and `volk.c`/`VOLK_IMPLEMENTATION` single-definition TUs (`SKIP_PRECOMPILE_HEADERS ON`, `SKIP_UNITY_BUILD_INCLUSION ON`).

**Per-platform sourcing** — `find_package(Vulkan)` first, fall back to vendored:
- **Arch:** `vulkan-headers vulkan-icd-loader vulkan-validation-layers glslang`
- **Debian/Ubuntu:** `libvulkan-dev vulkan-validationlayers glslang-tools`
- **Windows:** LunarG SDK sets `VULKAN_SDK`; loader ships with GPU drivers.
- **macOS:** **no system loader.** Bundle loader + MoltenVK + ICD JSON inside `.app` (`Contents/Frameworks` + a manifest whose `library_path` is relative to the manifest — LunarG-recommended, matches SP3 app-bundle/DMG). LunarG SDK also ships KosmicKrisp; can bundle both and select via `VK_DRIVER_FILES` (modern; `VK_ICD_FILENAMES` deprecated-but-honored, `VK_DRIVER_FILES` wins if both set).

**`XRAY_EXCEPTIONS=0` (value-based errors)** — forced on Darwin at `src/CMakeLists.txt:6`. Vulkan is a natural fit: C API returns `VkResult` and never throws; volk is plain C; vk-bootstrap returns `vkb::Result<T>`. If `vulkan.hpp` is used anywhere, set `VULKAN_HPP_NO_EXCEPTIONS` + `VULKAN_HPP_ASSERT_ON_RESULT`. **Every capability is a device-create-time caps bit → runtime branch, never an exception path.**

---

## 5b. Unified memory — cross-platform zero-staging strategy (first-class, rz 2026-07-05)

Unified memory is a design pillar, not a Metal-only trick. Vulkan expresses it through memory-type flags, and it pays off on Apple Silicon (primary), integrated GPUs, AMD APUs, and discrete cards with Resizable BAR — so one strategy serves all three platforms while being *optimal* on the primary target.

**The topology-adaptive upload path (the whole point).** At device init, classify the memory topology from `VkPhysicalDeviceMemoryProperties` (+ `VK_EXT_memory_budget`):
- **UMA / integrated / Apple-Silicon-via-MoltenVK:** a memory type with **`DEVICE_LOCAL | HOST_VISIBLE`** exists. Allocate resources there, **persistently map** them, and have the CPU write directly — the GPU reads the same physical memory. **No staging buffer, no transfer-queue copy, ever.** This is the unified-memory win, and on Apple Silicon it is the *only* path taken.
- **Discrete + Resizable BAR / Smart Access Memory:** all of VRAM is `HOST_VISIBLE | DEVICE_LOCAL` → same direct-upload path.
- **Discrete without ReBAR:** `DEVICE_LOCAL` is not host-visible → fall back to a `HOST_VISIBLE` staging buffer + `vkCmdCopyBuffer` on the transfer queue.

**One code path, not three.** Route all allocation through **VMA** (`VMA_MEMORY_USAGE_AUTO` + `HOST_ACCESS_SEQUENTIAL_WRITE`); after each allocation, check `vmaGetAllocationMemoryProperties` — if the block came back `HOST_VISIBLE`, `memcpy` into the persistent mapping and skip staging; otherwise take the staging+copy branch. The engine writes uploads once; VMA + this check pick zero-copy vs staging per GPU. On Apple Silicon the check always says "host-visible" → the staging branch is dead code there.

**Consequences designed in:**
- **Per-frame dynamic data** (uniforms, dynamic vertex/index, the constant ring) lives in host-visible device-local ring buffers on UMA — the "unified arena" from the old Metal plan, now in Vulkan, zero staging.
- **Persistent mapping** everywhere host-visible (no map/unmap churn; UMA is coherent or explicitly flushed via `VK_WHOLE_SIZE` ranges).
- **Memory-budget citizenship** via `VK_EXT_memory_budget`: on Apple Silicon the GPU shares system RAM, and we already reserved that RAM for the renderer (not a local LLM — see agentic-zone spec), so the renderer must track budget and evict/stream within it, never over-commit.
- **Honest ceiling (from the MoltenVK research):** Vulkan can't set per-resource storage modes as precisely as native Metal, and **can't reach memoryless/tile-memory** — those deepest TBDR wins stay in the parked Metal path. But the *load-bearing* UMA benefit (zero-staging direct upload + persistent mapping + budget awareness) is fully expressible in Vulkan and is exploited from V-R1 onward.

## 6. Phasing — V-R0 … V-R3 (replaces R1/R2/R3)

> **Execution order: Mac-FIRST (rz 2026-07-05).** Vulkan-everywhere stays the *goal*, but we bring it up on **macOS-via-MoltenVK first**, using the debug/run loop we already built and proved on Mac (agent bridge, CoC run setup, the whole stabilization pass). Each Vulkan step is validated on Mac before we open the other platforms. Rationale: Mac is our only currently-runnable, fully-instrumented target — fighting three OSes at once would slow the rewrite; and MoltenVK enforces the **Vulkan Portability Subset**, so code that runs on Mac is a conservative *subset* of full Vulkan — porting Mac→native-Linux/Windows is the easy direction (subset→superset). We keep validation layers + `VK_KHR_portability_subset` checks on from day one so we never write Mac-only Vulkan by accident. Linux and Windows are a dedicated **"port & verify" phase (V-RX)** after the Mac renderer reaches CoC-parity, not a per-step burden. Per-phase exit gates below are **Mac-first**; the all-three-OS gate moves to V-RX.

**V-R0 — Bring-up & harness (Mac).**
Add SDL2 Vulkan surface path (`Device_Initialize.cpp`); `xrRenderVulkan` + `xrRenderPC_Vulkan` targets; volk/VMA/vk-bootstrap wired; MoltenVK vendored+bundled; `renderer_vk` console verb; instance/device/swapchain with **timeline-semaphore + sync2 per-frame sync** from day one; clear-to-color + one triangle via dynamic rendering; glslang SPIR-V feeding `vkCreateShaderModule`.
*Exit (Mac):* triangle renders on **Mac/MoltenVK**; validation layers **and `VK_KHR_portability_subset`** clean; AgentBridge `shot` produces the screenshot — verified with the existing Mac run loop (CoC data, `agentctl.py`).

**V-R1 — Feature-parity forward/deferred (Mac, GL-equivalent scene).**
Port the R2 render phases onto dynamic rendering + the adapted render-pass manager & PSO cache; SPIR-V-based reflection; VMA-managed resources with the **topology-adaptive zero-staging upload path (§5b)** in place from the first resource; load a CoC level and walk it — driven through the agent-bridge soak.
*Exit (Mac):* CoC level renders without crash on **Mac/MoltenVK**; visual parity-or-better vs GL (target fixing the GL 4.1 shadow/lighting glitches); **on-device profiling** establishes the real perf baseline (no assumed numbers); **zero-staging uploads verified on Apple Silicon** (no staging buffers; `vmaGetAllocationMemoryProperties` reports host-visible device-local); bridge soak (movement/save-load/UI/quit) green on Vulkan.

**V-R2 — GPU-driven core (Mac).**
Bindless (descriptor indexing / argument buffers), draw-indirect-count, culling compute. **A/B measure argument buffers on Apple Silicon** — do not assume bindless is a free win. (Device-generated commands are a later PC-only accelerator, not built here.)
*Exit (Mac):* GPU-driven path measurably ≥ V-R1 on Mac; bridge soak green.

**V-RX — Port & verify: Linux, then Windows.**
*Only after the Mac Vulkan renderer reaches CoC-parity (≥V-R1, ideally V-R2).* Stand up native Vulkan builds on Linux (native, arguably easier — no translation layer) then Windows; resolve portability-subset gaps (things MoltenVK forced that native Vulkan does differently); bring the agent bridge up on those OSes for CI. Because Mac was developed against the Portability Subset, this is the *easy direction* (subset→full). Add per-distro packaging (incl. Arch) + Windows SDK.
*Exit:* CoC renders + bridge screenshot parity on **Linux + Windows native**. **← GL DEPRECATION GATE:** once Vulkan holds on all three, GL → reference-only; remove from default build after one stabilization cycle.

**V-R3 — Ray tracing (PC-first, Mac caps-gated).** *(after V-RX — RT is native-PC and needs the PC ports first.)*
Build the **full `VK_KHR` RT path (AS build + trace) on native PC** (NVIDIA / RADV / ANV all mature). AS/RT plumbing is greenfield — `SH_RT.h` today is a render-target (`CRT`), *not* acceleration structures. Ship Mac RT as a **caps-gated stub**.
*Exit:* RT effects on ≥2 PC vendors; caps probe cleanly disables RT on Mac with no crash. **Mac RT is explicitly OUT of firm scope** pending: (a) upstream MoltenVK #1956, (b) a carried fork (re-verify pablode/MoltenVK-rayQuery first), or (c) **the Metal-target revival decision below.**

**Verification:** Mac phases (V-R0…V-R2) gate on the existing Mac agent-bridge loop; V-RX adds Linux+Windows bridge/CI; from V-RX onward every gate requires green on all three.

**Metal-target removal decision — deferred to end of V-R3, gated on three questions:**
1. Did MoltenVK RT (#1956) land? 2. Is TBDR/memoryless a measured win worth a second Mac backend? 3. Is Mac RT a hard product requirement?
If RT lands upstream **and** TBDR shows no decisive win → **remove `xrRenderMetal`/`xrRenderPC_Metal`** (Vulkan-everywhere fully retires native Metal). Otherwise → **revive Metal as the Mac RT+TBDR fast-path.** Keep parked and documented until this decision; do not delete earlier.

---

## 7. Top Risks & Mitigations

1. **Mac RT gap (dominant schedule risk).** MoltenVK RT is greenfield/unscheduled; KosmicKrisp only "potential"; a fork is ray_query-only WIP. *Mitigation:* build/validate RT on PC first; Mac RT is caps-gated and out of firm scope; the parked native-Metal backend is the acknowledged only-real Mac RT path. **Do not schedule V-R3-Mac against any Vulkan-on-Metal layer.**

2. **Translation overhead on the PRIMARY platform, unmeasured.** No published figure for our workload; going through MoltenVK spends part of the Apple-Silicon unified-memory perf win. *Mitigation:* profile on-device at V-R1; keep native Metal as the escape hatch; never let a % enter the roadmap as fact; frame the DXVK ~50% arg-buffer regression correctly as a translated-DX11 artifact (#2530), not a native penalty.

3. **TBDR/tile-memory inexpressible via Vulkan.** A deferred design assuming on-chip imageblock/memoryless G-buffers cannot be built on MoltenVK. *Mitigation:* design V-R1 deferred **without** a tile-memory dependency; if measurement later shows TBDR is decisive, that's a native-Metal-revival trigger, not a Vulkan feature.

4. **Presentation/HDR sub-claims unverified on MoltenVK.** `present_wait`/`present_id2` and `hdr_metadata`/`swapchain_colorspace` availability not confirmed. *Mitigation:* verify against MoltenVK `Whats_New.md` before committing EDR or adaptive ProMotion pacing; plan a direct `VK_EXT_metal_surface` path to reduce SDL2→CAMetalLayer indirection.

5. **Shader toolchain correctness.** Current SPIR-V is emitted with optimizer off / validation off. *Mitigation:* enable validation and optimization before shipping render_vk; move reflection off MSL bindings onto SPIR-V decorations.

6. **CI across 3 OSes.** Three native driver stacks + MoltenVK bundling + AgentBridge screenshots. *Mitigation:* headless `vid_mode` + AgentBridge `shot` as the uniform smoke test; Linux (RADV/lavapipe) + Windows + macOS runners; gate merges on all three green.

7. **Fast-moving substrate / version drift.** MoltenVK and KosmicKrisp status changes monthly (KosmicKrisp hit 1.3 conformance in ~10 months; 1.4 "coming soon"; Metal-4/macOS-26 floor). *Mitigation:* every version/RT statement is dated; re-check before each milestone; treat KosmicKrisp as **track-don't-bet** and **not an interchangeable MoltenVK fallback** (it's Apple-Silicon-only, macOS 26+, drops Intel and older macOS that MoltenVK still serves).

---

**Bottom line:** Vulkan-everywhere is the right call for a modern raster/GPU-driven engine across three platforms with unified tooling, and it will beat today's Mac GL baseline. The single non-negotiable honesty is that **ray tracing and TBDR on Mac do not come with the Vulkan backend** — they remain the property of the parked native-Metal target, whose fate is a deliberate decision at the end of V-R3, not a foregone deletion.

**Relevant paths:** `/Users/rz/OpenXVibeRay/docs/superpowers/specs/2026-07-04-engine-v2-roadmap.md` (Epic R to replace; reconcile with 07-05 agentic-zone revision), `/Users/rz/OpenXVibeRay/src/Layers/{xrRender,xrRender_R2,xrRenderPC_GL,xrRenderMetal,xrRenderPC_Metal,CMakeLists.txt}`, `/Users/rz/OpenXVibeRay/src/xrEngine/{Device_Initialize.cpp,Engine.cpp,AgentBridge.cpp}`, `/Users/rz/OpenXVibeRay/src/CMakeLists.txt` (line 6, `XRAY_EXCEPTIONS=0`), `/Users/rz/OpenXVibeRay/Externals/{glslang,SPIRV-Cross}`.
