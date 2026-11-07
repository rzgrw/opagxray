# OpenXVibeRay — Engine Roadmap v2 (Long-Term Vision)

**Date:** 2026-07-05 (rev. 2: dual-native renderer decision, same day)
**Status (2026-11-07):** Long-term dual-native/agentic-Zone vision retained; the renderer-first execution order below is superseded by **AI on GL**. [PLAN_STATUS.md](../../PLAN_STATUS.md) owns current implementation status and acceptance; [HANDOVER.md](../../HANDOVER.md) gives runtime context. Historical detailed milestones remain for reference, not automatic execution.
**Child specs (authoritative for their domain):**
- Renderer (PC + dev vehicle): `2026-07-05-vulkan-renderer-roadmap.md` (scope narrowed: Linux/Windows backend, developed on Mac via MoltenVK)
- Renderer (Mac): `docs/superpowers/plans/2026-03-29-sp2-metal-renderer.md` historical Tasks 22–24 + Metal research; M-R0/menu shipped, M-R1 parked
- AI: `2026-07-05-agentic-zone-design.md` (xrMind agentic Zone; body wins over its appendix)
- Surviving substrate: `2026-07-05-zone-simulation-design.md` (ids/validation/materialization invariants only)
- Parked: seamless streaming (xrStream)
- Done: agent bridge (`2026-07-04-agent-bridge-design.md`)

## North Star

A platform-agnostic rebuild of the X-Ray engine with **DUAL-NATIVE renderers** (rz 2026-07-05, supersedes Vulkan-everywhere): **native Metal on macOS** (revived — TBDR, MetalFX, Metal RT) and **native Vulkan on Linux/Windows** (VK RT), sharing the engine's existing multi-backend architecture, one GLSL shader source (glslang→SPIR-V→{Vulkan directly | SPIRV-Cross→MSL}), and D3D-style conventions in shared code (`USE_METAL || USE_VULKAN` branches). **MoltenVK is demoted to a dev vehicle** — it lets the Vulkan backend be developed/debugged on the Mac before PC hardware/CI exists; it ships nowhere. Native RT on every platform; no translation-layer compromise on the primary one. Plus a **Zone simulated by LLM agents** (cloud-only, thin offline) and an engine that **drives and verifies itself** (agent bridge, shipped). The original engine is inspiration and foundation, not scripture — and its multi-renderer design is exactly why dual-native is cheap here.

## Current status (reconciled 2026-11-07)

| Piece | State |
|---|---|
| OpenGL / foundation | Working runtime; `8984220b6` ships empty-group guards, UI comparator, private CoC staging, and frame-wait/menu sequencing. Historical gate evidence is recorded, not rerun; original ignored artifacts/worktree are absent. |
| Agent bridge | Shipped; verification uses explicit private runtime paths from [macOS setup](../../macos-dev-setup.md). |
| Metal | M-R0/menu shipped; M-R1 parked at unmerged `caa2e4953`. Native-renderer work paused. |
| Vulkan | Not started; long-term Linux/Windows renderer, MoltenVK only a development vehicle. |
| AI on GL | Foundation, HTTP, thin actor harness, packs, async actor wakes and Gate 1 Task 1 canonical state (`04b4ef5b5`) shipped. Further AI expansion follows the current runtime stability/performance comparison; Gate 1 incomplete. |

The unmerged Gate 1 source at `1d951aa07` requires selective recovery and contract
reconciliation, not a wholesale merge. The `f1222d148` Gemini/model/version-ladder
reanimation is **proposed, not approved**. `claude-sonnet-5` remains a historical
plan value requiring official availability verification before live use.
See the inventory for exact refs, recovery order, and remaining acceptance.

## Historical version ladder (not current execution order)

**V1** = dual-native renderers (Metal on Mac + Vulkan on PC) + agentic Zone AI + bridge · **V2** = polish/sharpen, retire GL + compat scaffolding · **V3** = AI maturation at scale · **V4** = reserved expansion · **V5** = Rust rewrite (long horizon).

## Historical V1 milestones (renderer-first ordering superseded)

