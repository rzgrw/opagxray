# Open Agentic XRay — Handover

**Last updated:** 2026-11-08 (Gate 1 standalone tests and Metal M-R1 assessment merged into `dev`; prior status refresh was 2026-11-07 against `dev` `950a7820b`)
**Written for:** a fresh coding agent (Codex) or engineer picking this repo up with zero prior context.

---

## 0. TL;DR — current direction

Read [PLAN_STATUS.md](PLAN_STATUS.md) first for shipped refs, historical evidence, recovery order, and open acceptance. Development is moving to **Windows 11**; read [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md) before building or testing there. Runtime observations below were recorded on macOS in earlier sessions; this refresh did not rebuild or rerun them.

The repository is [rzgrw/opagxray](https://github.com/rzgrw/opagxray), a **detached fork**: OpenXRay/IXRay are inspiration and selective donors, with no upstream PR flow.

Two things guide current work:

1. **OpenGL is the working runtime.** The game boots, renders, and is fully playable on the GL backend (`renderer renderer_r3`). This is the baseline for everything below.
2. **The immediate focus is GL stability and frame-time consistency compared with IXRay.** AI development stays on GL; Task 1 canonical state is integrated, and further AI expansion follows the comparison. The renderer rewrite (native Metal / Vulkan) is **paused**. We are not blocked on graphics — GL is good enough to build the AI-driven Zone on top of. Metal M-R0 (menu) shipped; Metal M-R1 (in-game) is parked mid-flight with a clean resume guide (§4).

So: **build GL, measure GL, preserve the AI-on-GL direction.** Pick up Metal later from §4 if/when someone wants to resume the native-renderer track.

Historical roadmap for the overall vision (renderer-first sequencing superseded): [`docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`](superpowers/specs/2026-07-05-engine-roadmap-v2.md).
Original AI design (refined by the thin-harness and authority-spine specs linked in the inventory): [`docs/superpowers/specs/2026-07-05-agentic-zone-design.md`](superpowers/specs/2026-07-05-agentic-zone-design.md).

---

## 1. Repo state & branches

**Branch policy:** `main` is stable (GitHub default) and moves only by fast-forward from `dev` after a recorded gate; `dev` is experimental integration. Feature work goes on short-lived branches that are merged then deleted, or archived as annotated `archive/<name>` tags. Recover an archived tip with `git fetch origin tag archive/<name>` and `git switch -c <x> archive/<name>`.

| Ref | State | Notes |
|---|---|---|
| `main` | `950a7820b` (2026-11-07) | stable; GitHub default branch |
| `dev` | started at `950a7820b`; later commits are docs-only so far | experimental integration: M-R0, AI harness, IXRay foundation, Task 1, frame-time telemetry/provenance, menu reload repair, Windows handoff |
| tag `archive/codex/ai-authority-spine-gate1` → `1d951aa07` | unmerged recovery source | partial Tasks 1–10; not ready to merge, no Gate 1 completion claim. 2026-11-08: only `tools/xrsim_standalone` was taken into `dev` (2026-11-08) |
| Task 1 canonical state | integrated at `04b4ef5b5` | independently reviewed and tested; authority/commit remain queued after the runtime comparison |
| `feat/metal-mr0-first-frame` (historical, deleted) | **merged to dev** via PR #9 (repository was then named `OpenXVibeRay`) | Metal renders the CoC main menu. GL/DX11 untouched. |
| tag `archive/feat/metal-mr1-ingame` → `caa2e4953` | WIP, **not merged** | Metal deferred-pipeline foundation; in-game scene not yet rendering. Resume guide in §4. 2026-11-08: merges cleanly onto `dev` `437cca1aa` and was left archived because it edits shared GL shader sources; keep those shader edits together with the `r2_R_render.cpp` menu-quad change when resuming. |
| tags `archive/rzgrw/frame-overhead-base` → `617957470`, `archive/rzgrw/frame-overhead-reference` → `d603c01d2` | selectively integrated into `dev` (2026-11-08) | probe core, both standalone assert-based test targets (MSVC-built, exit 0), bridge/run-loop hooks, and the opt-in `XRAY_OVERHEAD_PROBE` CMake option (default OFF; probe sources not compiled when off); the pinned probe adapter, `tools/_overhead_deps` snapshot, and Mac-only `overhead_approved_builds_v1.json` remain archived |
| tag `archive/transfer/windows-2026-11-07` → `d336361d1` | selectively integrated into `dev` (2026-11-08) | `docs/transfer/2026-11-07/` evidence pack and provenance WIP `8cf2af572` (external communication directory plus edits that **aim** to repair the two P2 review findings — unverified until rechecked against the reviewer's `external-probes.py` on macOS); the archived WINDOWS_HANDOFF restore block was superseded by the rewritten dev doc and not ported |

All Metal changes are `USE_METAL`/`SM_METAL`-gated, so **GL and DX11 output is unchanged** on every branch.

---

## 2. Build

Windows 11 (current development machine): **not yet verified** on current `dev`. Follow [windows-dev-setup.md](windows-dev-setup.md) (toolchain, submodules, recommended CMake + Ninja route, assets, bridge gap). The MSBuild `src/engine.sln` route lacks this fork's CMake-only sources (`xrEngine.vcxproj` lists no `AgentBridge`, `xrSim`, or `FrameTime*` files) and the Windows CMake CI entries are commented out. Rebuild all engine/game modules together; `IGame_Level`/`MainMenu` layouts changed in `604fb0690`.

macOS (Apple Silicon):

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

- Binaries land in `bin/arm64/Release/` (NOT `build/bin/`).
- On macOS Tahoe beta also pass `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`.
- `sysctl -n hw.ncpu` returns a trailing space on Tahoe — use a literal `-j10`.
- **Build times:** a Metal-renderer rebuild is >2 min; give `cmake --build` a timeout ≥ 7 min. (The default 2-min shell timeout will kill it mid-link.)
- **C++20** is configured in `cmake/XRay.Build.cmake`; C++17 plan snippets are a portable subset.
- **GL is the default build.** `-DXRAY_METAL=ON` additionally builds/links the Metal module; leave it off for GL/AI work.

Hard constraint — **`XRAY_EXCEPTIONS=0` on Darwin** (`src/CMakeLists.txt`): C++ exceptions cannot cross LuaJIT's ARM64 trampoline, so `THROW` compiles to `VERIFY` (a fatal abort). **All new code must return failures as values, never throw.** SPIRV-Cross/glslang exceptions are OK only because they're caught entirely below any Lua frame.

---

## 3. Run & test (the agent bridge is the harness)

The engine is self-testable over a Unix socket. This is how you (an agent) drive and observe the game on macOS/Linux. On Windows the bridge is a stub (`AgentBridge.cpp` implements only the non-`XR_PLATFORM_WINDOWS` path) and frame-time export returns `export_unavailable`; a Windows control transport is an explicit open task.

### 3.1 Private runtime and launch

Follow [macOS setup](macos-dev-setup.md) for the exact primary IXRay CoC 1.4.12
base + 1.4.22 patch archive paths, target GL overlay, and explicit launch/bridge
commands. Use separate private user and verification runtimes/appdata. The old
Downloads run directory and save `1` are historical, not valid defaults; do not
copy donor saves/configs. Begin fresh tests with a new game and a unique private
save. Keep the game process alive through the terminal/task runner.

### 3.2 Verification status

Foundation `8984220b6` records 41 Python tests, two C++ targets, a corrected menu
capture, and a fresh Cordon movement/inventory/save/reload/quit gate (vsync off).
The original ignored artifacts and execution worktree are now absent: this is
historical evidence, not a current rerun. Full five-run GL/20-minute idle,
direct empty-group execution, sustained vsync-on gameplay, current private pack
verification, and hardened live non-coast acceptance remain open.

Later macOS records: telemetry/classification work (`a389540d8`, `456808010`) ran a
full GL build and private Cordon functional runs, and the menu reload repair
(`604fb0690`) passed focused/sanitizer tests plus two private live saved-load
lifecycles with exit 0 (see PLAN_STATUS and WINDOWS_HANDOFF). Frame-time captures
still fail asset-provenance acceptance; no performance number exists.

### 3.2a Frame-time telemetry (opt-in)

Bridge verbs `perf.start`, `perf.status`, `perf.stop`, `perf.reset`, `perf.export`
(export only when stopped; exclusive, no-overwrite). Analyse with
`tools/frame_time_report.py`; attest assets with `tools/frame_time_provenance.py`
(macOS process inspection only). Design: [measurement](superpowers/specs/2026-11-07-frame-time-measurement-design.md),
[provenance](superpowers/specs/2026-11-07-frame-time-provenance-design.md),
[overhead](superpowers/specs/2026-11-07-frame-time-overhead-design.md).

### 3.3 Bridge and GL gate

Use `tools/agentctl.py <private-socket> <verb>` and pass `--game-dir`, `--binary`,
`--socket`, and `--artifacts` explicitly to `tools/gl_macos_soak.py`, as shown in
[macOS setup](macos-dev-setup.md#gl-macos-stability-gate). Verbs include
`hello/cmd/lua/key/mouse/state/shot/bye`. The old load/play scripts and AI smoke
examples below must be adapted to the private runtime and its actual save;
do not execute their historical implicit defaults. Frame freshness alone does
not establish image correctness: inspect world, inventory, and menu captures.

### 3.4 Two gotchas that will waste your time
- **Do NOT use `-nosound`.** It crashes CoC's `sound_theme.script` (`nil played_id`). To silence audio, set in `appdata/user.ltx` (while the game is stopped): `snd_volume_eff 0.` and `snd_volume_music 0.` — keeps the sound subsystem alive so scripts work.
- **Renderer select** is `renderer` in `appdata/user.ltx`: `renderer_r3` = GL (**current default**), `renderer_metal` = Metal. Change while the game is stopped.

---

## 4. Metal M-R1 resume guide (parked — read only if resuming the renderer)

**Status:** menu renders perfectly on Metal; in-game the **gbuffer geometry pass works** (rt_Position holds the full scene) but the **deferred lighting/combine chain produces a magenta frame**. Foundation is committed at `caa2e4953`, preserved as tag `archive/feat/metal-mr1-ingame` (the branch was deleted).

**2026-11-08 assessment (not integrated):** `git merge-tree` onto `dev` `437cca1aa` applied with no conflicts (result tree `e5579d7dc`). The commit still edits `res/gamedata/shaders/gl/` (six shader files the GL renderer loads) plus shared `xrRender` / `xrRender_R2` sources. C++ branches are `USE_METAL`-gated and the shader `#else` text matches the previous GL lines, but those shader files are outside Metal-only paths, and the menu-quad change depends on `stub_notransform_t_menu.vs`. The commit was left whole on the tag. It was not built: no macOS or Metal SDK on this machine. `XRAY_METAL` stays OFF.

### 4.1 What already landed (all correct, verified)
- **Default sampler** bound with every texture (`metalHW.*`, `metalR_Backend_Runtime.h`). Unbound samplers are UB on Metal and were corrupting all textured draws.
- **`SM_METAL` macro** (`metal_shaders.cpp`) → GL-only Y-flips take the D3D form (Metal RTs are top-left origin, `[0..1]` depth). Applied in the 5 fullscreen stubs + `ssr.h`.
- **SPIRV-Cross `set_enabled_interface_variables(get_active_interface_variables())`** — strips unwritten varyings so PSO validation stops rejecting whole shader pairs.
- Texgen DX11 texel-adjust, sun/rain ortho `[0..1]` depth, sun-shadow TexelAdjust matrices — all switched to the D3D form on Metal.

### 4.2 Remaining work (from the convention audit — 27 confirmed findings)
Prime suspect first:
1. **Dynamic stencil DSS.** `set_Stencil`/`StencilRef` are no-ops on Metal (`metalR_Backend_Runtime.h`, `metalState.cpp`) → the gbuffer stencil mask (ref `0x01`, baked by `blender_deffer_flat`) is never written → the deferred combine's `stencil>=1` test likely gates the whole scene out. **Implement a hashed `MTL::DepthStencilState` cache applied at draw time.** This is the most likely single blocker.
2. **`combine_1.vs`** deferred-composite uv/Y — fix *together* with `combine_2` (`stub_notransform_aa_aa.vs`); their flips currently cancel, so fixing one alone inverts the frame.
3. **accum_direct** NDC/mask quad texcoord tables — adopt the R4 twin's mapping (`r4_rendertarget_accum_direct.cpp`); plus a 3rd `TexelAdjust` site (~L891) and the volumetric texgen (~L1178) still carrying GL `+0.5` Y.
4. Per-pass `CULL_NONE` for fullscreen passes that inherit `CULL_CCW` (bloom, PP, luminance).

Full findings: re-run the audit workflow (it self-documents) or read the commit message of `caa2e4953`.

### 4.3 Metal debug harness (re-add when resuming)
- **Intermediate-RT present:** a temporary `getenv("MTL_DEBUG_BLIT")` switch in `metal_rendertarget_phase_flip.cpp` that blits `position|color|normal|generic0|accum` to the drawable — walks the deferred chain without rebuilds. (Removed for the clean commit; re-add as needed.) **Note:** `rt_Color` is reused mid-frame (accumulator transfer), so an end-of-frame blit of it ≠ the gbuffer albedo — blit mid-frame or check a different RT.
- **Metal API validation:** launch with `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog`. This is what surfaced the unbound-sampler bug ("missing Sampler binding for s_baseSmplr").
- **One-shot `Msg` dumps** + `IWriter`-to-`$app_data_root$` MSL dumps in `metalShaderCompiler.cpp` are the pattern for inspecting generated MSL.

### 4.4 THE shader-sync gotcha (cost a full debug cycle)
The running game loads GLSL from a **copy** at `<CoC gamedata>/shaders/gl/`, **not** from the repo's `res/gamedata/shaders/gl/`. Editing a repo shader has **zero effect** until you `cp` it to the game copy. Symptom: your `#ifdef SM_METAL` fix looks like a no-op and the compiled MSL shows the `#else` branch. **Sync per-file after every shader edit; `grep` the game copy to confirm.** (Don't blanket-symlink — the game set is a CoC-specific superset of the repo base set.)

---

## 5. The active work — AI implementation on GL

**Gate 1 Task 1 canonical state is integrated** at `04b4ef5b5`; further authority-spine recovery follows the current stability/performance investigation. Read [PLAN_STATUS.md](PLAN_STATUS.md#gate-1-recovery-and-remaining-gates) for order and unresolved Task 3 contracts. On 2026-11-08, the recovery merged into `dev` took only `tools/xrsim_standalone` from tag `archive/codex/ai-authority-spine-gate1` (`1d951aa07`). Tasks 2+, live zone wiring, and the reanimation proposal stay on that tag.

Standalone xrSim tests do not need the engine. On Windows, from a VS 2022 x64 developer shell (verified 2026-11-08, MSVC 19.44, curl absent): `cmake -S tools\xrsim_standalone -B build-xrsim -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build-xrsim --parallel`, then `build-xrsim\xrSimAuthoritySpineTests.exe` and `build-xrsim\xrSimWorldStateTests.exe`. Both exited 0 with no `FAIL` lines (14 groups and 76 cases in `main`). `tools/xrsim_standalone/build_and_test.sh` remains the Linux/macOS entry.

Original agentic-Zone design: [`docs/superpowers/specs/2026-07-05-agentic-zone-design.md`](superpowers/specs/2026-07-05-agentic-zone-design.md).

Shape of it (per the spec and the design decisions in project memory):
- The Zone is **simulated by LLM agents** (persistent `WorldAgents`) that own world evolution. This is a **complete simulation rewrite**, not a sidecar: legacy ALife becomes the compatibility/materialization surface while `xrMind` + `xrSim` take over world authority.
- **Cloud-first, thin offline.** Move the bulk of compute to LLMs (Anthropic/OpenAI) — the local machine's resources are reserved for the renderer/engine, not a load-bearing local model.
- The authority-spine contract remains **Sonnet-only**. `claude-sonnet-5` is a historical plan/source value requiring an official availability check before live use; no model/provider substitution is authorized here. Keep deterministic-null/replay tests as the CI floor.
- Honest trade-offs the spec commits to: weaker determinism (replay via recorded tool-calls), effectively online, ~\$3–8/wall-hr.
- Invariants that carry over from the engine work: **stable IDs never regenerated**, **two-phase validation / no-THROW** (see §2), materialization contract.

Because GL already runs the full game, the AI work does **not** need the renderer rewrite — build against the GL runtime and the agent bridge (§3) as the verification harness.

### 5.1 xrSim foundation checkpoint

The first AI substrate began on the historical `codex/ai-zone-foundation` branch (deleted; fully contained in `dev`); the provider/actor continuation is now included in the reconciled `dev` baseline. `src/xrEngine/xrSim/` contains a deterministic coarse `WorldState`, stable index+generation handles, clamped `adjust_population`, an append-only tool log, actor intent validation, and the provider handoff. The C++ substrate remains replayable even though live model output is not deterministic.

Debug bridge verbs are available in `-agent_bridge` sessions:

```bash
python3 tools/agentctl.py <sock> ai.reset
python3 tools/agentctl.py <sock> ai.observe
python3 tools/agentctl.py <sock> ai.inject adjust_population debug_region blind_dog 500
python3 tools/agentctl.py <sock> ai.snapshot
python3 tools/agentctl.py <sock> ai.log
python3 tools/agentctl.py <sock> agent.provider live  # Sonnet-tier provider shell; coasts if no API key
```

Smoke script:

```bash
python3 tools/gl_macos_soak.py --scenario tools/ai_zone_smoke.txt --artifacts artifacts/ai_zone_smoke
```

Thin-harness actor checkpoint:
- `src/xrEngine/xrSim/` now includes debug `SquadAgent` and `MutantPackAgent` records that parse `xrsim_actor_intent_v1`, validate actor actions, and emit deterministic actuator command streams.
- Bridge verbs:
  - `agent.actor.list`
  - `agent.actor.observe squad`
  - `agent.actor.wake squad`
  - `agent.actor.observe mutant_pack`
  - `agent.actor.wake mutant_pack`
  - `agent.actor.commands`
- Session pack verbs create and register real mutant objects for the current level session:
  - `agent.pack.spawn <section> <count> [radius_m]`
  - `agent.pack.list`
  - `agent.pack.observe <pack_id>`
  - `agent.pack.wake <pack_id>`
- Smoke: `python3 tools/gl_macos_soak.py --scenario tools/ai_thin_harness_smoke.txt --artifacts artifacts/ai_thin_harness_smoke`

Provider and asynchronous actor checkpoint:
- Source defaults at `8984220b6` are `provider=anthropic`, `model=claude-sonnet-5`, `timeout_ms=30000`; this does not verify current model availability.
- Env overrides: `XRAY_AGENT_PROVIDER`, `XRAY_AGENT_MODEL`, `XRAY_AGENT_API_KEY` or `ANTHROPIC_API_KEY`, `XRAY_AGENT_TIMEOUT_MS`.
- Missing key is a normal **coast** value (`reason=missing_api_key`), not an error/throw. With a key present, the live provider shell now has an optional curl-backed HTTP transport when `XRAY_AGENT_HTTP=ON` and CMake finds `CURL::libcurl`; otherwise it still coasts with `network_adapter_not_linked`.
- `agent.provider live` reports the effective shell state and `transport=<curl|unavailable>` through the bridge; deterministic null remains the default runtime provider.
- The Anthropic Messages API envelope is shaped locally (`POST /v1/messages`, API version `2023-06-01`, JSON body with `model`, `max_tokens`, `messages`). Coarse Zone responses decode as `xrsim_agent_response_v1`; actor/pack responses decode as `xrsim_actor_intent_v1`.
- Live actor and pack requests now run through one cancellable provider worker. Wake preparation copies all context, the worker owns the synchronous curl call off the frame thread, and polling applies the validated result back on the engine/bridge thread. `ai.reset` and shutdown cancel in-flight curl work.
- Live bridge flow:

```bash
python3 tools/agentctl.py <sock> agent.actor.wake squad live
python3 tools/agentctl.py <sock> agent.actor.poll 1
python3 tools/agentctl.py <sock> agent.pack.wake '1 live'
python3 tools/agentctl.py <sock> agent.actor.poll 2
```

  A wake returns `... queued request=N`; poll returns `pending` or the applied result with provider/model/coast metadata. Deterministic wake payloads remain backward compatible. The older coarse `agent.wake live` path is still a synchronous diagnostic; do not use it from a frame-critical scheduler.

- **Real Anthropic acceptance status (2026-07-14):** API-key propagation, `state=ready`, curl transport, and off-frame execution are proven; frames continued advancing during every live request. The model added a prose/fence preface and echoed `agent_id`, then `scope`, exposing that the actor prompt did not define its response grammar precisely enough. The codec now tolerates the same preface/CRLF and numeric metadata already accepted by the coarse codec, and the prompt now lists every legal output record while explicitly forbidding prose, Markdown, and observation echo. Unit tests and the full build cover these changes, but the strengthened prompt still needs one fresh-key live non-coast acceptance run. Do not claim that final gate green yet.

Current boundary / next AI milestone:
- Actor commands are validated and retained in the xrSim command ledger, and coarse tools such as `adjust_population` mutate `WorldState`, but movement/combat commands are **not yet steering the spawned X-Ray objects**. Wire the command stream into the near-player ALife/monster executor without giving the LLM direct per-frame control.
- Replace manual bridge dispatch with the surprise/cadence scheduler, bounded in-flight accounting, cost/token ledger, and record/replay packets from the agentic-Zone spec.
- Transfer authoritative lifecycle ownership from legacy ALife to xrMind/xrSim in slices; the current session pack registry is a verified materialization seam, not the completed ownership inversion.

---

## 6. Conventions & key directories

- Renderer branches: `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)`. Keep GL/DX11 output unchanged when touching shared code.
- Each renderer module compiles with `RENDER_NAMESPACE=render_gl` / `render_metal`.
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`. Assertions: `VERIFY`, `R_ASSERT`, `THROW` (= `VERIFY` on macOS).
- `src/Layers/xrRenderPC_GL/` + `src/Layers/xrRenderGL/` — the working GL renderer.
- `src/Layers/xrRender/` + `src/Layers/xrRender_R2/` — shared render code / CRender phases.
- `src/xrEngine/AgentBridge.{h,cpp}` — the test bridge. `tools/agentctl.py` — its client.
- `src/xrGame/` — game logic, ALife, Lua bindings (the surface the AI rehaul will reshape).

## 7. Where the detailed context lives
- Current inventory and recovery gates: [PLAN_STATUS.md](PLAN_STATUS.md)
- Windows setup audit: [windows-dev-setup.md](windows-dev-setup.md)
- Windows transfer and next-session order: [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md)
- Roadmap / vision: `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`
- AI design: `docs/superpowers/specs/2026-07-05-agentic-zone-design.md`
- macOS setup: `docs/macos-dev-setup.md`
- `CLAUDE.md` — project guide (build, constraints, layout).
