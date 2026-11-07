# Mutant Pack Session Spawn Implementation Plan

> **Status reconciliation (2026-11-07):** SPAWN SEAM SHIPPED: `a374e6bc9`, `558f73348`, `0e494f6a9` plus fixes. Current private-runtime pack verification and full AI ownership remain open. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spawn a mutant pack during a live game session, register it as an xrSim `MutantPackAgent`, and wake it through the thin LLM harness.

**Architecture:** Keep xrSim game-agnostic by adding pack spawn parsing and pack registry state to `xrSimCore`. Keep the real spawn in `xrGame` by adding a small virtual method on `IGame_Level` that `CLevel` implements with the existing server spawn path. `AgentBridge` coordinates: parse `agent.pack.spawn`, ask the loaded level to spawn members near the player, then register the returned object ids with xrSim.

**Tech Stack:** C++17, existing xrSim unit test executable, existing Unix agent bridge, existing `xrServer::Process_spawn` / `CLevel` spawn path, GL runtime.

## Global Constraints

- Darwin uses `XRAY_EXCEPTIONS=0`; all new failure paths return values, not `THROW`.
- Do not make `xrEngine` depend on `xrGame`; cross the boundary through `IGame_Level`.
- Do not use Lua for the core spawn verb.
- Pack spawn count is clamped to `1..8`; radius is clamped to `1..40`.
- Deterministic actor provider remains the CI default; live Sonnet is an optional verification mode.
- Do not change legacy stalker AI in this slice.

---

## File Structure

- `src/xrEngine/xrSim/xrSimPackSpawn.h`: value types for pack spawn requests/results and pack registry records.
- `src/xrEngine/xrSim/xrSimPackSpawn.cpp`: payload parser, formatter, pack registry, bridge-facing pack verbs.
- `src/xrEngine/xrSim/xrSimBridge.cpp`: route `agent.pack.register`, `agent.pack.observe`, `agent.pack.wake`, `agent.pack.commands` to the new registry/runtime.
- `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`: tests for parser, registry, pack wake, and bridge verbs.
- `src/xrEngine/CMakeLists.txt`: add the new xrSim source/header.
- `src/xrEngine/IGame_Level.h`: add a small virtual spawn hook with default value failure.
- `src/xrEngine/AgentBridge.h`: add `VerbAgentPackSpawn`.
- `src/xrEngine/AgentBridge.cpp`: handle `agent.pack.spawn` before generic xrSim routing.
- `src/xrGame/Level.h`: declare the `IGame_Level` spawn override.
- `src/xrGame/Level_network_spawn.cpp`: implement the spawn override using server spawn so actual ids are returned.

---

### Task 1: xrSim Pack Registry

**Files:**
- Create: `src/xrEngine/xrSim/xrSimPackSpawn.h`
- Create: `src/xrEngine/xrSim/xrSimPackSpawn.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`
- Test: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`

**Interfaces:**
- Consumes: `ActorRuntime::Wake(WorldState&, ActorAgentRecord&, const std::string&, uint32_t)`
- Produces:
  - `PackSpawnParseResult ParsePackSpawnPayload(const std::string& payload)`
  - `PackRegisterResult RegisterSessionMutantPack(const PackRegistration& registration)`
  - `std::string FormatPackList()`
  - `std::string ObservePack(uint32_t packId, const WorldState& world)`
  - `ActuatorResult WakePack(uint32_t packId, WorldState& world, ActorRuntime& runtime, uint32_t gameDay)`

- [ ] **Step 1: Write failing parser tests**

Add tests:

```cpp
bool TestPackSpawnPayloadParserAcceptsDefaults()
{
    const xrSim::PackSpawnParseResult parsed = xrSim::ParsePackSpawnPayload("dog_weak 3");
    bool ok = Expect(parsed.ok, "pack spawn parser accepts section and count");
    ok = Expect(parsed.request.section == "dog_weak", "pack spawn parser captures section") && ok;
    ok = Expect(parsed.request.count == 3, "pack spawn parser captures count") && ok;
    ok = Expect(parsed.request.radiusMeters == 8, "pack spawn parser defaults radius") && ok;
    return ok;
}

