# LLM Thin-Harness AI Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `b38c943fb`…`589be8ff9` plus fixes. Command-ledger output is not completed live-object authority; historical checkboxes are preserved. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first replayable xrMind thin-harness slice: debug `SquadAgent` and `MutantPackAgent` actors that receive observations, parse LLM-authored `xrsim_actor_intent_v1` packets, validate them, and emit deterministic actuator command streams.

**Architecture:** Keep the current provider/world-state substrate intact and add a new actor-intent layer under `src/xrEngine/xrSim/`. The first slice is engine-debug only: no live CoC NPCs are hijacked yet. The bridge exposes squad/pack observation and wake verbs so the agent bridge can verify "LLM owns decisions, C++ owns embodiment" without risking gameplay regressions.

**Tech Stack:** C++20, existing `xrSimCore` static library, existing assert-style `xrSimWorldStateTests`, existing Unix socket agent bridge, Python `unittest` for bridge scenario dry-runs.

## Global Constraints

- macOS builds use `XRAY_EXCEPTIONS=0`; all parse, validation, provider, and actuator failures return values and never throw for normal failures.
- GL remains the runtime host; no renderer changes are part of this plan.
- LLM owns decisions; C++ owns embodiment.
- C++ may run minimal physical continuity/reflex behavior only when the provider is late, invalid, offline, or over budget.
- No frame may block on the LLM.
- No local load-bearing model.
- No C++ strategic simulation hidden behind the LLM.
- No C++ behavior-tree authoring for stalker or mutant tactics once an LLM-controlled path exists.
- No unvalidated direct memory mutation by model output.
- Existing `WorldState`, provider shell, bridge verbs, and HTTP transport remain valid.

---

## File Structure

- `src/xrEngine/xrSim/xrSimActorIntent.h/.cpp`: parse and format `xrsim_actor_intent_v1`; define actor actions, plans, and parse results.
- `src/xrEngine/xrSim/xrSimActors.h/.cpp`: define debug actor records, scopes, observation packet construction, and actor prompt text.
- `src/xrEngine/xrSim/xrSimActuator.h/.cpp`: validate actor intents and convert accepted actions into deterministic actuator commands; apply `adjust_population` through `WorldState` when requested.
- `src/xrEngine/xrSim/xrSimActorRuntime.h/.cpp`: provider-facing runtime for debug actor wakes, deterministic null actor provider, recorded actor provider, last-command ledger.
- `src/xrEngine/xrSim/xrSimBridge.cpp`: add debug bridge verbs for actor observation, wake, command inspection, and replayable output.
- `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`: add focused tests for parser, observation, actuator, runtime, and bridge verbs.
- `src/xrEngine/CMakeLists.txt`: add the new xrSim source files to `xrSimCore`.
- `tools/ai_thin_harness_smoke.txt`: bridge scenario that exercises debug squad and mutant-pack wakes.
- `tools/tests/test_gl_macos_soak.py`: dry-run assertion that the new smoke script covers the actor verbs.
- `docs/HANDOVER.md`: document the new thin-harness checkpoint and commands.

---

### Task 1: Actor Intent Parser

**Files:**
- Create: `src/xrEngine/xrSim/xrSimActorIntent.h`
- Create: `src/xrEngine/xrSim/xrSimActorIntent.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: plain provider text using the `xrsim_actor_intent_v1` line format.
- Produces:
  - `xrSim::ActorAction { std::string verb, target, arg0, arg1; int32_t amount; }`
  - `xrSim::ActorIntentPlan { bool coast, std::string goal, stance; uint32_t durationMs; std::vector<ActorAction> actions; std::vector<std::string> memories; }`
  - `xrSim::ActorIntentParseResult { bool ok; ActorIntentPlan plan; std::string reason; }`
  - `xrSim::ParseActorIntentPlan(const std::string& text)`
  - `xrSim::FormatActorIntentPlan(const ActorIntentPlan& plan)`

- [ ] **Step 1: Write the failing parser tests**

Add this include near the other xrSim includes in `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`:

```cpp
#include "xrSim/xrSimActorIntent.h"
```

Add these test functions before `TestNullAgentWakeAppliesDeterministicIntent()`:

```cpp
bool TestActorIntentParserAcceptsSquadPlan()
{
    const char* text =
        "xrsim_actor_intent_v1\n"
        "goal survive_and_delay_player\n"
        "stance cautious\n"
        "duration_ms 3500\n"
        "action move_to cover_node_12\n"
        "action fire_pattern target_player burst_short\n"
        "action vocalize ally_3 fall_back\n"
        "memory player_used_grenade_aggressively\n"
        "end\n";

    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(text);
    bool ok = Expect(parsed.ok, "actor intent parser accepts valid squad plan");
    ok = Expect(parsed.plan.goal == "survive_and_delay_player", "actor intent parser keeps goal") && ok;
    ok = Expect(parsed.plan.stance == "cautious", "actor intent parser keeps stance") && ok;
    ok = Expect(parsed.plan.durationMs == 3500, "actor intent parser keeps duration") && ok;
    ok = Expect(parsed.plan.actions.size() == 3, "actor intent parser keeps actions") && ok;
    if (parsed.plan.actions.size() == 3)
    {
        ok = Expect(parsed.plan.actions[0].verb == "move_to", "actor intent parser keeps first action verb") && ok;
        ok = Expect(parsed.plan.actions[0].target == "cover_node_12", "actor intent parser keeps first action target") && ok;
        ok = Expect(parsed.plan.actions[1].arg0 == "burst_short", "actor intent parser keeps action argument") && ok;
    }
    ok = Expect(parsed.plan.memories.size() == 1, "actor intent parser keeps memory records") && ok;
    return ok;
}

