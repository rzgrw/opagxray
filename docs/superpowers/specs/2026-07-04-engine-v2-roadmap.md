# OpenXVibeRay — Version Roadmap (V1 → V5)

> **SUPERSEDED (status reconciled 2026-11-07).** Retained as a historical research index. [Roadmap v2](2026-07-05-engine-roadmap-v2.md) retains the long-term vision; [PLAN_STATUS](../../PLAN_STATUS.md) owns current execution status: AI on GL, native renderers paused. Do not plan work from this historical file.

**Date:** 2026-07-04 (rev. 2026-07-05 — restructured around rz's version vision + cutting-edge research)
**Status:** Approved direction (rz)
**Supersedes:** `2026-03-29-macos-metal-port-design.md` (absorbed as the V1 renderer bring-up)

## North Star

Rebuild this X-Ray fork into a **native macOS engine that is the best-performing STALKER engine on Apple Silicon** — modern Metal graphics with raytracing, an AI system built for LLMs and complex simulation, and an engine a coding agent can drive and verify itself.

**The founding principle:** the original X-Ray engine and codebase are **inspiration and foundation, not scripture.** We keep what earns its place (asset formats, game logic, level data, the feel) and shed what doesn't (the D3D9-era render abstraction, the two-headed ALife data model). Faithful preservation is a non-goal; a clean, sharp, fast native engine is the goal.

**Guiding rules**
1. **Platform-agnostic, Mac-primary** *(rz 2026-07-05, reverses the earlier "native Metal over portable" rule).* The renderer is **Vulkan everywhere** — native on Linux/Windows, MoltenVK on macOS — one modern codebase across all three desktop OSes. We tune/profile hardest on Apple Silicon (primary), but Linux/Windows are first-class must-run. The native-Metal renderer is **parked** as the insurance path for the two things Vulkan-on-Metal can't do (ray tracing + TBDR tile-memory). GL stays as fallback until Vulkan parity, then retired. Full renderer plan: `2026-07-05-vulkan-renderer-roadmap.md`.
2. **Exploit unified memory properly, cross-platform.** A topology-adaptive memory strategy (§5b of the Vulkan spec) takes the **zero-staging, persistently-mapped, host-visible-device-local** path wherever UMA allows — always on Apple Silicon (primary), also on integrated GPUs / AMD APUs / ReBAR discrete — and falls back to staging only on discrete-without-ReBAR. One code path via VMA; on Apple Silicon the staging branch is dead code. The deepest TBDR-only wins (memoryless/tile-memory) remain a parked-Metal fast-path, not a baseline dependency.
3. **Stage, don't leap.** Every stage leaves the game runnable and verifiable through the agent bridge. Prove correctness, then chase performance, then extend.
4. **The engine tests itself.** The agent bridge is a first-class subsystem; every stage's exit criteria are bridge-scriptable.
5. **Content survives, mods don't.** Importers keep CoC-era levels/spawns/smart-terrains playable. Script-level (Lua) mod compatibility is deliberately dropped in the AI rehaul.

---

# V1 — Native engine: renderer, then AI

Three epics. **Renderer first, AI after.** Goal: Metal renderer stabilized and working with existing game resources **on par or faster** than the old GL path, with raytracing plumbing in place and modern graphics techniques; AI rehauled for LLMs + simulation. Best possible performance on unified-memory M-series.

**A live, always-simulated Zone is the throughline of the AI epic — and it is simulated by LLM AGENTS.** Per rz's 2026-07-05 pivot, the world's evolution (factions, ecology, economy, events, named-individual arcs, all off-screen regions) is authored by a society of persistent tool-using LLM agents — **xrMind** — with C++ (**xrSim**) demoted to a thin coarse state store + real-time near-player executor + materialization boundary. Current design: `2026-07-05-agentic-zone-design.md` (supersedes the authority model of `2026-07-05-zone-simulation-design.md`). **We keep X-Ray's classic per-level loading** (current level materialized/physical; other regions live only as coarse agent-owned state). Seamless no-loading-screen streaming (`xrStream`, `2026-07-05-seamless-zone-streaming-design.md`) is **parked**. Honest costs of the agentic world (weaker determinism, effectively online, ~$3-8/wall-hour) are in the agentic-zone spec.

## Epic T — Agent bridge ✅ DONE

Unix-socket control channel (`-agent_bridge`) — verbs `hello/cmd/lua/key/mouse/state/shot/bye`, `tools/agentctl.py` client. It is the verification harness for everything below (parity screenshots, perf capture, AI scenario replay). Spec: `2026-07-04-agent-bridge-design.md`. Built and proven.

## Epic R — Renderer (Vulkan everywhere, staged) — full spec: `2026-07-05-vulkan-renderer-roadmap.md`

**Pivot (rz 2026-07-05):** platform-agnostic **Vulkan** renderer — native on Linux/Windows, MoltenVK on macOS, one codebase. Mac primary-tuned, Linux/Windows must-run. The native-Metal backend (SP2, compiles/links, never rendered a frame) is **parked**, kept as the insurance path for Mac ray tracing + TBDR tile-memory (the two things Vulkan-on-Metal can't reach). GL stays as fallback until Vulkan parity, then retired.

**Honest verdict** (from the 11-agent replan): Vulkan/MoltenVK is sound for raster/compute/GPU-driven on all three OSes and beats today's GL 4.1 Mac path; **ray tracing is not available on Mac through MoltenVK** (unimplemented, architectural blocker, no committed date) — so RT is **native on PC first**, Mac RT deferred to the parked Metal path. Consistent with "RT-ready, defer depth."

**Big SP2 salvage:** the vendored **glslang GLSL→SPIR-V pipeline is Vulkan's native shader path** (drop only the SPIRV-Cross→MSL step); the `USE_METAL` shared-code ifdef branches become the `USE_VULKAN` template; the render-pass/pipeline/descriptor and RENDER_NAMESPACE patterns carry over. New backend pair: `xrRenderVulkan` (HAL) + `xrRenderPC_Vulkan` (`RENDER_NAMESPACE=render_vulkan`).

**Execution order: Mac-FIRST** (rz 2026-07-05). Bring Vulkan up on macOS-via-MoltenVK first, using the debug/run loop already built and proven on Mac (agent bridge + CoC setup); Linux/Windows are a dedicated port-&-verify phase (V-RX) after Mac reaches CoC-parity. MoltenVK enforces the Vulkan Portability Subset, so Mac→native-PC is the easy direction (subset→superset). Staged (details in the Vulkan spec):
- **V-R0 (Mac)** — first triangle/clear on Vulkan via SDL2 + MoltenVK; volk/vk-bootstrap/VMA; validation + portability-subset clean; verified through the Mac agent bridge.
- **V-R1 (Mac)** — deferred core (`VK_KHR_dynamic_rendering` + descriptor indexing) with the §5b zero-staging UMA path; CoC level at/below GL; bridge soak green.
- **V-R2 (Mac)** — GPU-driven (bindless, draw-indirect-count, culling compute); the "faster than GL" milestone on Apple Silicon.
- **V-RX** — port & verify on Linux then Windows; resolve portability gaps; agent bridge + CI on those OSes. **← GL deprecation gate** (Vulkan holds on all three).
- **V-R3** — hybrid RT via `VK_KHR_ray_tracing` **native on Linux/Windows**; Mac RT only if MoltenVK lands AS support, else via the parked Metal fast-path.
- Errors are **value-based** (no throw — `XRAY_EXCEPTIONS=0`).

## Epic A — AI rehaul (LLM-backed, after the renderer)

**Deterministic sim core owns all state; the LLM is an advisor, never the authority.** The LLM emits only schema-constrained *intents*; a validator maps them to legal game actions (fits `XRAY_EXCEPTIONS=0` cleanly — a parse/validation failure is a value, not a throw). Providers: Anthropic and OpenAI, **subscription-first** in the first iteration.

### A1 — Simulation core v2 (`xrSim`)
Deterministic fixed-tick world sim replacing ALife's registry/switch machinery: one data model (not online/offline duplicated), entities with needs/goals/relations (factions, economy, territory). **Importers** bring CoC `all.spawn`/LTX/smart-terrains/AI-maps into the new model so existing content plays. Snapshot/restore serialization (feeds both agent-bridge scenario tests and LLM context). **Breaking:** Lua script mods do not carry over.

### A2 — Behavior layer (the always-on local tier)
Utility-scored behavior trees replace GOAP/motivation stacks; perception + combat rebuilt on the new entities. Pathfinding core rewritten (incremental A*/HPA) over the imported AI-map graph. This is the reflex tier — and the fallback when no provider is available.

### A3 — LLM provider seam (`xrAIProvider`) — **the Zone is the LLM's domain**

**The centerpiece of the AI: the Zone itself is simulated by the LLM.** The living world — faction warfare, the economy, territorial control, mutant migrations, anomaly/emission events, stalker parties forming and dying, emergent quests and reputations, cause-and-effect across the map — is authored by an LLM **Zone Director** running on the strategic tick. This is where *all the deep, emergent complexity concentrates*: the Director reasons about the whole Zone as a system, the deterministic `xrSim` executes and persists its decisions, and the cheap tiers below stay lean and fast. The complexity budget (tokens, reasoning, latency) is spent here, on the Zone, and almost nowhere else.

Three latency tiers, complexity increasing downward:
- **Reflex (per-frame):** always local (A2), never an LLM. Movement, aiming, immediate combat.
- **Tactical (seconds):** squad orders, dialogue, reactions — Haiku / on-device SLM (llama.cpp-Metal), async, local fallback.
- **Strategic (minutes) = the Zone Director:** the LLM simulates the Zone as a whole — faction strategy, economy, emergent events, migrations, narrative causality — Sonnet/Opus, batched. Its output is a stream of **schema-constrained intents** that `xrSim` validates and applies deterministically over many ticks.

**Critical design guardrail (prevents the obvious misread):** "all complexity in the LLM" does **not** mean the LLM computes every NPC every tick — that path is a cost/coherence disaster (the per-NPC frontier-call anti-pattern). It means the Director operates at the **Zone / faction / region level of abstraction**; the mechanical fan-out to thousands of entities is `xrSim`'s job, executing the Director's intents. The LLM authors *what the Zone does and why*; the sim computes *how it plays out*.

Cloud via raw **HTTP/SSE** (no C++ vendor SDK); structured outputs / tool-use for deterministic intents; Zone/faction-scoped memory + reflection (not per-NPC). Config (provider/model/key/budget) in LTX. Every call has a bounded budget and a silent local fallback; the game never blocks on the Zone Director — a missed tick just means the world coasts on its last plan. Transport reuses the agent-bridge socket/framing code.

### A4 — Integration & tuning
Bridge-scripted scenario tests: reproducible sim snapshots, provider-on/off A/B, behavioral assertions ("squad flanks within 30s", "faction war escalates within N days"), record/replay behind a recordable interface.

## V1 milestone sequence (highest-leverage order)
1. **First Metal frame + capture harness** (R0) — compat-layer crutch reaches its one goal.
2. **Native TBDR deferred core at/below GL frame time** (R1) — pivot committed.
3. **GPU-driven + temporal upscale** (R2) — the "faster than GL" promise met.
4. **Hybrid RT shadows + denoise** (R3) — RT-ready proven.
5. **AI seam: the Zone Director live on one region, through record/replay** (A1→A3 vertical slice) — AI strictly after the renderer.

---

# V2 — Polish & sharpen

Full polish pass once V1 is functionally complete: eliminate obvious code and programmatic issues, retire the GL fallback (after the native renderer holds parity for two milestones), remove dead D3D9-era scaffolding and the compat layer, tighten error handling and resource lifetimes, close the honest-TODO lists accumulated across V1, and harden AI cost/latency. The goal is a codebase that is *sharp* — no rough edges, no "temporary" left standing.

---

# V3 — AI maturation: debug & deepen complexity

Where the **Zone becomes a properly LLM-simulated living world**, not just a working seam. V1 proves the Zone Director on one region; V3 makes it good and scales it Zone-wide: debug Director behavior at scale (latency, cost, coherence, fallback correctness), deepen the simulation the Director steers (rich faction/economy/territory dynamics, long-term Zone + faction memory and reflection, emergent narrative that persists and pays off), tune the tiering, and widen what the Director can meaningfully control. Bridge-scripted behavioral assertions and record/replay from A4 are the tools; the goal is a Zone that feels genuinely alive and holds together under complexity — the game's signature feature.

# V4 — Reserved (expansion band)

Deliberately open. The room between a mature engine and the Rust rewrite — the natural home for whatever the engine has earned the right to do by then: graphics R&D (mesh shaders, neural materials), broader content/mod tooling, new game-data support, platform breadth. Scope intentionally undecided until V1–V3 land; not planned in detail yet.

# V5 — Rust rewrite for extreme performance

Complete rewrite of the (by-then clean, well-understood) engine into Rust — the long-horizon endgame, not a near-term goal. Approach: incremental FFI-based migration (`cxx`/`bindgen`) starting from leaf/pure-logic subsystems (sim, math, asset pipeline) toward the graphics/FFI-heavy core; Metal via `metal-rs`; ECS for the sim. Everything before it exists partly to *earn* this — a sharp C++ engine with clear module boundaries and a self-testing harness is the precondition for a safe rewrite, not a leap of faith.

---

## Top risks (with first moves)
1. **The compat layer becoming the architecture** — freeze it at R0's exit; R1 is a fresh path, not an edit of R0. This is the single most important discipline.
2. **Silent regressions, no test suite** — the agent-bridge soak + parity screenshots are the gate; every renderer stage must pass before merge.
3. **Metal 4 / MetalFX API name drift (beta→GA)** — pin to shipping Metal 3 first; layer Metal 4 behind capability checks; verify selector names against final headers.
4. **Hardware gating fragmenting the fleet** — RT/M3+ features are additive toggles; the base renderer runs on all M-series.
5. **Naive per-NPC frontier LLM calls** (cost/coherence blowup) — faction-scoped director + tiering + budgets from the first line of A3.
6. **SDL2 hiding the present path** from ProMotion pacing — plan a direct CAMetalLayer present hook in R2.

## Research provenance
The concrete technique choices above come from a 13-agent research + adversarial-verify pass (2026-07-05) against WWDC 2021–2025 and Apple docs. Corrected claims (struck from the source brief): ARM Mali bandwidth figures aren't Apple's; "MLX 3× llama.cpp" is false; it's "Foundation Models" not "Core AI"; `output_format` is deprecated. Full brief in the session artifact.