bool TestPackSpawnPayloadParserRejectsUnsafeValues()
{
    bool ok = true;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 0").ok, "pack spawn parser rejects zero count") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 9").ok, "pack spawn parser rejects too many members") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 0").ok, "pack spawn parser rejects zero radius") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 41").ok, "pack spawn parser rejects too large radius") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 8 trailing").ok, "pack spawn parser rejects trailing tokens") && ok;
    return ok;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/arm64/Release/xrSimWorldStateTests`

Expected: compile fails because `xrSimPackSpawn.h` and parser symbols do not exist.

- [ ] **Step 3: Implement parser and value types**

Add:

```cpp
namespace xrSim
{
struct PackSpawnRequest
{
    std::string section;
    uint32_t count = 0;
    uint32_t radiusMeters = 8;
};

struct PackSpawnParseResult
{
    bool ok = false;
    std::string reason;
    PackSpawnRequest request;
};

PackSpawnParseResult ParsePackSpawnPayload(const std::string& payload);
}
```

The parser uses `std::istringstream`, requires `section count [radius]`, rejects trailing tokens, count outside `1..8`, and radius outside `1..40`.

- [ ] **Step 4: Run parser tests**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: parser tests pass; other existing tests remain green.

- [ ] **Step 5: Write failing registry and wake tests**

Add tests:

```cpp
bool TestSessionPackRegistryRegistersIdsAndObserves()
{
    xrSim::ResetSessionPacksForTests();
    xrSim::PackRegistration registration;
    registration.section = "dog_weak";
    registration.objectIds = {1010, 1011, 1012};
    registration.spawnReason = "session_spawn";

    const xrSim::PackRegisterResult result = xrSim::RegisterSessionMutantPack(registration);
    bool ok = Expect(result.ok, "pack registry accepts valid registration");
    ok = Expect(result.packId == 1, "pack registry allocates first pack id") && ok;
    ok = Expect(xrSim::FormatPackList().find("PACK#1") != std::string::npos, "pack list includes id") && ok;
    ok = Expect(xrSim::FormatPackList().find("members=3") != std::string::npos, "pack list includes member count") && ok;
    return ok;
}

bool TestSessionPackWakeUsesMutantPackIntent()
{
    xrSim::ResetSessionPacksForTests();
    xrSim::PackRegistration registration;
    registration.section = "dog_weak";
    registration.objectIds = {1010, 1011, 1012};
    const xrSim::PackRegisterResult registered = xrSim::RegisterSessionMutantPack(registration);

    xrSim::WorldState world;
    const xrSim::Handle region = world.CreateRegion("debug_region", 100);
    const xrSim::Handle species = world.CreateSpecies("blind_dog");
    bool ok = Expect(world.SetPopulation(region, species, 50).ok, "pack wake setup accepts population");

    xrSim::ActorRuntime runtime;
    const xrSim::ActuatorResult wake = xrSim::WakePack(registered.packId, world, runtime, 0);
    ok = Expect(wake.ok, "pack wake succeeds") && ok;
    ok = Expect(runtime.LastProviderResult().plan.goal == "feed_without_losing_alpha",
        "pack wake uses mutant pack deterministic intent") && ok;
    ok = Expect(!wake.commands.empty(), "pack wake emits actuator commands") && ok;
    return ok;
}
```

- [ ] **Step 6: Run registry tests to verify failure**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`

Expected: compile fails because registry symbols do not exist.

- [ ] **Step 7: Implement registry and wake**

Implement a static in-memory registry for this slice:

```cpp
struct PackRegistration
{
    std::string section;
    std::vector<uint16_t> objectIds;
    std::string spawnReason;
};

struct PackRegisterResult
{
    bool ok = false;
    uint32_t packId = 0;
    std::string reason;
};
```

Create an `ActorAgentRecord` with `scope=ActorAgentScope::MutantPack`, `agentId=2000 + packId`, `name=session_<section>_pack_<packId>`, and `memorySummary=spawnReason`. `ObservePack` should call `BuildActorObservation` with a situation containing `session_spawned members=<N> ids=<comma-list>`.