bool TestActorIntentParserAcceptsCoast()
{
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "coast\n"
        "end\n");

    bool ok = Expect(parsed.ok, "actor intent parser accepts coast");
    ok = Expect(parsed.plan.coast, "actor intent parser marks coast") && ok;
    ok = Expect(parsed.plan.actions.empty(), "actor intent coast has no actions") && ok;
    return ok;
}

bool TestActorIntentParserRejectsBadVersionAsValue()
{
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan("xrsim_actor_intent_v2\nend\n");
    bool ok = Expect(!parsed.ok, "actor intent parser rejects bad version as value");
    ok = Expect(parsed.reason.find("version") != std::string::npos, "actor intent parser explains bad version") && ok;
    return ok;
}
```

Add these calls in `main()` before `TestNullAgentWakeAppliesDeterministicIntent()`:

```cpp
    ok = TestActorIntentParserAcceptsSquadPlan() && ok;
    ok = TestActorIntentParserAcceptsCoast() && ok;
    ok = TestActorIntentParserRejectsBadVersionAsValue() && ok;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: FAIL with an include error for `xrSim/xrSimActorIntent.h`.

- [ ] **Step 3: Add the parser interface**

Create `src/xrEngine/xrSim/xrSimActorIntent.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ActorAction
{
    std::string verb;
    std::string target;
    std::string arg0;
    std::string arg1;
    int32_t amount = 0;
};

struct ActorIntentPlan
{
    bool coast = false;
    std::string goal;
    std::string stance;
    uint32_t durationMs = 0;
    std::vector<ActorAction> actions;
    std::vector<std::string> memories;
};

struct ActorIntentParseResult
{
    bool ok = false;
    ActorIntentPlan plan;
    std::string reason;
};

ActorIntentParseResult ParseActorIntentPlan(const std::string& text);
std::string FormatActorIntentPlan(const ActorIntentPlan& plan);
} // namespace xrSim
```

- [ ] **Step 4: Add the parser implementation**

Create `src/xrEngine/xrSim/xrSimActorIntent.cpp`:

```cpp
#include "xrSim/xrSimActorIntent.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace xrSim
{
namespace
{
ActorIntentParseResult FailActorIntent(const std::string& reason)
{
    ActorIntentParseResult result;
    result.reason = reason;
    return result;
}

bool ParseUint32Token(const std::string& text, uint32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    value = uint32_t(parsed);
    return true;
}

bool ParseInt32Token(const std::string& text, int32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max())
        return false;
    value = int32_t(parsed);
    return true;
}
} // namespace

ActorIntentParseResult ParseActorIntentPlan(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "xrsim_actor_intent_v1")
        return FailActorIntent("unsupported actor intent version");

    ActorIntentParseResult result;
    result.ok = true;

    bool sawEnd = false;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        if (line == "end")
        {
            sawEnd = true;
            break;
        }
        if (line == "coast")
        {
            if (!result.plan.actions.empty())
                return FailActorIntent("coast cannot be mixed with actions");
            result.plan.coast = true;
            continue;
        }

        std::istringstream record(line);
        std::string key;
        record >> key;
        if (key == "goal")
        {
            if (!(record >> result.plan.goal))
                return FailActorIntent("invalid goal record");
        }
        else if (key == "stance")
        {
            if (!(record >> result.plan.stance))
                return FailActorIntent("invalid stance record");
        }
        else if (key == "duration_ms")
        {
            std::string value;
            if (!(record >> value) || !ParseUint32Token(value, result.plan.durationMs))
                return FailActorIntent("invalid duration_ms record");
        }
        else if (key == "action")
        {
            if (result.plan.coast)
                return FailActorIntent("coast cannot be mixed with actions");
            ActorAction action;
            std::string amountText;
            record >> action.verb >> action.target >> action.arg0 >> action.arg1 >> amountText;
            if (action.verb.empty())
                return FailActorIntent("invalid action record");
            if (!amountText.empty() && !ParseInt32Token(amountText, action.amount))
                return FailActorIntent("invalid action amount");
            result.plan.actions.push_back(action);
        }
        else if (key == "memory")
        {
            std::string memory;
            if (!(record >> memory))
                return FailActorIntent("invalid memory record");
            result.plan.memories.push_back(memory);
        }
        else
        {
            return FailActorIntent("unknown actor intent record: " + key);
        }
    }

    if (!sawEnd)
        return FailActorIntent("missing actor intent end");
    if (!result.plan.coast && result.plan.actions.empty())
        return FailActorIntent("actor intent contained no actions");
    return result;
}

std::string FormatActorIntentPlan(const ActorIntentPlan& plan)
{
    std::ostringstream out;
    out << "xrsim_actor_intent_v1\n";
    if (plan.coast)
    {
        out << "coast\n";
    }
    else
    {
        if (!plan.goal.empty())
            out << "goal " << plan.goal << "\n";
        if (!plan.stance.empty())
            out << "stance " << plan.stance << "\n";
        if (plan.durationMs != 0)
            out << "duration_ms " << plan.durationMs << "\n";
        for (const ActorAction& action : plan.actions)
        {
            out << "action " << action.verb;
            if (!action.target.empty())
                out << " " << action.target;
            if (!action.arg0.empty())
                out << " " << action.arg0;
            if (!action.arg1.empty())
                out << " " << action.arg1;
            if (action.amount != 0)
                out << " " << action.amount;
            out << "\n";
        }
        for (const std::string& memory : plan.memories)
            out << "memory " << memory << "\n";
    }
    out << "end\n";
    return out.str();
}
} // namespace xrSim
```