**A0 — Agentic vertical slice on GL (parallel track, PROPOSED — rz to confirm).**
The AI epic's riskiest unknowns (real $/wall-hour, RPM/429 behavior, LLM ecology coherence, replay-gate integrity) are renderer-agnostic and testable **today** on the working GL game via the bridge. A0 = `xrWorldState` store + `ToolDispatcher` + 1 ZONE + 1 ECOLOGY agent + record/replay + a `zonesoak` script. *Exit:* measured $/hr and req/min published; byte-identical replay green. Runs parallel to V-R0/V-R1 (zero shared code surface). If rz prefers strict renderer-first serialization, delete this milestone explicitly — that is a resourcing decision, not a default.

**M-R0 — Metal first frame (Tasks 22–24 revival) — FIRST MILESTONE.**
Resume the parked SP2 plan at Task 22: boot with `renderer renderer_metal`; fix the known blender `VERIFY` at device create (first entry point); first clear → first geometry → CoC menu + level. Verified via bridge screenshots vs GL.
*Exit:* CoC menu + loaded level render on Metal; bridge screenshot ≈ GL; soak green on Metal. Then M-R1+: parity → TBDR/MetalFX/Metal-RT ladder per the original Metal research (the parked R1–R3 stages return for the Metal backend).

**L0 — Linux dev-loop bootstrap (Ubuntu laptop, can start anytime).**
rz has an Ubuntu laptop with an **NVIDIA RTX-class GPU** (full `VK_KHR` ray tracing via the proprietary driver — the V-R3 target). Sequencing (rz): **M-R0 (Metal first frame) first; L0 after** — bootstrap the laptop when Vulkan work starts. Bootstrap it as the second instrumented platform: build the existing GL game + agent bridge there (both are POSIX/cross-platform already), copy CoC data, get the bridge soak green on Linux. This de-risks V-RX years early and gives the Vulkan backend a NATIVE-first debug target (full API + validation on a real driver; MoltenVK remains the on-Mac convenience loop).
*Exit:* GL CoC + bridge soak green on Ubuntu.

**V-R0 — First Vulkan triangle (native on the Ubuntu laptop; MoltenVK on Mac as convenience).**
Vendor deps (see Dependencies below); `xrRenderVulkan` + `xrRenderPC_Vulkan` (`RENDER_NAMESPACE=render_vulkan`, `EXCLUDE_FROM_ALL`, `XRAY_VULKAN=OFF` until this exits, then default ON); SDL2 Vulkan surface; instance/device/swapchain via vk-bootstrap (value-returning — fits `XRAY_EXCEPTIONS=0`); timeline-semaphore + sync2 from day one; clear + triangle via dynamic rendering; runtime glslang compile (port the metalShaderCompiler front half, **bump to EShTargetVulkan_1_3 / SPIR-V 1.6**, persist a `VkPipelineCache`).
*Exit:* triangle on the Ubuntu laptop (native driver, validation clean) AND on Mac via MoltenVK (portability-subset clean); bridge `shot` captures both; **orientation verified against GL output** (see Convention decision).

**V-R1 — CoC scene parity (Vulkan, dev vehicle on Mac → ships on PC).**
Port render phases onto dynamic rendering; SPIR-V reflection (own task — decoration-based, replaces MSL reflection); VMA with the topology-adaptive **zero-staging UMA path** from the first resource; SPIR-V optimizer/validation ON.
*Exit (measured, not just recorded):* the **benchmark suite** (below) shows Vulkan **at/below GL frame time** on the same scenes; zero staging allocations on UMA hardware; bridge soak green on Vulkan. (Mac ships Metal — Vulkan-on-Mac numbers are dev-vehicle telemetry, not a ship gate.)