- [ ] **Step 8: Run xrSim tests**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: all tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimPackSpawn.* src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add xrSim session pack registry"
```

---

### Task 2: xrSim Pack Bridge Verbs

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimBridge.cpp`
- Test: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`

**Interfaces:**
- Consumes: pack registry APIs from Task 1.
- Produces bridge verbs:
  - `agent.pack.register <section> <id> [id...]`
  - `agent.pack.list`
  - `agent.pack.observe <pack_id>`
  - `agent.pack.wake <pack_id>`
  - `agent.pack.commands`

- [ ] **Step 1: Write failing bridge tests**

Add:

```cpp
bool TestPackBridgeVerbsRegisterObserveWake()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "pack bridge reset succeeds");

    out = xrSim::HandleBridgeVerb("agent.pack.register", "dog_weak 1010 1011 1012", verbOk);
    ok = Expect(verbOk, "agent.pack.register succeeds") && ok;
    ok = Expect(out.find("PACK#1") != std::string::npos, "agent.pack.register reports pack id") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.observe", "1", verbOk);
    ok = Expect(verbOk, "agent.pack.observe succeeds") && ok;
    ok = Expect(out.find("scope MUTANT_PACK") != std::string::npos, "agent.pack.observe reports mutant scope") && ok;
    ok = Expect(out.find("members=3") != std::string::npos, "agent.pack.observe reports members") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.wake", "1", verbOk);
    ok = Expect(verbOk, "agent.pack.wake succeeds") && ok;
    ok = Expect(out.find("goal=feed_without_losing_alpha") != std::string::npos, "agent.pack.wake reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.commands", "", verbOk);
    ok = Expect(verbOk, "agent.pack.commands succeeds") && ok;
    ok = Expect(out != "empty", "agent.pack.commands reports commands") && ok;
    return ok;
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`

Expected: test compiles, then fails because verbs are unknown.

- [ ] **Step 3: Implement bridge verbs**

In `HandleBridgeVerb`, add `agent.pack.*` cases after `agent.actor.*`. `ai.reset` should also clear session packs via `ResetSessionPacksForTests()` or a production-named reset helper.

- [ ] **Step 4: Run xrSim tests**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/xrSim/xrSimBridge.cpp src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: expose session pack bridge verbs"
```

---

### Task 3: Live Session Spawn Hook

**Files:**
- Modify: `src/xrEngine/IGame_Level.h`
- Modify: `src/xrEngine/AgentBridge.h`
- Modify: `src/xrEngine/AgentBridge.cpp`
- Modify: `src/xrGame/Level.h`
- Modify: `src/xrGame/Level_network_spawn.cpp`

**Interfaces:**
- Consumes: `ParsePackSpawnPayload` and `agent.pack.register`.
- Produces:
  - `IGame_Level::AgentBridgeSpawnObjectNearCurrentEntity(pcstr section, uint32_t ordinal, uint32_t count, float radius, u16& id, xr_string& reason)`
  - bridge verb `agent.pack.spawn <section> <count> [radius_m]`

- [ ] **Step 1: Write compile-time surface first**

Add virtual default to `IGame_Level`:

```cpp
virtual bool AgentBridgeSpawnObjectNearCurrentEntity(
    pcstr section, u32 ordinal, u32 count, float radius, u16& id, xr_string& reason)
{
    reason = "agent bridge spawn unavailable";
    id = u16(-1);
    return false;
}
```

Declare override in `CLevel`.

- [ ] **Step 2: Implement `AgentBridge` spawn orchestration**

Handle `agent.pack.spawn` before routing `agent.*` to xrSim:

```cpp
else if (verb == "agent.pack.spawn")
    result = VerbAgentPackSpawn(payload, ok);
```

`VerbAgentPackSpawn` should:

1. parse with `xrSim::ParsePackSpawnPayload`;
2. require `g_pGameLevel && g_pGameLevel->bReady`;
3. call `AgentBridgeSpawnObjectNearCurrentEntity` for each member;
4. build `agent.pack.register <section> <id...>`;
5. call `xrSim::HandleBridgeVerb("agent.pack.register", registerPayload, ok)`;
6. return `spawned <register-result> ids=<comma-list>`.

- [ ] **Step 3: Implement `CLevel` spawn**

In `Level_network_spawn.cpp`, implement the override:

```cpp
bool CLevel::AgentBridgeSpawnObjectNearCurrentEntity(
    pcstr section, u32 ordinal, u32 count, float radius, u16& id, xr_string& reason)
```

Validation:

- `pSettings && pSettings->section_exist(section)`
- `CurrentEntity() != nullptr`
- `Server != nullptr`
- `ai().get_level_graph() != nullptr`

Placement:

- use current entity position;
- distribute offsets around a circle with angle `2*pi*ordinal/count`;
- keep y at actor y initially;
- use `ai().level_graph().vertex_id(position)` if valid;
- otherwise use `guess_vertex_id(actor_vertex, position)` and `vertex_position`.

Spawn:

- create `CSE_Abstract* abstract = spawn_item(section, spawnPos, levelVertexId, 0xffff, true)`;
- create `ClientID clientID; clientID.set(0xffff);`
- write packet with `abstract->Spawn_Write(packet, TRUE)`;
- destroy the temporary object after `Process_spawn` owns a decoded copy;
- call `Server->Process_spawn(packet, clientID)`;
- return `spawned->ID` on success.

All failure exits set `reason` and return false.

- [ ] **Step 4: Build**

Run: `cmake --build build -j10 --target xr_3da`

Expected: build succeeds.

- [ ] **Step 5: Smoke the parser/bridge tests**

Run: `./bin/arm64/Release/xrSimWorldStateTests`

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/xrEngine/IGame_Level.h src/xrEngine/AgentBridge.* src/xrGame/Level.h src/xrGame/Level_network_spawn.cpp
git commit -m "feat: spawn session mutant packs through bridge"
```

---

### Task 4: Live Verification and Crash Guard Decision

**Files:**
- No source files unless the sound crash reproduces before pack verification.
- Possible docs update: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: `agent.pack.spawn`, `agent.pack.observe`, `agent.pack.wake`, `agent.pack.commands`.
- Produces: live screenshot/log evidence.

- [ ] **Step 1: Relaunch**

Run from the CoC directory with Sonnet env if live provider testing is desired:

```bash
cd "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl"
DYLD_LIBRARY_PATH=/Users/rz/OpenXVibeRay/bin/arm64/Release \
XRAY_AGENT_PROVIDER=anthropic \
XRAY_AGENT_MODEL=claude-sonnet-5 \
XRAY_AGENT_API_KEY="$XRAY_AGENT_API_KEY" \
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge appdata/agent_bridge_pack.sock \
  > /Users/rz/OpenXVibeRay/artifacts/live_sonnet_play/xr_3da_pack_spawn.stdout.log 2>&1 &
```

- [ ] **Step 2: Load game and validate state**

Run:

```bash
python3 tools/agentctl.py "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl/appdata/agent_bridge_pack.sock" \
  cmd "start server(1/single/alife/load) client(localhost)"
python3 tools/agentctl.py "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl/appdata/agent_bridge_pack.sock" state
```

Expected: `scene=game`, `loadscr=0`, `precache=0`.

- [ ] **Step 3: Spawn and wake pack**

Run:

```bash
python3 tools/agentctl.py "$SOCK" agent.pack.spawn "dog_weak 3 8"
python3 tools/agentctl.py "$SOCK" agent.pack.list
python3 tools/agentctl.py "$SOCK" agent.pack.observe 1
python3 tools/agentctl.py "$SOCK" agent.pack.wake 1
python3 tools/agentctl.py "$SOCK" agent.pack.commands
```

If `dog_weak` is invalid, query candidate sections through `pSettings:section_exist("<candidate>")` and retry with the first valid dog section.

- [ ] **Step 4: Screenshot and logs**

Run:

```bash
python3 tools/agentctl.py "$SOCK" shot pack_spawn_loaded
rg -n "FATAL|SCRIPT RUNTIME ERROR|lua_pcall_failed|played_id|invalid agent response|transport_error|agent.pack" \
  /Users/rz/OpenXVibeRay/artifacts/live_sonnet_play/xr_3da_pack_spawn.stdout.log
```

Expected: no fatal or parser errors; pack bridge lines show spawn/register/wake.

- [ ] **Step 5: If sound crash reproduces**

Do not claim live verification. Add a separate focused bugfix task for CoC `sound_theme.script:206` compatibility, with root cause evidence from the log and a minimal test/run that proves the game survives the campfire update.

- [ ] **Step 6: Commit verification docs if changed**

```bash
git add docs/HANDOVER.md
git commit -m "docs: record pack spawn verification notes"
```

Only run this if docs changed.

---

## Self-Review

- Spec coverage: the plan includes session spawning, xrSim registration, pack observe/wake, live verification, and the known sound crash blocker.
- Placeholder scan: no task contains placeholder markers or an unclear future step.
- Type consistency: pack id is `uint32_t`; live object ids are `uint16_t`; spawn count/radius match the spec bounds.