- [ ] **Step 5: Add the new files to CMake**

In `src/xrEngine/CMakeLists.txt`, add the new files to `xrSimCore`:

```cmake
    xrSim/xrSimActorIntent.cpp
    xrSim/xrSimActorIntent.h
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: PASS.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimActorIntent.* src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add xrSim actor intent parser"
```

### Task 2: Debug Actor Records and Observations

**Files:**
- Create: `src/xrEngine/xrSim/xrSimActors.h`
- Create: `src/xrEngine/xrSim/xrSimActors.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `xrSim::WorldState`, `xrSim::ActorIntentPlan`.
- Produces:
  - `enum class ActorAgentScope { Zone, Squad, MutantPack }`
  - `xrSim::ActorAgentRecord`
  - `xrSim::ActorScopeName(ActorAgentScope scope)`
  - `xrSim::MakeDebugSquadAgent()`
  - `xrSim::MakeDebugMutantPackAgent()`
  - `xrSim::BuildActorObservation(const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)`
  - `xrSim::BuildActorWakePrompt(const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)`

- [ ] **Step 1: Write the failing observation tests**

Add this include:

```cpp
#include "xrSim/xrSimActors.h"
```

Add these test functions:

```cpp
bool TestDebugSquadObservationIsExperiential()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "squad observation setup accepts population");

    const xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    const std::string observation = xrSim::BuildActorObservation(squad, state, "player_visible medium_range");

    ok = Expect(observation.find("scope SQUAD") != std::string::npos, "squad observation records scope") && ok;
    ok = Expect(observation.find("memory_summary") != std::string::npos, "squad observation records memory") && ok;
    ok = Expect(observation.find("situation player_visible medium_range") != std::string::npos,
        "squad observation records situation") && ok;
    ok = Expect(observation.find("legal_tools move_to fire_pattern vocalize retreat author_memory") != std::string::npos,
        "squad observation records legal tools") && ok;
    ok = Expect(observation.find(state.Digest()) != std::string::npos, "squad observation includes world digest") && ok;
    return ok;
}

bool TestDebugMutantPackObservationUsesPackTools()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    const xrSim::ActorAgentRecord pack = xrSim::MakeDebugMutantPackAgent();
    const std::string prompt = xrSim::BuildActorWakePrompt(pack, state, "heard_gunfire");

    bool ok = Expect(prompt.find("xrsim_actor_wake_v1") == 0, "actor wake prompt has version");
    ok = Expect(prompt.find("scope MUTANT_PACK") != std::string::npos, "mutant prompt records scope") && ok;
    ok = Expect(prompt.find("legal_tools stalk ambush retreat author_memory") != std::string::npos,
        "mutant prompt records pack legal tools") && ok;
    ok = Expect(prompt.find("return xrsim_actor_intent_v1") != std::string::npos,
        "mutant prompt requests actor intent response") && ok;
    return ok;
}
```

Add calls in `main()` after the Task 1 parser tests:

```cpp
    ok = TestDebugSquadObservationIsExperiential() && ok;
    ok = TestDebugMutantPackObservationUsesPackTools() && ok;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: FAIL with an include error for `xrSim/xrSimActors.h`.

- [ ] **Step 3: Add actor record interface**

Create `src/xrEngine/xrSim/xrSimActors.h`:

```cpp
#pragma once

#include "xrSim/xrSimActorIntent.h"
#include "xrSim/xrSimWorldState.h"

#include <cstdint>
#include <string>

namespace xrSim
{
enum class ActorAgentScope
{
    Zone,
    Squad,
    MutantPack,
};

struct ActorAgentRecord
{
    uint32_t agentId = 0;
    ActorAgentScope scope = ActorAgentScope::Zone;
    std::string name;
    std::string memorySummary;
    ActorIntentPlan lastIntent;
};

const char* ActorScopeName(ActorAgentScope scope);
ActorAgentRecord MakeDebugSquadAgent();
ActorAgentRecord MakeDebugMutantPackAgent();
std::string BuildActorObservation(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation);
std::string BuildActorWakePrompt(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation);
} // namespace xrSim
```

- [ ] **Step 4: Add actor observation implementation**

Create `src/xrEngine/xrSim/xrSimActors.cpp`:

```cpp
#include "xrSim/xrSimActors.h"

#include <sstream>

namespace xrSim
{
namespace
{
const char* LegalToolsForScope(ActorAgentScope scope)
{
    switch (scope)
    {
    case ActorAgentScope::Squad: return "move_to fire_pattern vocalize retreat author_memory";
    case ActorAgentScope::MutantPack: return "stalk ambush retreat author_memory";
    case ActorAgentScope::Zone: return "author_memory";
    }
    return "author_memory";
}
} // namespace

const char* ActorScopeName(ActorAgentScope scope)
{
    switch (scope)
    {
    case ActorAgentScope::Zone: return "ZONE";
    case ActorAgentScope::Squad: return "SQUAD";
    case ActorAgentScope::MutantPack: return "MUTANT_PACK";
    }
    return "UNKNOWN";
}

ActorAgentRecord MakeDebugSquadAgent()
{
    ActorAgentRecord actor;
    actor.agentId = 101;
    actor.scope = ActorAgentScope::Squad;
    actor.name = "debug_stalker_squad";
    actor.memorySummary = "met_player_recently cautious_about_grenades";
    return actor;
}

ActorAgentRecord MakeDebugMutantPackAgent()
{
    ActorAgentRecord actor;
    actor.agentId = 201;
    actor.scope = ActorAgentScope::MutantPack;
    actor.name = "debug_blind_dog_pack";
    actor.memorySummary = "hungry_guarding_garbage_den";
    return actor;
}

std::string BuildActorObservation(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)
{
    std::ostringstream out;
    out << "agent_id " << actor.agentId << "\n";
    out << "scope " << ActorScopeName(actor.scope) << "\n";
    out << "name " << actor.name << "\n";
    out << "memory_summary " << actor.memorySummary << "\n";
    out << "situation " << situation << "\n";
    out << "world_digest " << world.Digest() << "\n";
    out << "legal_tools " << LegalToolsForScope(actor.scope) << "\n";
    return out.str();
}

std::string BuildActorWakePrompt(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)
{
    std::ostringstream out;
    out << "xrsim_actor_wake_v1\n";
    out << BuildActorObservation(actor, world, situation);
    out << "rules\n";
    out << "return xrsim_actor_intent_v1\n";
    out << "llm_owns_decisions cxx_owns_embodiment\n";
    out << "do_not_emit_unlisted_tools\n";
    out << "end\n";
    return out.str();
}
} // namespace xrSim
```

- [ ] **Step 5: Add the new files to CMake**

In `src/xrEngine/CMakeLists.txt`, add:

```cmake
    xrSim/xrSimActors.cpp
    xrSim/xrSimActors.h
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: PASS.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimActors.* src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add xrSim debug actor observations"
```

### Task 3: Actuator Command Executor

**Files:**
- Create: `src/xrEngine/xrSim/xrSimActuator.h`
- Create: `src/xrEngine/xrSim/xrSimActuator.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `xrSim::ActorAgentRecord`, `xrSim::ActorIntentPlan`, `xrSim::WorldState`.
- Produces:
  - `xrSim::ActuatorCommand { std::string verb, target, arg0, arg1; }`
  - `xrSim::ActuatorResult { bool ok, coast; int32_t appliedDelta; std::string reason; std::vector<ActuatorCommand> commands; }`
  - `xrSim::ExecuteActorIntent(WorldState& world, const ActorAgentRecord& actor, const ActorIntentPlan& plan, uint32_t gameDay)`
  - `xrSim::FormatActuatorCommands(const std::vector<ActuatorCommand>& commands)`

- [ ] **Step 1: Write the failing actuator tests**

Add this include:

```cpp
#include "xrSim/xrSimActuator.h"
```

Add these test functions:

```cpp
bool TestActuatorConvertsSquadIntentToCommands()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorIntentPlan plan;
    plan.goal = "survive_and_delay_player";
    plan.stance = "cautious";
    plan.durationMs = 3500;
    plan.actions.push_back(xrSim::ActorAction{ "move_to", "cover_node_12", "", "", 0 });
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugSquadAgent(), plan, 2);

    bool ok = Expect(result.ok, "actuator accepts squad intent");
    ok = Expect(result.commands.size() == 3, "actuator emits stance plus two commands") && ok;
    if (result.commands.size() == 3)
    {
        ok = Expect(result.commands[0].verb == "set_stance", "actuator emits stance command") && ok;
        ok = Expect(result.commands[1].verb == "move_to", "actuator emits move command") && ok;
        ok = Expect(result.commands[2].arg0 == "burst_short", "actuator keeps fire pattern argument") && ok;
    }
    return ok;
}

bool TestActuatorRejectsIllegalPackVerbAsValue()
{
    xrSim::WorldState state;
    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugMutantPackAgent(), plan, 0);

    bool ok = Expect(!result.ok, "actuator rejects illegal pack verb as value");
    ok = Expect(result.reason.find("illegal action") != std::string::npos, "actuator explains illegal pack verb") && ok;
    return ok;
}

bool TestActuatorAppliesPopulationToolThroughWorldState()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "actuator population setup accepts population");

    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "adjust_population", "debug_region", "blind_dog", "", 500 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugMutantPackAgent(), plan, 4);

    ok = Expect(result.ok, "actuator accepts population tool") && ok;
    ok = Expect(result.appliedDelta == 10, "actuator applies clamped population delta") && ok;
    ok = Expect(state.Population(region, species) == 60, "actuator mutates world state through validator") && ok;
    return ok;
}
```

Add calls in `main()` after the Task 2 tests:

```cpp
    ok = TestActuatorConvertsSquadIntentToCommands() && ok;
    ok = TestActuatorRejectsIllegalPackVerbAsValue() && ok;
    ok = TestActuatorAppliesPopulationToolThroughWorldState() && ok;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: FAIL with an include error for `xrSim/xrSimActuator.h`.

- [ ] **Step 3: Add actuator interface**

Create `src/xrEngine/xrSim/xrSimActuator.h`:

```cpp
#pragma once

#include "xrSim/xrSimActors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ActuatorCommand
{
    std::string verb;
    std::string target;
    std::string arg0;
    std::string arg1;
};

struct ActuatorResult
{
    bool ok = false;
    bool coast = false;
    int32_t appliedDelta = 0;
    std::string reason;
    std::vector<ActuatorCommand> commands;
};

ActuatorResult ExecuteActorIntent(
    WorldState& world, const ActorAgentRecord& actor, const ActorIntentPlan& plan, uint32_t gameDay);
std::string FormatActuatorCommands(const std::vector<ActuatorCommand>& commands);
} // namespace xrSim
```

- [ ] **Step 4: Add actuator implementation**

Create `src/xrEngine/xrSim/xrSimActuator.cpp`:

```cpp
#include "xrSim/xrSimActuator.h"

#include <sstream>

namespace xrSim
{
namespace
{
bool IsLegalAction(ActorAgentScope scope, const std::string& verb)
{
    if (verb == "author_memory" || verb == "adjust_population")
        return true;
    if (scope == ActorAgentScope::Squad)
        return verb == "move_to" || verb == "fire_pattern" || verb == "vocalize" || verb == "retreat";
    if (scope == ActorAgentScope::MutantPack)
        return verb == "stalk" || verb == "ambush" || verb == "retreat";
    return false;
}

ActuatorResult FailActuator(const std::string& reason)
{
    ActuatorResult result;
    result.reason = reason;
    return result;
}
} // namespace

ActuatorResult ExecuteActorIntent(
    WorldState& world, const ActorAgentRecord& actor, const ActorIntentPlan& plan, uint32_t gameDay)
{
    ActuatorResult result;
    if (plan.coast)
    {
        result.ok = true;
        result.coast = true;
        result.reason = "coast";
        return result;
    }

    if (!plan.stance.empty())
        result.commands.push_back(ActuatorCommand{ "set_stance", plan.stance, "", "" });

    for (const ActorAction& action : plan.actions)
    {
        if (!IsLegalAction(actor.scope, action.verb))
            return FailActuator("illegal action for scope: " + action.verb);

        if (action.verb == "adjust_population")
        {
            const Handle region = world.FindRegionByName(action.target);
            if (!region.IsValid())
                return FailActuator("unknown region: " + action.target);
            const Handle species = world.FindSpeciesByName(action.arg0);
            if (!species.IsValid())
                return FailActuator("unknown species: " + action.arg0);
            const uint32_t seq = uint32_t(world.ToolLog().size() + 1);
            const Result applied = world.ApplyAdjustPopulation(seq, region, species, action.amount, gameDay);
            if (!applied.ok)
                return FailActuator(applied.reason);
            result.appliedDelta += applied.appliedDelta;
            continue;
        }

        result.commands.push_back(ActuatorCommand{ action.verb, action.target, action.arg0, action.arg1 });
    }

    result.ok = true;
    return result;
}

std::string FormatActuatorCommands(const std::vector<ActuatorCommand>& commands)
{
    if (commands.empty())
        return "empty";

    std::ostringstream out;
    for (size_t i = 0; i < commands.size(); ++i)
    {
        const ActuatorCommand& command = commands[i];
        if (i != 0)
            out << " | ";
        out << command.verb;
        if (!command.target.empty())
            out << " " << command.target;
        if (!command.arg0.empty())
            out << " " << command.arg0;
        if (!command.arg1.empty())
            out << " " << command.arg1;
    }
    return out.str();
}
} // namespace xrSim
```

- [ ] **Step 5: Add the new files to CMake**

In `src/xrEngine/CMakeLists.txt`, add:

```cmake
    xrSim/xrSimActuator.cpp
    xrSim/xrSimActuator.h
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: PASS.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimActuator.* src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add xrSim actor actuator executor"
```

### Task 4: Thin Actor Runtime and Deterministic Providers

**Files:**
- Create: `src/xrEngine/xrSim/xrSimActorRuntime.h`
- Create: `src/xrEngine/xrSim/xrSimActorRuntime.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `ActorAgentRecord`, `BuildActorWakePrompt`, `ParseActorIntentPlan`, `ExecuteActorIntent`.
- Produces:
  - `xrSim::ActorWakeContext`
  - `xrSim::ActorProviderResult`
  - `class IActorIntentProvider`
  - `class DeterministicActorIntentProvider`
  - `class RecordedActorIntentProvider`
  - `class ActorRuntime`
  - `ActorRuntime::Wake(WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay)`

- [ ] **Step 1: Write the failing runtime tests**

Add this include:

```cpp
#include "xrSim/xrSimActorRuntime.h"
```

Add these test functions:

```cpp
bool TestActorRuntimeWakesDebugSquad()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    xrSim::DeterministicActorIntentProvider provider;
    xrSim::ActorRuntime runtime;
    runtime.SetProvider(&provider);

    const xrSim::ActuatorResult result = runtime.Wake(state, squad, "player_visible medium_range", 1);

    bool ok = Expect(result.ok, "actor runtime wakes debug squad");
    ok = Expect(runtime.WakeCount() == 1, "actor runtime records wake count") && ok;
    ok = Expect(runtime.LastCommands().size() >= 2, "actor runtime stores actuator commands") && ok;
    ok = Expect(squad.lastIntent.goal == "survive_and_delay_player", "actor runtime stores last squad intent") && ok;
    return ok;
}