**V-R2 — GPU-driven (Vulkan).**
Bindless (descriptor indexing), draw-indirect-count, culling compute; A/B argument buffers on Apple Silicon; HDR/ProMotion presentation work item.
*Exit:* benchmark suite shows Vulkan **strictly faster than GL** on all scenes (the V1 "faster" promise on PC; the Mac "faster" promise is M-R1's gate on Metal).

**V-RX — Port & verify (Linux, then Windows).**
Native Vulkan builds; portability-subset gaps resolved; bridge + CI on both. May slip behind Mac-RT work per Mac-primary ranking.
*Exit:* CoC + bridge parity on Linux & Windows. **THE single GL-retirement gate:** V-RX exit + benchmark green + one stabilization cycle. (Supersedes all earlier/conflicting GL-gate statements.)

**V-R3 — Ray tracing, dual-native.**
PC: full `VK_KHR` RT on NVIDIA/RADV/ANV. Mac: Metal RT (`MTLAccelerationStructure` + `intersector<>`, M3+) on the revived backend per the original research. One thin internal RT interface (BLAS/TLAS build/refit, trace pass) with two implementations; caps-gated cleanly on unsupported hardware.

**A1+ — Agentic Zone build-out** (post-renderer, per the agentic-zone spec; A0 learnings folded in).

## Normative decisions (binding; discovered by the review)

1. **Clip-space convention:** `render_vulkan` uses **negative viewport height** (core ≥1.1, MoltenVK-supported) so the effective convention equals D3D/Metal. All `USE_METAL` convention branches gain `|| defined(USE_VULKAN)` **unchanged** — this is what makes the salvage claim true. V-R0 verifies orientation vs GL.
2. **Benchmark suite:** N bridge-scripted scenes (same CoC level/settings/machine, `agentctl` frame-time capture) run on GL and Vulkan. It gates V-R1 (≤GL), V-R2 (<GL), and GL retirement. No perf claim enters the docs without it.
3. **Backend screenshot contract:** `Screenshot(mode, name)` honors `name` (GL fixed; Vulkan/Metal must comply) — the bridge depends on it.
4. **Dependency pinning:** pick ONE current LunarG SDK tag; pin glslang, SPIRV-Cross, Vulkan-Headers, volk, vk-bootstrap and the bundled MoltenVK to it (re-pin the existing 1.3.290.0 drops). New deps = pinned submodules; MoltenVK = prebuilt dylib + ICD JSON (document `VK_DRIVER_FILES` for dev runs). VMA gets one `VMA_IMPLEMENTATION` TU with the unity/PCH-skip idiom (the metal_cpp_impl lesson).
5. **Both-backend keep-alive:** `XRAY_METAL=ON` (and later `XRAY_VULKAN=ON`) compile is a pre-merge checklist item while shared `USE_*` files are churned — neither active backend may rot silently. MoltenVK is a dev vehicle only; nothing ships through it.
6. **No on-device LLM is load-bearing anywhere in V1–V3** (thin-offline decision; agentic-zone body wins over its appendix).
7. **Orphaned-goal owners:** temporal upscaling → V-R2+ decision (FSR-class cross-platform vs parked-Metal MetalFX vs cut); LLM dialogue → explicit in/out-of-V1 call in the AI epic before A1; HDR/ProMotion → V-R2; SPIR-V optimizer/validation → V-R1 exit; memory-budget eviction/streaming → V2 unless V-R2 profiling forces it; cost/RPM measurement → A0 exit; MoltenVK/KosmicKrisp re-check → every V-R phase entry.

## Cleanup backlog (P2, non-blocking)

Stale "SP2 Task N" comments across shared `r2_*`/`r3_*` files (reword to "parked Metal backend"); dead `USE_METAL` arms in shared `dxImGuiRender.cpp` (Metal uses a file replacement); `MTL::` casts leaking into shared `r2_rendertarget.cpp` (pattern hygiene before `vk*` equivalents copy it); appendix/body drift in older specs (bannered).

## Historical top risks (2026-07-05; current status above takes precedence)

1. **MoltenVK is the primary path but a translation layer** — no RT, no tile-memory; mitigations: parked Metal insurance + keep-alive + standing status reviews. 2. **Perf promise unmeasured until the benchmark suite exists** — build it at V-R0/V-R1, not later. 3. **AI economics unknown** — A0 exists to measure before the big build. 4. **Single-developer fleet: three OSes** — Mac-first sequencing + V-RX as its own phase. 5. **Doc drift after rapid pivots** — this document is the single plan of record; children own detail; amendments update this file first.
