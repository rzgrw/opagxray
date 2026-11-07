# AI Zone Foundation Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `c8d671514`, `27b7dc9a2`, `b002d9d7a`; unchecked boxes do not imply missing code. Model strings are historical, not verified current availability. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first deterministic xrSim substrate for the agentic Zone on top of the stabilized GL runtime.

**Architecture:** Add a small `xrSimCore` library under `src/xrEngine/xrSim` for stable handles, coarse region/species population state, validation, clamped tool application, and an append-only tool log. Wire read-only/debug bridge verbs through `AgentBridge` so the GL harness can observe and inject deterministic-null AI intents before provider calls exist.

**Tech Stack:** C++20, CMake, stdlib containers, existing Unix socket agent bridge, Python `unittest` for bridge tooling checks, standalone C++ assert-based tests for the xrSim substrate.

## Global Constraints

- macOS builds use `XRAY_EXCEPTIONS=0`; new AI code returns result values and never throws for normal failures.
- GL remains the runtime host; no renderer changes are part of this plan.
- Stable IDs are never regenerated; handles use index plus generation.
- LLM/provider calls are out of scope for this slice; deterministic-null and bridge-visible state come first.
- No live ALife behavior changes are part of this slice; xrSim state is debug-controlled until materialization work begins.
- The AI initiative is a complete simulation rewrite; legacy ALife should be treated as the compatibility/materialization layer, not the target authority.
- Future provider-backed in-game AI tests default to Sonnet-tier (`claude-sonnet-5` in the current spec) after deterministic-null replay gates pass.

---

### Task 1: Deterministic Coarse World State

**Files:**
- Create: `src/xrEngine/xrSim/xrSimWorldState.h`
- Create: `src/xrEngine/xrSim/xrSimWorldState.cpp`
- Create: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Produces: `xrSim::WorldState`, `xrSim::Result`, `xrSim::Handle`, `xrSim::ApplyAdjustPopulation`, `xrSim::WorldState::Digest()`.
- Consumes: only C++ stdlib types and fixed-width integers.

- [ ] **Step 1: Write the failing test**

Create a test executable that constructs one region and species, applies positive and negative population intents, and asserts the accepted delta is clamped and logged.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`
Expected: FAIL because `xrSimWorldState.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Implement the store with value-returning validation, stable handles, deterministic digest ordering, population clamps, and append-only tool records.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./build/src/xrEngine/xrSimWorldStateTests`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim && git commit -m "feat: add xrSim world state core"`

### Task 2: Agent Bridge Debug Verbs

**Files:**
- Create: `src/xrEngine/xrSim/xrSimBridge.h`
- Create: `src/xrEngine/xrSim/xrSimBridge.cpp`
- Modify: `src/xrEngine/AgentBridge.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `xrSim::WorldState`.
- Produces: bridge verbs `ai.status`, `ai.reset`, `ai.observe`, `ai.inject`.

- [ ] **Step 1: Write the failing bridge command test**

Extend the C++ test executable to call `xrSim::HandleBridgeVerb("ai.reset", "", ok)` and `xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 6", ok)`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`
Expected: FAIL because `HandleBridgeVerb` is not defined.

- [ ] **Step 3: Write minimal implementation**

Add the bridge handler and route `ai.*` verbs from `CAgentBridge::HandleRequest` before the unknown-verb response.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./build/src/xrEngine/xrSimWorldStateTests`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add src/xrEngine/AgentBridge.cpp src/xrEngine/CMakeLists.txt src/xrEngine/xrSim && git commit -m "feat: expose xrSim debug bridge verbs"`

### Task 3: GL Harness Visibility

**Files:**
- Create: `tools/ai_zone_smoke.txt`
- Modify: `tools/gl_macos_soak.py`
- Modify: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: bridge verbs from Task 2.
- Produces: a scriptable smoke path that proves the AI substrate is reachable from a GL run.

- [ ] **Step 1: Write the failing tooling test**

Add a Python unit test that dry-runs `tools/ai_zone_smoke.txt` and asserts it includes `ai.reset`, `ai.observe`, and `ai.inject`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tools.tests.test_gl_macos_soak -v`
Expected: FAIL because the script does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Add `tools/ai_zone_smoke.txt` with bridge commands that reset, observe, inject one population adjustment, observe again, then quit.

- [ ] **Step 4: Run tests and build**

Run: `python3 -m unittest tools.tests.test_gl_macos_soak -v && cmake --build build -j10 --target xr_3da`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add tools/ai_zone_smoke.txt tools/tests/test_gl_macos_soak.py docs/HANDOVER.md && git commit -m "test: add xrSim bridge smoke script"`