bool TestActorRuntimeWakesDebugMutantPack()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "mutant runtime setup accepts population");

    xrSim::ActorAgentRecord pack = xrSim::MakeDebugMutantPackAgent();
    xrSim::DeterministicActorIntentProvider provider;
    xrSim::ActorRuntime runtime;
    runtime.SetProvider(&provider);

    const xrSim::ActuatorResult result = runtime.Wake(state, pack, "heard_gunfire", 1);

    ok = Expect(result.ok, "actor runtime wakes debug mutant pack") && ok;
    ok = Expect(pack.lastIntent.goal == "feed_without_losing_alpha", "actor runtime stores pack goal") && ok;
    ok = Expect(runtime.LastCommands().size() >= 1, "actor runtime stores pack commands") && ok;
    return ok;
}

bool TestRecordedActorProviderExhaustionFailsAsValue()
{
    std::vector<std::string> script;
    xrSim::RecordedActorIntentProvider provider(script);
    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugSquadAgent();

    const xrSim::ActorProviderResult result = provider.Wake(context);
    bool ok = Expect(!result.ok, "recorded actor provider exhaustion fails as value");
    ok = Expect(result.error.find("exhausted") != std::string::npos, "recorded actor provider explains exhaustion") && ok;
    return ok;
}
```

Add calls in `main()` after Task 3 tests:

```cpp
    ok = TestActorRuntimeWakesDebugSquad() && ok;
    ok = TestActorRuntimeWakesDebugMutantPack() && ok;
    ok = TestRecordedActorProviderExhaustionFailsAsValue() && ok;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: FAIL with an include error for `xrSim/xrSimActorRuntime.h`.

- [ ] **Step 3: Add runtime interface**

Create `src/xrEngine/xrSim/xrSimActorRuntime.h`:

```cpp
#pragma once

#include "xrSim/xrSimActuator.h"

namespace xrSim
{
struct ActorWakeContext
{
    ActorAgentRecord actor;
    std::string observation;
    std::string prompt;
};

struct ActorProviderResult
{
    bool ok = false;
    bool coast = false;
    std::string provider;
    std::string model;
    std::string error;
    ActorIntentPlan plan;
};

class IActorIntentProvider
{
public:
    virtual ~IActorIntentProvider() = default;
    virtual ActorProviderResult Wake(const ActorWakeContext& context) = 0;
};

class DeterministicActorIntentProvider : public IActorIntentProvider
{
public:
    ActorProviderResult Wake(const ActorWakeContext& context) override;
};

class RecordedActorIntentProvider : public IActorIntentProvider
{
public:
    explicit RecordedActorIntentProvider(const std::vector<std::string>& script);
    ActorProviderResult Wake(const ActorWakeContext& context) override;

private:
    std::vector<std::string> m_script;
    size_t m_cursor = 0;
};

class ActorRuntime
{
public:
    void SetProvider(IActorIntentProvider* provider);
    ActuatorResult Wake(WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay);
    uint32_t WakeCount() const;
    const std::vector<ActuatorCommand>& LastCommands() const;
    const ActorProviderResult& LastProviderResult() const;

private:
    IActorIntentProvider* m_provider = nullptr;
    ActorProviderResult m_lastProviderResult;
    std::vector<ActuatorCommand> m_lastCommands;
    uint32_t m_wakeCount = 0;
};
} // namespace xrSim
```

- [ ] **Step 4: Add runtime implementation**

Create `src/xrEngine/xrSim/xrSimActorRuntime.cpp`:

```cpp
#include "xrSim/xrSimActorRuntime.h"

namespace xrSim
{
namespace
{
ActorProviderResult ParseProviderText(const std::string& provider, const std::string& model, const std::string& text)
{
    const ActorIntentParseResult parsed = ParseActorIntentPlan(text);
    ActorProviderResult result;
    result.ok = parsed.ok;
    result.provider = provider;
    result.model = model;
    result.plan = parsed.plan;
    result.coast = parsed.ok && parsed.plan.coast;
    result.error = parsed.reason;
    return result;
}
} // namespace

ActorProviderResult DeterministicActorIntentProvider::Wake(const ActorWakeContext& context)
{
    if (context.actor.scope == ActorAgentScope::MutantPack)
    {
        return ParseProviderText("deterministic-actor", "fixture",
            "xrsim_actor_intent_v1\n"
            "goal feed_without_losing_alpha\n"
            "stance hungry_but_cautious\n"
            "duration_ms 2500\n"
            "action stalk target_player crescent\n"
            "memory heard_gunfire_near_den\n"
            "end\n");
    }

    return ParseProviderText("deterministic-actor", "fixture",
        "xrsim_actor_intent_v1\n"
        "goal survive_and_delay_player\n"
        "stance cautious\n"
        "duration_ms 3500\n"
        "action move_to cover_node_12\n"
        "action fire_pattern target_player burst_short\n"
        "memory player_used_grenade_aggressively\n"
        "end\n");
}

RecordedActorIntentProvider::RecordedActorIntentProvider(const std::vector<std::string>& script) : m_script(script) {}

ActorProviderResult RecordedActorIntentProvider::Wake(const ActorWakeContext& context)
{
    (void)context;
    if (m_cursor >= m_script.size())
    {
        ActorProviderResult result;
        result.ok = false;
        result.provider = "recorded-actor";
        result.model = "fixture";
        result.error = "recorded actor provider exhausted";
        return result;
    }
    return ParseProviderText("recorded-actor", "fixture", m_script[m_cursor++]);
}

void ActorRuntime::SetProvider(IActorIntentProvider* provider) { m_provider = provider; }

ActuatorResult ActorRuntime::Wake(
    WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay)
{
    ActorWakeContext context;
    context.actor = actor;
    context.observation = BuildActorObservation(actor, world, situation);
    context.prompt = BuildActorWakePrompt(actor, world, situation);

    DeterministicActorIntentProvider defaultProvider;
    IActorIntentProvider* provider = m_provider ? m_provider : &defaultProvider;
    m_lastProviderResult = provider->Wake(context);
    if (!m_lastProviderResult.ok)
    {
        ActuatorResult failed;
        failed.reason = m_lastProviderResult.error.empty() ? "actor provider failed" : m_lastProviderResult.error;
        return failed;
    }

    actor.lastIntent = m_lastProviderResult.plan;
    ActuatorResult executed = ExecuteActorIntent(world, actor, m_lastProviderResult.plan, gameDay);
    if (executed.ok)
    {
        m_lastCommands = executed.commands;
        ++m_wakeCount;
    }
    return executed;
}

uint32_t ActorRuntime::WakeCount() const { return m_wakeCount; }

const std::vector<ActuatorCommand>& ActorRuntime::LastCommands() const { return m_lastCommands; }

const ActorProviderResult& ActorRuntime::LastProviderResult() const { return m_lastProviderResult; }
} // namespace xrSim
```

- [ ] **Step 5: Add the new files to CMake**

In `src/xrEngine/CMakeLists.txt`, add:

```cmake
    xrSim/xrSimActorRuntime.cpp
    xrSim/xrSimActorRuntime.h
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: PASS.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimActorRuntime.* src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add xrSim actor runtime"
```

### Task 5: Bridge Verbs for Squad and Mutant Pack

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimBridge.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`

**Interfaces:**
- Consumes: `ActorRuntime`, `MakeDebugSquadAgent`, `MakeDebugMutantPackAgent`, `BuildActorObservation`, `FormatActuatorCommands`.
- Produces bridge verbs:
  - `agent.actor.list`
  - `agent.actor.observe squad`
  - `agent.actor.observe mutant_pack`
  - `agent.actor.wake squad`
  - `agent.actor.wake mutant_pack`
  - `agent.actor.commands`

- [ ] **Step 1: Write the failing bridge tests**

Add this test function:

```cpp
bool TestActorBridgeVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "actor bridge setup reset succeeds");

    out = xrSim::HandleBridgeVerb("agent.actor.list", "", verbOk);
    ok = Expect(verbOk, "agent.actor.list succeeds") && ok;
    ok = Expect(out.find("SQUAD#101") != std::string::npos, "agent.actor.list reports squad") && ok;
    ok = Expect(out.find("MUTANT_PACK#201") != std::string::npos, "agent.actor.list reports mutant pack") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.observe", "squad", verbOk);
    ok = Expect(verbOk, "agent.actor.observe squad succeeds") && ok;
    ok = Expect(out.find("scope SQUAD") != std::string::npos, "agent.actor.observe squad reports scope") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.wake", "squad", verbOk);
    ok = Expect(verbOk, "agent.actor.wake squad succeeds") && ok;
    ok = Expect(out.find("goal=survive_and_delay_player") != std::string::npos,
        "agent.actor.wake squad reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.wake", "mutant_pack", verbOk);
    ok = Expect(verbOk, "agent.actor.wake mutant_pack succeeds") && ok;
    ok = Expect(out.find("goal=feed_without_losing_alpha") != std::string::npos,
        "agent.actor.wake mutant pack reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.commands", "", verbOk);
    ok = Expect(verbOk, "agent.actor.commands succeeds") && ok;
    ok = Expect(out != "empty", "agent.actor.commands reports actuator stream") && ok;
    return ok;
}
```

Add this call in `main()` after `TestAgentBridgeAliases()`:

```cpp
    ok = TestActorBridgeVerbs() && ok;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: FAIL with `unknown ai verb: agent.actor.list`.

- [ ] **Step 3: Add bridge state and helpers**

In `src/xrEngine/xrSim/xrSimBridge.cpp`, add this include:

```cpp
#include "xrSim/xrSimActorRuntime.h"
```

Add these globals near `g_nullAgent`:

```cpp
ActorRuntime g_actorRuntime;
ActorAgentRecord g_debugSquad;
ActorAgentRecord g_debugMutantPack;
```

In `ResetDebugWorld()`, after `g_nullAgent = NullAgentRuntime{};`, add:

```cpp
    g_actorRuntime = ActorRuntime{};
    g_debugSquad = MakeDebugSquadAgent();
    g_debugMutantPack = MakeDebugMutantPackAgent();
```

Add these helper functions before `HandleBridgeVerb()`:

```cpp
ActorAgentRecord* FindDebugActor(const std::string& name)
{
    EnsureDebugWorld();
    if (name == "squad")
        return &g_debugSquad;
    if (name == "mutant_pack")
        return &g_debugMutantPack;
    return nullptr;
}

std::string WakeDebugActor(const std::string& payload, bool& ok)
{
    ActorAgentRecord* actor = FindDebugActor(payload);
    if (!actor)
    {
        ok = false;
        return "unknown actor: " + payload;
    }

    const std::string situation = payload == "mutant_pack" ? "heard_gunfire" : "player_visible medium_range";
    const ActuatorResult result = g_actorRuntime.Wake(g_debugWorld, *actor, situation, 0);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    return std::string("actor wake scope=") + ActorScopeName(actor->scope) + " id=" + std::to_string(actor->agentId) +
        " goal=" + actor->lastIntent.goal + " commands=" + std::to_string(result.commands.size()) +
        " applied_delta=" + std::to_string(result.appliedDelta);
}
```

- [ ] **Step 4: Add bridge verb handlers**

In `HandleBridgeVerb()`, before `agent.tree`, add:

```cpp
    if (verb == "agent.actor.list")
    {
        EnsureDebugWorld();
        ok = true;
        return "SQUAD#101 debug_stalker_squad | MUTANT_PACK#201 debug_blind_dog_pack";
    }

    if (verb == "agent.actor.observe")
    {
        ActorAgentRecord* actor = FindDebugActor(payload);
        if (!actor)
        {
            ok = false;
            return "unknown actor: " + payload;
        }
        const std::string situation = payload == "mutant_pack" ? "heard_gunfire" : "player_visible medium_range";
        ok = true;
        return BuildActorObservation(*actor, g_debugWorld, situation);
    }

    if (verb == "agent.actor.wake")
        return WakeDebugActor(payload, ok);

    if (verb == "agent.actor.commands")
    {
        EnsureDebugWorld();
        ok = true;
        return FormatActuatorCommands(g_actorRuntime.LastCommands());
    }
```

- [ ] **Step 5: Run test to verify it passes**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: PASS.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/xrEngine/xrSim/xrSimBridge.cpp src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: expose xrSim actor bridge verbs"
```

### Task 6: Bridge Smoke Script and Handover

**Files:**
- Create: `tools/ai_thin_harness_smoke.txt`
- Modify: `tools/tests/test_gl_macos_soak.py`
- Modify: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: bridge verbs from Task 5.
- Produces: a dry-runnable smoke scenario documenting and exercising the thin-harness debug layer.

- [ ] **Step 1: Write the failing Python dry-run test**

In `tools/tests/test_gl_macos_soak.py`, add this test method to `DryRunCliTests`:

```python
    def test_ai_thin_harness_smoke_script_exercises_actor_verbs(self):
        scenario = Path("tools/ai_thin_harness_smoke.txt")
        steps = parse_scenario_lines(scenario.read_text().splitlines())

        commands = [step.raw for step in steps if step.kind == "bridge"]
        self.assertIn("ai.reset", commands)
        self.assertIn("agent.actor.list", commands)
        self.assertIn("agent.actor.observe squad", commands)
        self.assertIn("agent.actor.wake squad", commands)
        self.assertIn("agent.actor.observe mutant_pack", commands)
        self.assertIn("agent.actor.wake mutant_pack", commands)
        self.assertIn("agent.actor.commands", commands)
        self.assertIn("agent.provider live", commands)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak.DryRunCliTests.test_ai_thin_harness_smoke_script_exercises_actor_verbs -v
```

Expected: FAIL because `tools/ai_thin_harness_smoke.txt` does not exist.

- [ ] **Step 3: Add the smoke script**

Create `tools/ai_thin_harness_smoke.txt`:

```text
# Thin-harness debug AI smoke: proves LLM-style actor intents are reachable over the bridge.
hello
ai.reset
agent.provider live
agent.actor.list
agent.actor.observe squad
agent.actor.wake squad
agent.actor.commands
agent.actor.observe mutant_pack
agent.actor.wake mutant_pack
agent.actor.commands
ai.snapshot
ai.log
cmd quit
```

- [ ] **Step 4: Update handover**

In `docs/HANDOVER.md`, under the AI checkpoint section, add:

```markdown
Thin-harness actor checkpoint:
- `src/xrEngine/xrSim/` now includes debug `SquadAgent` and `MutantPackAgent` records that parse `xrsim_actor_intent_v1`, validate actor actions, and emit deterministic actuator command streams.
- Bridge verbs:
  - `agent.actor.list`
  - `agent.actor.observe squad`
  - `agent.actor.wake squad`
  - `agent.actor.observe mutant_pack`
  - `agent.actor.wake mutant_pack`
  - `agent.actor.commands`
- Smoke: `python3 tools/gl_macos_soak.py --scenario tools/ai_thin_harness_smoke.txt --artifacts artifacts/ai_thin_harness_smoke`
```

- [ ] **Step 5: Run verification**

Run:

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
cmake --build build -j10 --target xrSimWorldStateTests
./bin/arm64/Release/xrSimWorldStateTests
cmake --build build -j10 --target xr_3da
```

Expected: PASS.

- [ ] **Step 6: Commit**

Run:

```bash
git add tools/ai_thin_harness_smoke.txt tools/tests/test_gl_macos_soak.py docs/HANDOVER.md
git commit -m "test: add thin-harness actor smoke"
```

## Self-Review Checklist

- Spec coverage:
  - Actor intent schema: Task 1.
  - Squad and mutant-pack debug agents: Task 2 and Task 4.
  - C++ as embodiment through actuator command streams: Task 3.
  - Bridge-visible wake and command inspection: Task 5.
  - Replay foundation through deterministic recorded provider and existing tests: Task 4.
  - Smoke and handover docs: Task 6.
- Deferred by design:
  - Live CoC NPC takeover is not in this first slice; Task 7 from the spec follows after debug actor replay works.
  - Real provider-backed actor wakes are not in this first slice; the existing provider transport remains available for the next plan.
- Marker scan: this plan contains no red-flag markers, unnamed files, or missing produced interfaces.
- Type consistency: `ActorIntentPlan`, `ActorAgentRecord`, `ActuatorResult`, `ActorRuntime`, and bridge verb names are defined before use.
