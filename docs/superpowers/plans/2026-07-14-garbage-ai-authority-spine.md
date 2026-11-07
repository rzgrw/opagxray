# Garbage AI Authority Spine Implementation Plan

> **Status reconciliation (2026-11-07):** RECOVERY ACTIVE, NOT COMPLETE. Gate 1 is absent from baseline `8984220b6`; unmerged `1d951aa07` contains partial Tasks 1–10. Task 1 recovery is active separately in `ai-authority-manifest`; Task 2 can follow, while Task 3 contracts need reconciliation. This plan is not executable wholesale as written. Repository C++20 is unchanged; C++17 examples are a portable subset. Historical `claude-sonnet-5` requires official availability verification; no provider/model substitution is approved. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and prove the Gate 1 Garbage vertical slice in which Sonnet-backed world agents exclusively author transferred stalker, bandit, and mutant-pack objectives while xrSim validates facts and X-Ray executes live mechanics.

**Architecture:** Add a canonical `ZoneState`, per-scope `AuthorityRegistry`, strict revisioned intent codec, atomic commit path, decision ledger, bounded off-frame Sonnet scheduler, and pure materialization layer inside `xrSimCore`. A small `CAgentZoneRuntime` integrates those units with the engine tick and AgentBridge; `CLevel` implements the live adapter for spawning, objective steering, combat release/reacquisition, player provocation, and observed-outcome feedback.

**Tech Stack:** C++17, CMake 3.23+, existing xrSim value-return conventions, `std::thread`/condition variables, libcurl-backed Anthropic Messages API, X-Ray `CScriptEntity` live control, Python 3 `unittest`, AgentBridge, OpenGL runtime.

## Global Constraints

- Implement only Gate 1, the uninterrupted Garbage vertical slice; cross-level continuity, all-level offline simulation, and global legacy-planner retirement remain separate gates.
- Use the Anthropic provider and `claude-sonnet-5` for every live creative world-agent decision; add no provider or model fallback.
- Accept the API key only from `XRAY_AGENT_API_KEY`; never log it, persist it, put it in a manifest, or pass it as a command-line argument.
- Do not add a cumulative session call or token limit. Keep one in-flight request per agent, `maxConcurrent=3`, `maxQueue=64`, `requestsPerMinute=20`, `minCooldownMs=5000`, `maxRetries=2`, `baseBackoffMs=1000`, `circuitFailureThreshold=3`, and `circuitOpenMs=30000` as configurable initial stability bounds.
- Provider I/O, backoff, response decoding, and cancellation must never block the frame or simulation thread; completed work is drained only at a simulation-tick boundary.
- All model-, network-, manifest-, authority-, validation-, and materialization-originated failures are returned as values; do not rely on C++ exceptions because Darwin builds use `XRAY_EXCEPTIONS=0`.
- xrSim is the sole writer of canonical facts. Facts, private beliefs, plans, and observed consequences remain distinct records.
- Every writable scope has exactly one author. After transfer, legacy objective writes are rejected and audited; live X-Ray systems retain only deterministic mechanics.
- Apply an intent only when its schema, IDs, ranges, authority, preconditions, conservation guards, and base revision all validate. Stage on a copy and atomically commit the complete dependent operation group.
- Never silently rebase a stale response. Record it, discard it, and schedule a new wake against the current revision after cooldown.
- Preserve deterministic commit and ledger order even when provider completions arrive out of order.
- Preserve GL and legacy behavior outside explicitly transferred Gate 1 scopes.
- Turn player immortality on with AgentBridge `cmd g_god on`; do not modify gameplay damage rules to make the player immortal.
- Use the fixed version-controlled scenario and seed. Do not inherit participating populations or objective ownership from the loaded save.

---

## File map

### xrSimCore

- Create `src/xrEngine/xrSim/xrSimZoneManifest.h/.cpp`: strict Gate 1 manifest grammar and value-return parser.
- Create `src/xrEngine/xrSim/xrSimAuthority.h/.cpp`: scope identifiers, four-state transfer machine, one-writer checks, and authority audit.
- Create `src/xrEngine/xrSim/xrSimZoneState.h/.cpp`: canonical factions, locations, routes, groups, resources, objectives, events, deterministic digest, and snapshots.
- Create `src/xrEngine/xrSim/xrSimIntentEnvelope.h/.cpp`: strict `xrsim_world_intent_v1` codec and the complete typed operation vocabulary.
- Create `src/xrEngine/xrSim/xrSimIntentCommit.h/.cpp`: authority/range/conservation validation, copy-stage application, tick-boundary revalidation, and atomic commit.
- Create `src/xrEngine/xrSim/xrSimDecisionLedger.h/.cpp`: request, verdict, delta, event, usage, retry, latency, and hash records without secrets.
- Create `src/xrEngine/xrSim/xrSimWorldAgents.h/.cpp`: persistent charters, private beliefs, bounded memory, projections, and wake contexts.
- Create `src/xrEngine/xrSim/xrSimWorldProvider.h/.cpp`: Sonnet request/response adapter and recorded/fault providers.
- Create `src/xrEngine/xrSim/xrSimWakeScheduler.h/.cpp`: bounded priority queue, coalescing, rate limit, retry, circuit breaker, cancellation, and deterministic completion drain.
- Create `src/xrEngine/xrSim/xrSimMaterialization.h/.cpp`: stable-to-live bindings, typed live commands/observations, and reconciliation.
- Create `src/xrEngine/xrSim/xrSimZoneRuntime.h/.cpp`: Gate 1 orchestration, simulation ticks, event routing, provider wake/commit/materialize loop, metrics, and bridge command surface.
- Create `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`: focused deterministic Gate 1 test executable.
- Modify `src/xrEngine/xrSim/xrSimAgentProvider.h/.cpp`: decoded Anthropic usage fields and response metadata.
- Modify `src/xrEngine/CMakeLists.txt`: sources and `xrSimAuthoritySpineTests` target.

### Engine/game integration

- Create `src/xrEngine/AgentZoneRuntime.h/.cpp`: engine-owned lifecycle wrapper and exported hit/death notification functions.
- Modify `src/xrEngine/AgentBridge.cpp`: route `agent.zone.*` verbs.
- Modify `src/xrEngine/device.cpp`: tick the runtime after `FrameMove` without waiting on provider work.
- Modify `src/xrEngine/x_ray.cpp`: initialize before AgentBridge and destroy after AgentBridge.
- Modify `src/xrEngine/IGame_Level.h`: value-return `IAgentZoneLiveAdapter` boundary.
- Create `src/xrGame/Level_agent_zone.cpp`: `CLevel` implementation of spawn, observation, objective steering, debug player placement, and bounded player-sourced hit.
- Modify `src/xrGame/Level.h`, `src/xrGame/CMakeLists.txt`: declare and compile the adapter.
- Modify `src/xrGame/entity_alive.cpp`: report bound-entity hits and deaths to the runtime after normal gameplay handling.

### Scenario and acceptance harness

- Create `tools/ai_garbage_gate1.xrsim`: exact scenario, agents, groups, anchors, routes, resources, authority scopes, and seed.
- Create `tools/ai_zone_soak.py`: launch/control/collect/report harness for preflight and the 60-minute live run.
- Create `tools/tests/test_ai_zone_soak.py`: parser, phase, artifact, metrics, and acceptance-report tests.
- Modify `docs/HANDOVER.md`: Gate 1 commands, key handling, bridge verbs, artifacts, and resume point.

---

### Task 1: Canonical scenario manifest and Zone state

**Files:**
- Create: `src/xrEngine/xrSim/xrSimZoneManifest.h`
- Create: `src/xrEngine/xrSim/xrSimZoneManifest.cpp`
- Create: `src/xrEngine/xrSim/xrSimZoneState.h`
- Create: `src/xrEngine/xrSim/xrSimZoneState.cpp`
- Create: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `xrSim::Handle` from `xrSimWorldState.h`; no runtime or provider dependency.
- Produces: `ParseZoneManifest(const std::string&) -> ZoneManifestParseResult`, `ZoneState::Bootstrap(const ZoneManifest&) -> ZoneResult`, stable lookup methods, `ZoneState::Revision()`, `ZoneState::Digest()`, and `ZoneState::SaveSnapshot()`.

- [ ] **Step 1: Add the focused test target and failing manifest/state tests**

Add the new source target beside `xrSimWorldStateTests`:

```cmake
add_executable(xrSimAuthoritySpineTests EXCLUDE_FROM_ALL
    xrSim/tests/xrSimAuthoritySpineTests.cpp
)
target_include_directories(xrSimAuthoritySpineTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(xrSimAuthoritySpineTests PRIVATE xrSimCore)
```

Start the new test file with a small `Expect` helper and these assertions:

```cpp
#include "xrSim/xrSimZoneManifest.h"
#include "xrSim/xrSimZoneState.h"

#include <iostream>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

const char* kManifest = R"(xrsim_zone_manifest_v1
scenario=garbage_gate1
save=radik zagirov - to garbage
level=l02_garbage
seed=14072026
origin=player
anchor=stalker_camp,3500,0,0
anchor=bandit_camp,-3500,0,0
anchor=scrapyard_center,0,0,4500
faction=stalker,101
faction=bandit,102
group=stalker_squad,squad,stalker,sim_default_stalker_0,3,stalker_camp,201
group=bandit_squad,squad,bandit,sim_default_bandit_0,3,bandit_camp,202
group=garbage_dogs,mutant_pack,ecology,dog_weak,4,scrapyard_center,203
resource=scrap_cache,scrap,scrapyard_center,40,5
route=stalker_to_scrap,stalker_camp,scrapyard_center
agent=100,zone,1,zone_director,zone_director_v1
agent=101,faction,1,stalker_faction,stalker_faction_v1
agent=102,faction,2,bandit_faction,bandit_faction_v1
agent=103,ecology,1,garbage_ecology,ecology_v1
agent=104,economy,1,garbage_economy,economy_v1
agent=105,region,1,garbage_region,region_v1
agent=201,squad,1,stalker_squad,squad_v1
agent=202,squad,2,bandit_squad,squad_v1
agent=203,mutant_pack,3,garbage_dogs,pack_v1
)";

bool TestManifestBootstrapsDeterministically()
{
    const xrSim::ZoneManifestParseResult parsed = xrSim::ParseZoneManifest(kManifest);
    if (!Expect(parsed.ok, "valid manifest parses"))
        return false;
    xrSim::ZoneState first;
    xrSim::ZoneState second;
    const xrSim::ZoneResult a = first.Bootstrap(parsed.manifest);
    const xrSim::ZoneResult b = second.Bootstrap(parsed.manifest);
    return Expect(a.ok && b.ok, "manifest bootstraps") &&
        Expect(first.Revision() == 1, "bootstrap creates revision one") &&
        Expect(first.Digest() == second.Digest(), "same manifest produces same digest") &&
        Expect(first.FindGroupByName("bandit_squad").IsValid(), "group has stable handle") &&
        Expect(first.FindLocationByName("scrapyard_center").IsValid(), "anchor has stable handle");
}

bool TestManifestRejectsUnknownAndDuplicateRecords()
{
    return Expect(!xrSim::ParseZoneManifest("xrsim_zone_manifest_v1\nunknown=x\n").ok,
               "unknown key rejected") &&
        Expect(!xrSim::ParseZoneManifest(
            "xrsim_zone_manifest_v1\nscenario=x\nscenario=y\n").ok,
            "duplicate singleton rejected");
}
}

int main()
{
    bool ok = true;
    ok = TestManifestBootstrapsDeterministically() && ok;
    ok = TestManifestRejectsUnknownAndDuplicateRecords() && ok;
    return ok ? 0 : 1;
}
```

- [ ] **Step 2: Build to verify the new tests fail**

Run:

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails because `xrSimZoneManifest.h` and `xrSimZoneState.h` do not exist.

- [ ] **Step 3: Define the manifest and canonical-state value types**

Use these public contracts:

```cpp
// xrSimZoneManifest.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ManifestAnchor { std::string name; int32_t xCm = 0; int32_t yCm = 0; int32_t zCm = 0; };
struct ManifestFaction { std::string name; uint32_t agentId = 0; };
struct ManifestGroup
{
    std::string name;
    std::string kind;
    std::string ownerDomain;
    std::string section;
    uint32_t count = 0;
    std::string anchor;
    uint32_t agentId = 0;
};
struct ManifestResource
{
    std::string name;
    std::string kind;
    std::string anchor;
    int32_t quantity = 0;
    int32_t minimum = 0;
};
struct ManifestRoute { std::string name; std::string from; std::string to; };
struct ManifestAgent
{
    uint32_t agentId = 0;
    std::string scopeKind;
    uint32_t scopeId = 0;
    std::string name;
    std::string charterKey;
};
struct ZoneManifest
{
    std::string scenario;
    std::string saveName;
    std::string levelName;
    uint32_t seed = 0;
    std::string origin;
    std::vector<ManifestAnchor> anchors;
    std::vector<ManifestFaction> factions;
    std::vector<ManifestGroup> groups;
    std::vector<ManifestResource> resources;
    std::vector<ManifestRoute> routes;
    std::vector<ManifestAgent> agents;
};
struct ZoneManifestParseResult { bool ok = false; std::string reason; ZoneManifest manifest; };
ZoneManifestParseResult ParseZoneManifest(const std::string& text);
}
```

```cpp
// xrSimZoneState.h
#pragma once
#include "xrSim/xrSimWorldState.h"
#include "xrSim/xrSimZoneManifest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ZoneResult { bool ok = false; std::string reason; uint64_t revision = 0; };
struct FixedPosition { int32_t xCm = 0; int32_t yCm = 0; int32_t zCm = 0; uint32_t levelVertex = 0xffffffffu; };
enum class GroupKind : uint8_t { Squad, MutantPack };
enum class ObjectiveKind : uint8_t { Hold, Move, Retreat, Resupply, Hunt, Avoid, Reinforce };
enum class ObjectiveAuthor : uint8_t { Legacy, Ai, Executor };
enum class ObjectiveSubjectKind : uint8_t { Scope, Faction, Group, NamedActor };
struct FactionRecord { Handle id; std::string name; uint32_t agentId = 0; };
struct LocationRecord { Handle id; std::string name; FixedPosition position; Handle ownerFaction; };
struct RouteRecord { Handle id; std::string name; Handle from; Handle to; bool open = true; };
struct GroupRecord
{
    Handle id;
    std::string name;
    GroupKind kind = GroupKind::Squad;
    Handle faction;
    std::string section;
    uint32_t initialCount = 0;
    uint32_t aliveCount = 0;
    Handle location;
    uint32_t agentId = 0;
    bool active = true;
};
struct ResourceRecord
{
    Handle id;
    std::string name;
    std::string kind;
    Handle location;
    int32_t quantity = 0;
    int32_t minimum = 0;
    int32_t supply = 0;
    int32_t demand = 0;
    int32_t maximum = 1000;
};
struct RelationRecord { Handle first; Handle second; int16_t score = 0; };
struct ObjectiveRecord
{
    uint64_t planId = 0;
    uint32_t scopeKind = 0;
    uint32_t scopeId = 0;
    ObjectiveSubjectKind subjectKind = ObjectiveSubjectKind::Group;
    Handle subject;
    std::string subjectName;
    ObjectiveKind kind = ObjectiveKind::Hold;
    Handle target;
    uint64_t expiresAtTick = 0;
    ObjectiveAuthor author = ObjectiveAuthor::Legacy;
    bool active = false;
};
struct IncidentRecord
{
    uint64_t id = 0;
    Handle location;
    std::string kind;
    int32_t magnitude = 0;
    bool pending = true;
};
struct SalientActorRecord
{
    Handle id;
    Handle parentGroup;
    uint16_t lastLiveObjectId = 0xffffu;
    bool alive = true;
};
struct WorldEvent
{
    uint64_t seq = 0;
    uint64_t tick = 0;
    std::string kind;
    Handle subject;
    Handle object;
    int32_t amount = 0;
    std::string source;
};

class ZoneState
{
public:
    ZoneResult Bootstrap(const ZoneManifest& manifest);
    uint64_t Revision() const;
    Handle FindFactionByName(const std::string& name) const;
    Handle FindLocationByName(const std::string& name) const;
    Handle FindGroupByName(const std::string& name) const;
    Handle FindResourceByName(const std::string& name) const;
    Handle FindRouteByName(const std::string& name) const;
    const FactionRecord* FindFaction(Handle id) const;
    const LocationRecord* FindLocation(Handle id) const;
    LocationRecord* FindLocation(Handle id);
    const GroupRecord* FindGroup(Handle id) const;
    GroupRecord* FindGroup(Handle id);
    const ResourceRecord* FindResource(Handle id) const;
    ResourceRecord* FindResource(Handle id);
    const RouteRecord* FindRoute(Handle id) const;
    RouteRecord* FindRoute(Handle id);
    ZoneResult AddGroup(const GroupRecord& group);
    ZoneResult DeactivateGroup(Handle group);
    ZoneResult SetRelation(Handle first, Handle second, int16_t score);
    ZoneResult AppendIncident(const IncidentRecord& incident);
    ZoneResult PromoteSalientActor(Handle group, uint16_t liveObjectId);
    const std::vector<ObjectiveRecord>& Objectives() const;
    std::vector<ObjectiveRecord>& MutableObjectives();
    const std::vector<WorldEvent>& Events() const;
    void AppendEvent(const WorldEvent& event);
    void SetRevision(uint64_t revision);
    std::string Digest() const;
    std::string SaveSnapshot() const;

private:
    uint64_t m_revision = 0;
    uint64_t m_nextEventSeq = 1;
    std::vector<FactionRecord> m_factions;
    std::vector<LocationRecord> m_locations;
    std::vector<RouteRecord> m_routes;
    std::vector<GroupRecord> m_groups;
    std::vector<ResourceRecord> m_resources;
    std::vector<RelationRecord> m_relations;
    std::vector<ObjectiveRecord> m_objectives;
    std::vector<IncidentRecord> m_incidents;
    std::vector<SalientActorRecord> m_salientActors;
    std::vector<WorldEvent> m_events;
};
}
```

The parser must normalize CRLF, reject invalid UTF-8/control bytes, reject unknown keys, require exactly one header/scenario/save/level/seed/origin, cap every name/section at 96 bytes, cap groups and agents at 32, group size at 8, anchors/routes/resources at 64, reject duplicate names/agent IDs/scopes, and verify every group/resource/route reference before returning `ok=true`. Parse groups with `group=name,kind,owner_domain,section,count,anchor,agent_id`: a Squad owner domain must name a declared faction, while a MutantPack owner domain must be the literal `ecology` and stores an invalid faction handle. Parse agents with the exact `agent=id,scope_kind,scope_id,name,charter_key` grammar; reject zero IDs, unknown scope kinds, and charter keys outside `zone_director_v1|stalker_faction_v1|bandit_faction_v1|ecology_v1|economy_v1|region_v1|squad_v1|pack_v1`. `Bootstrap` must build records in manifest order, assign 24-bit-index/8-bit-generation handles with generation `1`, set revision to `1`, and leave no partial state on failure by constructing a local `ZoneState staged` and moving it into `*this` only after validation.

- [ ] **Step 4: Register sources and run the focused tests**

Add the four new production files to `xrSimCore`, then run:

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: build succeeds and exits `0` with no `FAIL:` lines.

- [ ] **Step 5: Commit the deterministic canonical-state foundation**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimZoneManifest.h src/xrEngine/xrSim/xrSimZoneManifest.cpp src/xrEngine/xrSim/xrSimZoneState.h src/xrEngine/xrSim/xrSimZoneState.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: add canonical Gate 1 zone state"
```

---

### Task 2: Per-scope authority transfer and one-writer audit

**Files:**
- Create: `src/xrEngine/xrSim/xrSimAuthority.h`
- Create: `src/xrEngine/xrSim/xrSimAuthority.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `ZoneState::Revision()` from Task 1.
- Produces: `ScopeId`, `AuthorityRegistry::{Register,BeginShadow,BeginTransfer,CommitTransfer,ReverseTransfer,CanAuthor,RecordPlan,Audit}`, and deterministic `AuthorityAuditRecord` entries.

- [ ] **Step 1: Write failing authority state-machine tests**

```cpp
#include "xrSim/xrSimAuthority.h"

bool TestAuthorityTransferRejectsLegacyAfterCommit()
{
    xrSim::AuthorityRegistry registry;
    const xrSim::ScopeId scope{xrSim::AgentScopeKind::Squad, 7};
    bool ok = Expect(registry.Register(scope, 201, 1).ok, "scope registers in observe");
    ok = Expect(registry.BeginShadow(scope, 2).ok, "observe enters shadow") && ok;
    ok = Expect(registry.BeginTransfer(scope, 3).ok, "shadow enters transfer") && ok;
    ok = Expect(registry.CommitTransfer(scope, 3).ok, "matching transfer commits") && ok;
    ok = Expect(registry.CanAuthor(scope, xrSim::ObjectiveAuthor::Ai, 201).ok,
             "owner AI can author") && ok;
    const xrSim::AuthorityResult legacy =
        registry.CanAuthor(scope, xrSim::ObjectiveAuthor::Legacy, 0);
    return Expect(!legacy.ok && legacy.reason == "legacy_author_rejected",
        "legacy objective is rejected after transfer");
}

bool TestAuthorityTransferIsRevisionChecked()
{
    xrSim::AuthorityRegistry registry;
    const xrSim::ScopeId scope{xrSim::AgentScopeKind::MutantPack, 9};
    registry.Register(scope, 203, 1);
    registry.BeginShadow(scope, 2);
    registry.BeginTransfer(scope, 3);
    const xrSim::AuthorityResult result = registry.CommitTransfer(scope, 4);
    return Expect(!result.ok && result.reason == "transfer_revision_mismatch",
        "transfer cannot commit against another revision") &&
        Expect(registry.Find(scope)->mode == xrSim::AuthorityMode::TransferPending,
            "failed transfer does not change owner mode");
}
```

Append both functions to `main()`.

- [ ] **Step 2: Run to verify missing authority contracts fail compilation**

Run:

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on the missing `xrSimAuthority.h` include.

- [ ] **Step 3: Implement the exact authority contract**

```cpp
#pragma once
#include "xrSim/xrSimZoneState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class AgentScopeKind : uint8_t
{
    Zone, Faction, Ecology, Economy, Incident, Region, Squad, MutantPack, NamedActor
};
struct ScopeId { AgentScopeKind kind = AgentScopeKind::Zone; uint32_t id = 0; };
bool operator==(const ScopeId& lhs, const ScopeId& rhs);
bool operator<(const ScopeId& lhs, const ScopeId& rhs);
enum class AuthorityMode : uint8_t { Observe, Shadow, TransferPending, AiOwned };
struct AuthorityRecord
{
    ScopeId scope;
    uint32_t ownerAgentId = 0;
    AuthorityMode mode = AuthorityMode::Observe;
    uint64_t revision = 0;
    uint64_t activePlanId = 0;
    uint64_t expiresAtTick = 0;
};
struct AuthorityResult { bool ok = false; std::string reason; };
struct AuthorityAuditRecord
{
    uint64_t seq = 0;
    uint64_t revision = 0;
    ScopeId scope;
    AuthorityMode from = AuthorityMode::Observe;
    AuthorityMode to = AuthorityMode::Observe;
    ObjectiveAuthor attemptedAuthor = ObjectiveAuthor::Legacy;
    uint32_t attemptedAgentId = 0;
    bool accepted = false;
    std::string reason;
};

class AuthorityRegistry
{
public:
    AuthorityResult Register(ScopeId scope, uint32_t ownerAgentId, uint64_t revision);
    AuthorityResult BeginShadow(ScopeId scope, uint64_t revision);
    AuthorityResult BeginTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult CommitTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult ReverseTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult CanAuthor(ScopeId scope, ObjectiveAuthor author, uint32_t agentId);
    AuthorityResult RecordPlan(ScopeId scope, uint32_t agentId, uint64_t planId,
        uint64_t expiresAtTick, uint64_t revision);
    const AuthorityRecord* Find(ScopeId scope) const;
    const std::vector<AuthorityAuditRecord>& Audit() const;
    std::string DumpAudit() const;

private:
    AuthorityRecord* FindMutable(ScopeId scope);
    AuthorityResult Transition(ScopeId scope, AuthorityMode expected, AuthorityMode next,
        uint64_t revision);
    void AppendAudit(ScopeId scope, AuthorityMode from, AuthorityMode to,
        ObjectiveAuthor author, uint32_t agentId, uint64_t revision, bool accepted,
        const std::string& reason);
    uint64_t m_nextAuditSeq = 1;
    std::vector<AuthorityRecord> m_records;
    std::vector<AuthorityAuditRecord> m_audit;
};
}
```

`Register` rejects duplicate scopes and agent ID `0`. Transitions accept only `Observe→Shadow→TransferPending→AiOwned`; `ReverseTransfer` is the only `AiOwned→Observe` path and requires an exact revision match. `CanAuthor` allows only Legacy in Observe/Shadow, no author in TransferPending, and only the recorded AI owner in AiOwned. It appends every rejected author attempt to the audit. `RecordPlan` calls `CanAuthor`, requires nonzero plan ID and future expiry, and mutates the active plan only on success.

- [ ] **Step 4: Build and run authority tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: exit `0`; audit contains one `legacy_author_rejected` entry from the test.

- [ ] **Step 5: Commit the authority state machine**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimAuthority.h src/xrEngine/xrSim/xrSimAuthority.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: enforce per-scope AI authority"
```

---

### Task 3: Strict world-intent envelope and atomic commit

**Files:**
- Create: `src/xrEngine/xrSim/xrSimIntentEnvelope.h`
- Create: `src/xrEngine/xrSim/xrSimIntentEnvelope.cpp`
- Create: `src/xrEngine/xrSim/xrSimIntentCommit.h`
- Create: `src/xrEngine/xrSim/xrSimIntentCommit.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `ZoneState`, `ScopeId`, and `AuthorityRegistry` from Tasks 1–2.
- Produces: `ParseWorldIntentEnvelope`, `FormatWorldIntentEnvelope`, `StageIntent`, and `CommitStagedIntent` with whole-envelope rejection.

- [ ] **Step 1: Write failing codec, stale-revision, authority, and atomicity tests**

Use this valid fixture and verify strict rejection paths:

```cpp
#include "xrSim/xrSimIntentCommit.h"

const char* kIntent = R"(xrsim_world_intent_v1
request_id=17
agent_id=202
scope=squad,2
base_revision=3
horizon_ticks=600
priority=80
wake_reason=player_trespass
rationale=defend the western approach
op=set_objective,bandit_squad,hold,bandit_camp,900
)";

bool TestIntentCodecIsStrictAndRoundTrips()
{
    const xrSim::IntentParseResult parsed = xrSim::ParseWorldIntentEnvelope(kIntent);
    return Expect(parsed.ok, "valid world intent parses") &&
        Expect(parsed.envelope.operations.size() == 1, "one operation decoded") &&
        Expect(xrSim::ParseWorldIntentEnvelope(
            std::string(kIntent) + "unknown=value\n").reason == "unknown_record",
            "unknown record rejected") &&
        Expect(xrSim::ParseWorldIntentEnvelope(
            xrSim::FormatWorldIntentEnvelope(parsed.envelope)).ok,
            "formatted envelope reparses");
}

bool TestIntentCommitRejectsStaleAndMixedPlansAtomically()
{
    const xrSim::ZoneManifestParseResult manifest = xrSim::ParseZoneManifest(kManifest);
    xrSim::ZoneState world;
    world.Bootstrap(manifest.manifest);
    world.SetRevision(3);
    xrSim::AuthorityRegistry authority;
    const xrSim::ScopeId scope{xrSim::AgentScopeKind::Squad, 2};
    authority.Register(scope, 202, 1);
    authority.BeginShadow(scope, 2);
    authority.BeginTransfer(scope, 3);
    authority.CommitTransfer(scope, 3);

    xrSim::IntentEnvelope stale = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    stale.baseRevision = 2;
    const std::string before = world.Digest();
    const xrSim::StageIntentResult staleResult = xrSim::StageIntent(world, authority, stale, 300);
    if (!Expect(!staleResult.ok && staleResult.reason == "stale_revision", "stale rejected"))
        return false;

    xrSim::IntentEnvelope mixed = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    mixed.operations.push_back({xrSim::PlanOpKind::SetSupply, "missing_resource", "", "", 4, 0});
    const xrSim::StageIntentResult mixedResult = xrSim::StageIntent(world, authority, mixed, 300);
    return Expect(!mixedResult.ok, "invalid dependent operation rejects envelope") &&
        Expect(world.Digest() == before, "failed stage leaves canonical state untouched");
}
```

- [ ] **Step 2: Build to verify missing intent contracts fail compilation**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimIntentCommit.h`.

- [ ] **Step 3: Define the strict operation and commit contracts**

```cpp
// xrSimIntentEnvelope.h
#pragma once
#include "xrSim/xrSimAuthority.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class PlanOpKind : uint8_t
{
    Create, Dissolve, Dispatch, Move, Retreat, Resupply, Claim, Release, Relate,
    SetObjective, ClearObjective, AdjustPopulation, SetMigration, SetSupply,
    SetDemand, SetRoute, ProposeIncident
};
struct PlanOp
{
    PlanOpKind kind = PlanOpKind::SetObjective;
    std::string subject;
    std::string verb;
    std::string target;
    int32_t amount = 0;
    uint64_t expiresAtTick = 0;
};
struct IntentEnvelope
{
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    ScopeId scope;
    uint64_t baseRevision = 0;
    uint32_t horizonTicks = 0;
    uint8_t priority = 0;
    std::string wakeReason;
    std::string rationaleSummary;
    std::vector<std::string> preconditions;
    std::vector<std::string> constraints;
    std::vector<PlanOp> operations;
};
struct IntentParseResult { bool ok = false; std::string reason; IntentEnvelope envelope; };
IntentParseResult ParseWorldIntentEnvelope(const std::string& text);
std::string FormatWorldIntentEnvelope(const IntentEnvelope& envelope);
}
```

```cpp
// xrSimIntentCommit.h
#pragma once
#include "xrSim/xrSimIntentEnvelope.h"

namespace xrSim
{
struct AppliedDelta
{
    PlanOpKind kind = PlanOpKind::SetObjective;
    std::string subject;
    std::string before;
    std::string after;
};
struct StagedIntent
{
    IntentEnvelope envelope;
    ZoneState state;
    std::vector<AppliedDelta> deltas;
    std::vector<WorldEvent> events;
};
struct StageIntentResult { bool ok = false; std::string reason; StagedIntent staged; };
struct CommitIntentResult
{
    bool ok = false;
    bool stale = false;
    std::string reason;
    uint64_t committedRevision = 0;
    uint64_t planId = 0;
    std::vector<AppliedDelta> deltas;
    std::vector<WorldEvent> events;
};
StageIntentResult StageIntent(const ZoneState& world, AuthorityRegistry& authority,
    const IntentEnvelope& envelope, uint64_t currentTick);
CommitIntentResult CommitStagedIntent(ZoneState& world, AuthorityRegistry& authority,
    StagedIntent&& staged, uint64_t currentTick);
}
```

The codec accepts exactly one header and one each of `request_id`, `agent_id`, `scope`, `base_revision`, `horizon_ticks`, `priority`, `wake_reason`, and `rationale`; it accepts zero-to-eight `precondition`, `constraint`, and `op` lines; all other records fail with `unknown_record`. Cap the full response at 32 KiB, each string field at 512 bytes, operations at 8, horizon at 36,000 ticks, priority at 100, and require nonzero request/agent/revision/horizon values.

`StageIntent` first checks `baseRevision == world.Revision()`, `authority.CanAuthor(scope, Ai, agentId)`, and every precondition. It copies `world` into `staged.state`, applies operations there in input order, and returns no staged object if any operation fails. The operation behavior is exact:

| Operation | Gate 1 mutation and guard |
|---|---|
| `create` | Add one group only when subject is absent, section exists in the manifest-derived state, count is `1..8`, and faction population budget remains nonnegative. |
| `dissolve` | Mark a group inactive only when alive count is `0` and no active objective references it. |
| `dispatch`, `move`, `retreat`, `resupply`, `set_objective` | Replace the named faction/group subject’s active objective in the envelope scope; target must be a known location/route, expiry must be after current tick and no more than 36,000 ticks away. Faction scopes may name only their faction; Squad/Pack scopes may name only their group. |
| `claim`, `release` | Change a known location owner; claim requires a live or canonical group of the claiming faction at that location. |
| `relate` | Adjust a known faction-pair relation by `-10..10`, clamped to `-100..100`; self-relations are rejected. |
| `clear_objective` | Deactivate only an objective owned by the envelope scope. |
| `adjust_population` | Change a named group/cohort by `-8..8`, never below `0` and never above its manifest carrying budget. |
| `set_migration` | Set a pack objective to Move only across a known open route. |
| `set_supply`, `set_demand` | Change a known resource by `-20..20`, never below its declared minimum and never above `1000`. |
| `set_route` | Toggle only a known route using verb `open` or `closed`. |
| `propose_incident` | Append a pending incident event; it creates no claimed outcome and has a maximum magnitude of `10`. |

Reject operations outside the envelope owner’s class: Zone may resolve proposal conflicts but directly apply only `set_objective` to its own scope subject; Faction may apply faction `set_objective`, `claim`, `release`, and `relate`; Ecology may apply `adjust_population` and `set_migration`; Economy may apply `set_supply`, `set_demand`, and `set_route`; Incident may apply only `propose_incident`; Region may apply `create`, `dissolve`, and `dispatch` inside its region; Squad/MutantPack may apply `move`, `retreat`, `resupply`, `set_objective`, and `clear_objective` to their own group; NamedActor may set/clear only its own salient actor objective. A scope subject uses `subjectName` plus the envelope `ScopeId` and may have an invalid `Handle`; a faction/group/named-actor subject must resolve a stable handle. Cross-owner desires become wake proposals and never bypass this matrix.

`CommitStagedIntent` runs only at a simulation boundary, requires `world.Revision() == envelope.baseRevision`, rechecks authority and preconditions, sets the staged revision to `baseRevision + 1`, replaces `world` with the staged copy, records the plan in the authority registry, and returns its deltas/events. A second revision mismatch returns `stale=true` and leaves both state and active-plan record unchanged.

- [ ] **Step 4: Build and run the complete strict-commit tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: exit `0`; strict codec round-trip, stale rejection, authority rejection, and whole-envelope atomicity pass.

- [ ] **Step 5: Commit the intent transaction layer**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimIntentEnvelope.h src/xrEngine/xrSim/xrSimIntentEnvelope.cpp src/xrEngine/xrSim/xrSimIntentCommit.h src/xrEngine/xrSim/xrSimIntentCommit.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: commit revisioned world intents atomically"
```

---

### Task 4: Decision ledger, scoped projections, and persistent agent memory

**Files:**
- Create: `src/xrEngine/xrSim/xrSimDecisionLedger.h`
- Create: `src/xrEngine/xrSim/xrSimDecisionLedger.cpp`
- Create: `src/xrEngine/xrSim/xrSimWorldAgents.h`
- Create: `src/xrEngine/xrSim/xrSimWorldAgents.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: canonical state, authority scopes, envelopes, deltas, and world events from Tasks 1–3.
- Produces: append-only `DecisionLedger`, stable request/response hashes, `WorldAgentRecord`, `BuildScopedProjection`, `PrepareWorldWake`, and `ApplyObservedConsequence`.

- [ ] **Step 1: Write failing tests for fact/belief separation, bounded memory, and secret-free ledger output**

```cpp
#include "xrSim/xrSimDecisionLedger.h"
#include "xrSim/xrSimWorldAgents.h"

bool TestScopedProjectionDoesNotExposeOtherAgentBeliefs()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    xrSim::WorldAgentRecord bandits;
    bandits.agentId = 202;
    bandits.scope = {xrSim::AgentScopeKind::Squad, 2};
    bandits.name = "bandit_squad_mind";
    bandits.beliefs.push_back({"player_intent", "hostile", 60});
    xrSim::WorldAgentRecord stalkers;
    stalkers.agentId = 201;
    stalkers.scope = {xrSim::AgentScopeKind::Squad, 1};
    stalkers.name = "stalker_squad_mind";
    stalkers.beliefs.push_back({"player_intent", "friendly", 40});

    const std::string projection = xrSim::BuildScopedProjection(world, bandits);
    return Expect(projection.find("hostile") != std::string::npos,
               "owner sees its belief") &&
        Expect(projection.find("friendly") == std::string::npos,
            "owner does not see another private belief");
}

bool TestObservedConsequenceUpdatesMemoryWithoutInventingFact()
{
    xrSim::WorldAgentRecord agent;
    agent.agentId = 202;
    agent.scope = {xrSim::AgentScopeKind::Squad, 2};
    xrSim::WorldEvent event{1, 500, "player_damaged_group", {}, {}, 5, "live_observation"};
    xrSim::ApplyObservedConsequence(agent, event);
    return Expect(agent.episodes.size() == 1, "observed event enters episodic memory") &&
        Expect(agent.memorySummary.find("player_damaged_group") != std::string::npos,
            "bounded summary reflects encounter");
}

bool TestLedgerHashesPayloadAndNeverPrintsKey()
{
    xrSim::DecisionLedger ledger;
    xrSim::RequestLedgerRecord record;
    record.requestId = 17;
    record.agentId = 202;
    record.promptHash = xrSim::StableLedgerHash("prompt");
    record.responseHash = xrSim::StableLedgerHash("response");
    record.provider = "anthropic";
    record.model = "claude-sonnet-5";
    ledger.AppendRequest(record);
    const std::string dump = ledger.Dump();
    return Expect(dump.find("prompt") == std::string::npos, "raw prompt absent") &&
        Expect(dump.find("sk-ant") == std::string::npos, "secret prefix absent") &&
        Expect(dump.find("claude-sonnet-5") != std::string::npos, "model metadata present");
}
```

- [ ] **Step 2: Build to verify missing ledger/agent contracts fail compilation**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimDecisionLedger.h`.

- [ ] **Step 3: Implement the ledger and agent contracts**

```cpp
// xrSimDecisionLedger.h
#pragma once
#include "xrSim/xrSimIntentCommit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class ProviderVerdict : uint8_t
{
    Accepted, Coast, Timeout, RateLimited, TransportError, Cancelled,
    CodecRejected, ValidationRejected, StaleRejected
};
struct TokenUsage { uint32_t inputTokens = 0; uint32_t outputTokens = 0; };
struct RequestLedgerRecord
{
    uint64_t seq = 0;
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    ScopeId scope;
    uint64_t baseRevision = 0;
    std::string wakeReason;
    std::string promptHash;
    std::string responseHash;
    std::string provider;
    std::string model;
    TokenUsage usage;
    uint32_t latencyMs = 0;
    uint32_t retryCount = 0;
    ProviderVerdict providerVerdict = ProviderVerdict::Coast;
    std::string codecVerdict;
    std::string validationVerdict;
    uint64_t committedRevision = 0;
    std::vector<AppliedDelta> deltas;
    std::vector<uint64_t> eventSeqs;
};
class DecisionLedger
{
public:
    void AppendRequest(RequestLedgerRecord record);
    const std::vector<RequestLedgerRecord>& Requests() const;
    std::string Dump() const;
    std::string DumpPage(uint64_t afterSeq, uint32_t limit) const;
    std::string SaveReplayPacket() const;
private:
    uint64_t m_nextSeq = 1;
    std::vector<RequestLedgerRecord> m_requests;
};
std::string StableLedgerHash(const std::string& value);
uint64_t StableLedgerHash64(const std::string& value);
}
```

```cpp
// xrSimWorldAgents.h
#pragma once
#include "xrSim/xrSimDecisionLedger.h"

namespace xrSim
{
struct AgentBelief { std::string key; std::string value; uint8_t confidence = 0; };
struct AgentEpisode
{
    uint64_t eventSeq = 0;
    uint64_t tick = 0;
    std::string kind;
    std::string summary;
};
struct WorldAgentRecord
{
    uint32_t agentId = 0;
    ScopeId scope;
    std::string name;
    uint32_t charterVersion = 1;
    std::string charter;
    std::string memorySummary;
    std::vector<AgentBelief> beliefs;
    std::vector<AgentEpisode> episodes;
    uint64_t lastPlanId = 0;
    uint64_t lastWakeMs = 0;
};
struct WorldWakeContext
{
    uint64_t requestId = 0;
    WorldAgentRecord agent;
    uint64_t baseRevision = 0;
    uint64_t tick = 0;
    std::string wakeReason;
    std::string observation;
    std::string prompt;
};
std::string BuildScopedProjection(const ZoneState& world, const WorldAgentRecord& agent);
WorldWakeContext PrepareWorldWake(uint64_t requestId, const ZoneState& world,
    const WorldAgentRecord& agent, uint64_t tick, const std::string& wakeReason);
void ApplyObservedConsequence(WorldAgentRecord& agent, const WorldEvent& event);
std::vector<WorldAgentRecord> CreateWorldAgents(const ZoneManifest& manifest);
}
```

Implement `StableLedgerHash64` as FNV-1a 64-bit and `StableLedgerHash` as its lowercase 16-hex-digit rendering for deterministic replay identity, not credential security. `Dump` and replay packets contain hashes, never prompt/response bodies or config values. `BuildScopedProjection` emits canonical facts plus only the target agent’s beliefs, episodes, memory, charter, active plan, and permitted parent summaries. Keep the newest 32 episodes, 64 beliefs, a 2 KiB reflection summary, and a 16 KiB projection. `ApplyObservedConsequence` accepts only `WorldEvent` values already appended to `ZoneState`; it never mutates canonical facts.

`CreateWorldAgents` maps the manifest scope strings to `AgentScopeKind`, preserves manifest order, and resolves every charter key to a compact versioned charter. Faction charters set faction priorities but forbid squad-fact writes; Region arbitrates local proposals; Squad/Pack owns its persistent live objective; Ecology owns population/migration; Economy owns supply/demand; Zone resolves cross-owner conflict. Every charter states that model narration is not a fact and all effects must use typed operations.

- [ ] **Step 4: Run focused tests and the existing xrSim regression binary**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
```

Expected: both executables exit `0`.

- [ ] **Step 5: Commit the trace and memory layer**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimDecisionLedger.h src/xrEngine/xrSim/xrSimDecisionLedger.cpp src/xrEngine/xrSim/xrSimWorldAgents.h src/xrEngine/xrSim/xrSimWorldAgents.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: trace zone decisions and agent memory"
```

---

### Task 5: Sonnet world-intent provider and usage decoding

**Files:**
- Create: `src/xrEngine/xrSim/xrSimWorldProvider.h`
- Create: `src/xrEngine/xrSim/xrSimWorldProvider.cpp`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.h`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `AgentProviderConfig`, `IAnthropicTransport`, Anthropic request builder, `WorldWakeContext`, strict intent codec, and `TokenUsage`.
- Produces: `IWorldIntentProvider`, `AnthropicWorldIntentProvider`, `RecordedWorldIntentProvider`, `FaultWorldIntentProvider`, and value-rich `WorldProviderResult`.

- [ ] **Step 1: Write failing injected-transport and failure-value tests**

```cpp
#include "xrSim/xrSimWorldProvider.h"

class WorldFakeTransport final : public xrSim::IAnthropicTransport
{
public:
    xrSim::AnthropicTransportResult next;
    xrSim::AnthropicMessagesRequest captured;
    xrSim::AnthropicTransportResult Send(const xrSim::AgentProviderConfig&,
        const xrSim::AnthropicMessagesRequest& request) override
    {
        captured = request;
        return next;
    }
};

bool TestWorldProviderDecodesIntentAndUsage()
{
    xrSim::AgentProviderConfig config;
    config.apiKey = "in-memory-test-key";
    WorldFakeTransport transport;
    transport.next.ok = true;
    transport.next.status = 200;
    transport.next.latencyMs = 41;
    transport.next.body = R"({"content":[{"type":"text","text":"xrsim_world_intent_v1\nrequest_id=17\nagent_id=202\nscope=squad,2\nbase_revision=3\nhorizon_ticks=600\npriority=80\nwake_reason=player_trespass\nrationale=hold\nop=set_objective,bandit_squad,hold,bandit_camp,900\n"}],"usage":{"input_tokens":321,"output_tokens":87}})";
    xrSim::AnthropicWorldIntentProvider provider(config);
    provider.SetTransport(&transport);
    xrSim::WorldWakeContext context;
    context.requestId = 17;
    context.baseRevision = 3;
    context.agent.agentId = 202;
    context.agent.scope = {xrSim::AgentScopeKind::Squad, 2};
    context.prompt = "bounded prompt";
    const xrSim::WorldProviderResult result = provider.Wake(context);
    return Expect(result.ok && !result.coast, "world response accepted") &&
        Expect(result.usage.inputTokens == 321 && result.usage.outputTokens == 87,
            "usage decoded") &&
        Expect(result.intent.requestId == 17, "intent request identity preserved") &&
        Expect(transport.captured.body.find("in-memory-test-key") == std::string::npos,
            "key absent from request body");
}

bool TestWorldProviderClassifiesRetryableFailuresAsValues()
{
    xrSim::AgentProviderConfig config;
    config.apiKey = "in-memory-test-key";
    WorldFakeTransport transport;
    transport.next.ok = true;
    transport.next.status = 429;
    xrSim::AnthropicWorldIntentProvider provider(config);
    provider.SetTransport(&transport);
    const xrSim::WorldProviderResult result = provider.Wake({});
    return Expect(!result.ok && result.coast, "rate limit coasts") &&
        Expect(result.retryable && result.rateLimited, "rate limit classified") &&
        Expect(result.error == "http_status_429", "failure has stable reason");
}
```

- [ ] **Step 2: Build to verify the world-provider contracts are absent**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimWorldProvider.h`.

- [ ] **Step 3: Extend Anthropic decoding and implement the provider family**

Extend the existing decoded response without changing its callers’ text/error semantics:

```cpp
struct AnthropicTextResult
{
    bool ok = false;
    std::string text;
    std::string error;
    uint32_t inputTokens = 0;
    uint32_t outputTokens = 0;
};
```

Add the world provider contract:

```cpp
#pragma once
#include "xrSim/xrSimWorldAgents.h"

#include <memory>

namespace xrSim
{
struct WorldProviderResult
{
    bool ok = false;
    bool coast = false;
    bool retryable = false;
    bool rateLimited = false;
    std::string provider;
    std::string model;
    std::string error;
    std::string coastReason;
    std::string responseHash;
    IntentEnvelope intent;
    TokenUsage usage;
    uint32_t latencyMs = 0;
};
class IWorldIntentProvider
{
public:
    virtual ~IWorldIntentProvider() = default;
    virtual WorldProviderResult Wake(const WorldWakeContext& context) = 0;
    virtual void Cancel() {}
};
class AnthropicWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit AnthropicWorldIntentProvider(const AgentProviderConfig& config);
    void SetTransport(IAnthropicTransport* transport);
    void SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
    void Cancel() override;
private:
    AgentProviderConfig m_config;
    std::unique_ptr<IAnthropicTransport> m_ownedTransport;
    IAnthropicTransport* m_transport = nullptr;
};
class RecordedWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit RecordedWorldIntentProvider(std::vector<WorldProviderResult> script);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
private:
    std::vector<WorldProviderResult> m_script;
    size_t m_cursor = 0;
};
enum class InjectedProviderFault : uint8_t
{ None, Timeout, RateLimit, Malformed, Cancelled, TransportError };
class FaultWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit FaultWorldIntentProvider(InjectedProviderFault fault);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
    void Cancel() override;
private:
    InjectedProviderFault m_fault;
    bool m_cancelled = false;
};
std::unique_ptr<IWorldIntentProvider> CreateLiveWorldIntentProvider(const AgentProviderConfig& config);
}
```

The live provider must coast on disabled provider, missing key, missing transport, timeout/transport error, HTTP error, or cancellation. Mark only timeout, `429`, `500`, `502`, `503`, `504`, and transport failure retryable; mark `429` rate-limited. A decoded but malformed intent is `ok=false`, `coast=false`, `retryable=false`, `error="codec_rejected:<reason>"`. Verify response request/agent/scope/base-revision identities exactly match the copied wake context before returning `ok=true`. The system prompt must require only `xrsim_world_intent_v1`, no Markdown, no observation echo, maximum eight operations, and concise rationale summary rather than hidden reasoning. For deterministic non-blocking tests, `FaultWorldIntentProvider::Timeout` waits 100 ms inside the worker call before returning the timeout value; no frame-thread code calls it directly.

- [ ] **Step 4: Run provider and existing codec tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
```

Expected: both exit `0`; existing actor and coarse-agent Anthropic response tests remain green.

- [ ] **Step 5: Commit the Sonnet world-provider adapter**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimAgentProvider.h src/xrEngine/xrSim/xrSimAgentProvider.cpp src/xrEngine/xrSim/xrSimWorldProvider.h src/xrEngine/xrSim/xrSimWorldProvider.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: add Sonnet world intent provider"
```

---

### Task 6: Bounded off-frame wake scheduler

**Files:**
- Create: `src/xrEngine/xrSim/xrSimWakeScheduler.h`
- Create: `src/xrEngine/xrSim/xrSimWakeScheduler.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: copied `WorldWakeContext` and `IWorldIntentProvider` from Task 5.
- Produces: non-blocking `WakeScheduler::{Schedule,Pump,DrainReady,CancelAll,Shutdown}`, deterministic completions, and scheduler metrics.

- [ ] **Step 1: Write failing queue, coalescing, retry, circuit, and non-blocking tests**

```cpp
#include "xrSim/xrSimWakeScheduler.h"
#include <chrono>

bool TestSchedulerCoalescesAndBoundsOneInflightPerAgent()
{
    std::vector<xrSim::WorldProviderResult> script(2);
    script[0].ok = true;
    script[1].ok = true;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(script);
    xrSim::WakeScheduler scheduler({}, std::move(provider));
    xrSim::WorldWakeContext first;
    first.agent.agentId = 202;
    first.wakeReason = "heartbeat";
    xrSim::WorldWakeContext urgent = first;
    urgent.wakeReason = "player_trespass";
    const xrSim::ScheduleWakeResult a =
        scheduler.Schedule(first, xrSim::WakePriority::Heartbeat, 1000);
    const xrSim::ScheduleWakeResult b =
        scheduler.Schedule(urgent, xrSim::WakePriority::NearPlayer, 1001);
    scheduler.Pump(1001);
    scheduler.WaitForIdleForTest(2000);
    const xrSim::SchedulerMetrics metrics = scheduler.Metrics();
    return Expect(a.ok && b.ok, "compatible wakes accepted") &&
        Expect(metrics.coalesced == 1, "second wake coalesced") &&
        Expect(metrics.maxInflightPerAgent == 1, "one request per agent");
}

bool TestSchedulerCallSiteNeverWaitsForSlowProvider()
{
    auto provider = std::make_unique<xrSim::FaultWorldIntentProvider>(
        xrSim::InjectedProviderFault::Timeout);
    xrSim::WakeScheduler scheduler({}, std::move(provider));
    xrSim::WorldWakeContext context;
    context.agent.agentId = 202;
    const auto start = std::chrono::steady_clock::now();
    scheduler.Schedule(context, xrSim::WakePriority::NearPlayer, 1000);
    scheduler.Pump(1000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    scheduler.Shutdown();
    return Expect(elapsed < 10, "schedule and pump never wait on provider");
}

bool TestSchedulerOpensCircuitAfterThreeFailures()
{
    auto provider = std::make_unique<xrSim::FaultWorldIntentProvider>(
        xrSim::InjectedProviderFault::TransportError);
    xrSim::SchedulerConfig config;
    config.minCooldownMs = 0;
    config.maxRetries = 0;
    xrSim::WakeScheduler scheduler(config, std::move(provider));
    for (uint64_t i = 0; i < 3; ++i)
    {
        xrSim::WorldWakeContext context;
        context.agent.agentId = 202;
        scheduler.Schedule(context, xrSim::WakePriority::NearPlayer, 1000 + i);
        scheduler.Pump(1000 + i);
        scheduler.WaitForIdleForTest(2000);
        scheduler.Pump(1000 + i);
        scheduler.DrainReady();
    }
    xrSim::WorldWakeContext blockedContext;
    blockedContext.agent.agentId = 202;
    const xrSim::ScheduleWakeResult blocked = scheduler.Schedule(
        blockedContext, xrSim::WakePriority::Heartbeat, 1004);
    return Expect(!blocked.ok && blocked.reason == "agent_circuit_open",
        "three failures open per-agent circuit");
}
```

- [ ] **Step 2: Build to verify scheduler symbols are missing**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimWakeScheduler.h`.

- [ ] **Step 3: Implement scheduler types and defaults**

```cpp
#pragma once
#include "xrSim/xrSimWorldProvider.h"

#include <memory>
#include <vector>

namespace xrSim
{
enum class WakePriority : uint8_t { NearPlayer, InvalidatedPlan, Strategic, Heartbeat };
struct SchedulerConfig
{
    uint32_t maxConcurrent = 3;
    uint32_t maxQueue = 64;
    uint32_t requestsPerMinute = 20;
    uint32_t minCooldownMs = 5000;
    uint32_t maxRetries = 2;
    uint32_t baseBackoffMs = 1000;
    uint32_t circuitFailureThreshold = 3;
    uint32_t circuitOpenMs = 30000;
};
struct ScheduleWakeResult
{
    bool ok = false;
    bool coalesced = false;
    uint64_t requestId = 0;
    std::string reason;
};
struct SchedulerCompletion
{
    uint64_t dispatchSeq = 0;
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    uint32_t retryCount = 0;
    WorldWakeContext context;
    WorldProviderResult result;
};
struct SchedulerMetrics
{
    uint64_t scheduled = 0;
    uint64_t dispatched = 0;
    uint64_t completed = 0;
    uint64_t coalesced = 0;
    uint64_t queueRejected = 0;
    uint64_t retries = 0;
    uint64_t circuitOpened = 0;
    uint32_t queueDepth = 0;
    uint32_t inflight = 0;
    uint32_t maxQueueDepth = 0;
    uint32_t maxInflight = 0;
    uint32_t maxInflightPerAgent = 0;
    uint32_t effectiveRequestsPerMinute = 20;
};
class WakeScheduler
{
public:
    WakeScheduler(const SchedulerConfig& config, std::unique_ptr<IWorldIntentProvider> provider);
    ~WakeScheduler();
    ScheduleWakeResult Schedule(WorldWakeContext context, WakePriority priority, uint64_t nowMs);
    void Pump(uint64_t nowMs);
    std::vector<SchedulerCompletion> DrainReady();
    void CancelAll();
    void Shutdown();
    SchedulerMetrics Metrics() const;
    bool WaitForIdleForTest(uint32_t timeoutMs);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
```

`Schedule` copies all context, rejects agent ID `0`, queue overflow, cooldown, and an open circuit as values. It coalesces a queued wake for the same agent by keeping the higher priority, newest revision/observation, and a sorted semicolon-joined set of wake reasons. `Pump` only moves eligible jobs into a worker queue and processes already-finished results; it never waits. Three worker threads call the synchronous provider. A provider result returns through a mutex-protected completion queue. `DrainReady` emits completions in monotonically increasing dispatch sequence; later completed requests remain buffered until earlier sequences are resolved. Retry only retryable results, preserving the same request ID and copied context, with `baseBackoffMs * 2^retryCount` plus deterministic jitter `StableLedgerHash64(requestId:retryCount) % 251`; never retry a codec/validation error. A `429` halves effective RPM to a floor of `1`; each successful minute raises it by `1` up to configured RPM. There is no lifetime-call counter or lifetime-call rejection path. `CancelAll` calls provider cancellation, marks queued/in-flight jobs cancelled as values, and leaves workers joinable. `Shutdown` is idempotent and joins all workers.

- [ ] **Step 4: Run scheduler tests repeatedly to expose races**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
for i in 1 2 3 4 5; do ./bin/arm64/Release/xrSimAuthoritySpineTests || exit 1; done
```

Expected: all five runs exit `0`, no hang, and `maxInflightPerAgent` remains `1`.

- [ ] **Step 5: Commit the bounded scheduler**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimWakeScheduler.h src/xrEngine/xrSim/xrSimWakeScheduler.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: schedule bounded off-frame world wakes"
```

---

### Task 7: Pure materialization and live binding registry

**Files:**
- Create: `src/xrEngine/xrSim/xrSimMaterialization.h`
- Create: `src/xrEngine/xrSim/xrSimMaterialization.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `ZoneState`, committed objectives/events, and stable `Handle` values.
- Produces: engine-independent `IAgentZoneLiveAdapter`, materializer-owned live bindings, typed commands/observations, and `Materializer::{Bootstrap,ApplyCommittedState,PollObservations,ToLivePosition}`.

- [ ] **Step 1: Write failing duplicate-spawn, redirect, and observed-outcome tests with a fake adapter**

```cpp
#include "xrSim/xrSimMaterialization.h"

class FakeLiveAdapter final : public xrSim::IAgentZoneLiveAdapter
{
public:
    uint32_t spawnCalls = 0;
    uint32_t objectiveCalls = 0;
    xrSim::ReadPositionResult CapturePlayerOrigin() override
    { return {true, "", {0, 0, 0, 0xffffffffu}}; }
    xrSim::LiveAdapterResult ValidateSection(const std::string&) override { return {true, ""}; }
    xrSim::SpawnGroupResult SpawnGroup(const xrSim::LiveSpawnRequest& request) override
    {
        ++spawnCalls;
        xrSim::SpawnGroupResult result;
        result.ok = true;
        for (uint32_t i = 0; i < request.count; ++i)
            result.objectIds.push_back(static_cast<uint16_t>(100 + i));
        return result;
    }
    xrSim::LiveAdapterResult DestroyObjects(const std::vector<uint16_t>&) override
    { return {true, ""}; }
    xrSim::LiveAdapterResult ApplyObjective(const xrSim::LiveObjectiveCommand&) override
    { ++objectiveCalls; return {true, ""}; }
    xrSim::ObserveGroupResult ObserveGroup(const xrSim::LiveBinding& binding) override
    { return {true, "", static_cast<uint32_t>(binding.objectIds.size()), false, {}, 100}; }
    xrSim::LiveAdapterResult ReleaseObjective(const xrSim::LiveBinding&) override { return {true, ""}; }
    xrSim::LiveAdapterResult SetPlayerAt(const xrSim::FixedPosition&) override { return {true, ""}; }
    xrSim::LiveAdapterResult DamageGroupFromPlayer(const xrSim::LiveBinding&, uint32_t) override
    { return {true, ""}; }
};

bool TestMaterializerNeverDuplicatesStableGroup()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    FakeLiveAdapter adapter;
    xrSim::Materializer materializer(adapter);
    const xrSim::MaterializeResult first = materializer.Bootstrap(world, 2);
    const xrSim::MaterializeResult second = materializer.Bootstrap(world, 2);
    const xrSim::MaterializeResult third = materializer.Bootstrap(world, 2);
    return Expect(first.ok && second.ok, "bootstrap calls succeed") &&
        Expect(third.ok && third.complete, "all groups eventually bind") &&
        Expect(adapter.spawnCalls == 3, "three manifest groups spawned once") &&
        Expect(materializer.Bindings().size() == 3, "stable groups never duplicate");
}

bool TestMaterializerFeedsObservedAliveCountBackAsEvent()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    FakeLiveAdapter adapter;
    xrSim::Materializer materializer(adapter);
    while (!materializer.Bootstrap(world, 2).complete) {}
    const xrSim::ObservationBatch batch = materializer.PollObservations(world, 50);
    return Expect(batch.ok, "observation poll succeeds") &&
        Expect(!batch.events.empty(), "live results return as observed events") &&
        Expect(batch.events.front().source == "live_observation", "outcome source is explicit");
}
```

- [ ] **Step 2: Build to verify the materialization interface is missing**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimMaterialization.h`.

- [ ] **Step 3: Implement the pure adapter and binding contracts**

```cpp
#pragma once
#include "xrSim/xrSimIntentCommit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct LiveAdapterResult { bool ok = false; std::string reason; };
struct ReadPositionResult
{
    bool ok = false;
    std::string reason;
    FixedPosition position;
};
struct LiveSpawnRequest
{
    Handle group;
    std::string section;
    uint32_t count = 0;
    FixedPosition position;
};
struct SpawnGroupResult
{
    bool ok = false;
    std::string reason;
    std::vector<uint16_t> objectIds;
};
struct LiveBinding
{
    Handle group;
    std::vector<uint16_t> objectIds;
    uint64_t boundRevision = 0;
    uint64_t activePlanId = 0;
    uint64_t lastObservationTick = 0;
    bool tacticalCombat = false;
    uint64_t calmSinceTick = 0;
};
struct LiveObjectiveCommand
{
    Handle group;
    uint64_t planId = 0;
    ObjectiveKind kind = ObjectiveKind::Hold;
    FixedPosition target;
    uint64_t expiresAtTick = 0;
};
struct ObserveGroupResult
{
    bool ok = false;
    std::string reason;
    uint32_t aliveCount = 0;
    bool inCombat = false;
    FixedPosition centroid;
    uint32_t healthPermille = 0;
};
class IAgentZoneLiveAdapter
{
public:
    virtual ~IAgentZoneLiveAdapter() = default;
    virtual ReadPositionResult CapturePlayerOrigin()
    { return {false, "live_adapter_unavailable", {}}; }
    virtual LiveAdapterResult ValidateSection(const std::string&)
    { return {false, "live_adapter_unavailable"}; }
    virtual SpawnGroupResult SpawnGroup(const LiveSpawnRequest&)
    { return {false, "live_adapter_unavailable", {}}; }
    virtual LiveAdapterResult DestroyObjects(const std::vector<uint16_t>&)
    { return {false, "live_adapter_unavailable"}; }
    virtual LiveAdapterResult ApplyObjective(const LiveObjectiveCommand&)
    { return {false, "live_adapter_unavailable"}; }
    virtual ObserveGroupResult ObserveGroup(const LiveBinding&)
    { return {false, "live_adapter_unavailable", 0, false, {}, 0}; }
    virtual LiveAdapterResult ReleaseObjective(const LiveBinding&)
    { return {false, "live_adapter_unavailable"}; }
    virtual LiveAdapterResult SetPlayerAt(const FixedPosition&)
    { return {false, "live_adapter_unavailable"}; }
    virtual LiveAdapterResult DamageGroupFromPlayer(const LiveBinding& binding,
        uint32_t damagePermille)
    { (void)binding; (void)damagePermille; return {false, "live_adapter_unavailable"}; }
};
struct MaterializeResult
{
    bool ok = false;
    bool complete = false;
    std::string reason;
    uint32_t applied = 0;
};
struct ObservationBatch
{
    bool ok = false;
    std::string reason;
    std::vector<WorldEvent> events;
};
class Materializer
{
public:
    explicit Materializer(IAgentZoneLiveAdapter& adapter);
    MaterializeResult Bootstrap(const ZoneState& world, uint32_t spawnBudget);
    MaterializeResult ApplyCommittedState(const ZoneState& world, uint32_t commandBudget,
        uint64_t currentTick);
    ObservationBatch PollObservations(const ZoneState& world, uint64_t currentTick);
    const LiveBinding* FindBinding(Handle group) const;
    const std::vector<LiveBinding>& Bindings() const;
    FixedPosition ToLivePosition(const FixedPosition& relative) const;
    void ReleaseAll();
private:
    IAgentZoneLiveAdapter& m_adapter;
    FixedPosition m_origin;
    bool m_originCaptured = false;
    std::vector<LiveBinding> m_bindings;
};
}
```

`Bootstrap` calls `CapturePlayerOrigin` exactly once, adds each manifest-relative centimetre anchor to that origin, validates every section before the first spawn, spawns at most `spawnBudget` complete groups per call, and records a binding only when returned IDs exactly match requested count and are unique across every binding. On a partial or duplicate result, call `DestroyObjects` for every returned ID, record the failure, and retry only through the runtime cooldown path. `ToLivePosition` performs the same origin translation for bridge player actions. `ApplyCommittedState` materializes only objectives with `subjectKind=Group`, redirects existing bindings, and never respawns for an objective change; faction objectives remain canonical strategic pressure that wakes owned child scopes. It applies at most `commandBudget=2` commands per tick. `PollObservations` reports alive-count, position, health, combat-entered, combat-left, objective-arrived, and missing-object changes as events. `ReleaseAll` releases script control, destroys only scenario-spawned object IDs, and clears bindings so reset cannot orphan or duplicate entities. Canonical mutation remains the runtime’s responsibility after it accepts these observations.

- [ ] **Step 4: Run focused and existing tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
```

Expected: both exit `0`; fake adapter proves no duplicate IDs or respawn-on-redirect.

- [ ] **Step 5: Commit the materialization boundary**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimMaterialization.h src/xrEngine/xrSim/xrSimMaterialization.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: materialize committed zone objectives"
```

---

### Task 8: Zone orchestration runtime and AgentBridge control surface

**Files:**
- Create: `src/xrEngine/xrSim/xrSimZoneRuntime.h`
- Create: `src/xrEngine/xrSim/xrSimZoneRuntime.cpp`
- Create: `src/xrEngine/AgentZoneRuntime.h`
- Create: `src/xrEngine/AgentZoneRuntime.cpp`
- Modify: `src/xrEngine/IGame_Level.h`
- Modify: `src/xrEngine/AgentBridge.cpp`
- Modify: `src/xrEngine/device.cpp`
- Modify: `src/xrEngine/x_ray.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`

**Interfaces:**
- Consumes: all pure xrSim components from Tasks 1–7 and an `IAgentZoneLiveAdapter` supplied only when a level is ready.
- Produces: `ZoneRuntime` tick orchestration, `CAgentZoneRuntime` engine lifecycle, `agent.zone.*` bridge verbs, and non-blocking hit/death event ingress.

- [ ] **Step 1: Write failing orchestration tests with a recorded provider and fake live adapter**

```cpp
#include "xrSim/xrSimZoneRuntime.h"

bool TestZoneRuntimeCommitsOnlyAtTickBoundary()
{
    FakeLiveAdapter adapter;
    xrSim::WorldProviderResult response;
    response.ok = true;
    response.intent = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(
        std::vector<xrSim::WorldProviderResult>{response});
    xrSim::ZoneRuntime runtime;
    xrSim::ZoneRuntimeConfig config;
    config.tickMs = 250;
    config.providerMode = xrSim::ZoneProviderMode::Recorded;
    const xrSim::ZoneManifest manifest = xrSim::ParseZoneManifest(kManifest).manifest;
    if (!Expect(runtime.Bootstrap(manifest, adapter, config, std::move(provider)).ok,
            "runtime bootstraps"))
        return false;
    runtime.TransferAllForTest();
    runtime.WakeAgentForTest(202, "player_trespass", xrSim::WakePriority::NearPlayer, 1000);
    runtime.OnFrame(1100, 10);
    const uint64_t before = runtime.World().Revision();
    runtime.Scheduler().WaitForIdleForTest(2000);
    runtime.OnFrame(1249, 11);
    if (!Expect(runtime.World().Revision() == before, "no mid-tick commit"))
        return false;
    runtime.OnFrame(1250, 12);
    return Expect(runtime.World().Revision() == before + 1,
        "ready response commits at boundary");
}

bool TestZoneRuntimeStaleCompletionSchedulesFreshWake()
{
    FakeLiveAdapter adapter;
    xrSim::WorldProviderResult response;
    response.ok = true;
    response.intent = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(
        std::vector<xrSim::WorldProviderResult>{response});
    xrSim::ZoneRuntime runtime;
    xrSim::ZoneRuntimeConfig config;
    config.scheduler.minCooldownMs = 0;
    runtime.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest, adapter, config,
        std::move(provider));
    runtime.TransferAllForTest();
    runtime.SetWorldRevisionForTest(response.intent.baseRevision + 1);
    runtime.WakeAgentForTest(202, "stale_test", xrSim::WakePriority::InvalidatedPlan, 1000);
    runtime.OnFrame(1250, 20);
    runtime.Scheduler().WaitForIdleForTest(2000);
    runtime.OnFrame(1500, 21);
    return Expect(runtime.Metrics().staleRejected == 1, "stale result recorded") &&
        Expect(runtime.Metrics().freshWakesAfterStale == 1, "fresh revision wake queued");
}
```

- [ ] **Step 2: Build to verify runtime contracts are absent**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimZoneRuntime.h`.

- [ ] **Step 3: Implement pure runtime state and bridge payloads**

```cpp
// xrSimZoneRuntime.h
#pragma once
#include "xrSim/xrSimMaterialization.h"
#include "xrSim/xrSimWakeScheduler.h"

#include <memory>

namespace xrSim
{
enum class ZoneProviderMode : uint8_t { Live, Recorded, Fault };
struct ZoneRuntimeConfig
{
    uint32_t tickMs = 250;
    uint32_t observationTicks = 1;
    uint32_t heartbeatTicks = 240;
    uint32_t commandBudget = 2;
    uint32_t spawnBudget = 1;
    uint32_t calmReacquireMs = 5000;
    ZoneProviderMode providerMode = ZoneProviderMode::Live;
    SchedulerConfig scheduler;
};
struct ZoneRuntimeMetrics
{
    uint64_t frameCalls = 0;
    uint64_t simulationTicks = 0;
    uint64_t providerWaitOnFrameUs = 0;
    uint64_t acceptedIntents = 0;
    uint64_t providerCoasts = 0;
    uint64_t codecRejected = 0;
    uint64_t validationRejected = 0;
    uint64_t staleRejected = 0;
    uint64_t freshWakesAfterStale = 0;
    uint64_t legacyObjectiveRejected = 0;
    uint64_t observedEvents = 0;
    uint64_t providerRequests = 0;
    uint64_t inputTokens = 0;
    uint64_t outputTokens = 0;
    uint64_t providerErrors = 0;
    uint64_t retries = 0;
    uint64_t totalLatencyMs = 0;
    uint32_t maxQueueDepth = 0;
    uint32_t maxInflight = 0;
    uint32_t liveEventQueueDepth = 0;
    uint64_t liveEventsDropped = 0;
};
struct ZoneRuntimeResult { bool ok = false; std::string reason; };
class ZoneRuntime
{
public:
    ZoneRuntime();
    ~ZoneRuntime();
    ZoneRuntimeResult Bootstrap(const ZoneManifest& manifest, IAgentZoneLiveAdapter& adapter,
        const ZoneRuntimeConfig& config, std::unique_ptr<IWorldIntentProvider> provider);
    ZoneRuntimeResult Start();
    void OnFrame(uint64_t nowMs, uint64_t frameNumber);
    void Reset();
    std::string HandleBridgeVerb(const std::string& verb, const std::string& payload,
        bool& ok);
    void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    void NotifyDeath(uint16_t targetId, uint16_t sourceId);
    const ZoneState& World() const;
    const AuthorityRegistry& Authority() const;
    const DecisionLedger& Ledger() const;
    const std::vector<WorldAgentRecord>& Agents() const;
    const ZoneRuntimeMetrics& Metrics() const;
    WakeScheduler& Scheduler();
    void TransferAllForTest();
    void WakeAgentForTest(uint32_t agentId, const std::string& reason,
        WakePriority priority, uint64_t nowMs);
    void SetWorldRevisionForTest(uint64_t revision);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
```

At each 250 ms boundary the runtime must perform this exact order: drain the bounded 256-entry live event inbox; append canonical observed events; update only scoped agent memories; poll live group observations; expire objectives; schedule event wakes; schedule deterministic randomized heartbeats from manifest seed; call scheduler `Pump`; drain completions in dispatch order; stage/revalidate/commit valid intents; append ledger verdicts; schedule downstream `upstream_plan_changed` wakes; apply at most two live commands; then increment the simulation tick. For heartbeat ordinal `n`, schedule agent `a` after `heartbeatTicks + StableLedgerHash64(seed:a:n) % 121` ticks, giving a repeatable 60–90 second quiet interval at the default tick. Coalesce duplicate hit observations for the same target/tick; if the inbox still overflows, increment `liveEventsDropped`, emit a warning event, and never block the caller. Provider failures coast on each scope’s current plan until expiry. At expiry, deactivate the canonical plan and let the compatibility executor keep a stationary steady posture; record it as continuity execution, not as a new objective or AI decision.

Bridge verbs and responses are:

```text
agent.zone.bootstrap <manifest-path>       -> scenario=<name> revision=1 groups=<n>
agent.zone.start live                      -> running provider=anthropic model=claude-sonnet-5
agent.zone.stop                            -> stopped
agent.zone.reset                           -> reset cancelled=<n>
agent.zone.shadow all                      -> shadowed=<n> revision=<r>
agent.zone.transfer all                    -> transferred=<n> revision=<r>
agent.zone.status                          -> running=<0|1> tick=<n> revision=<r> inflight=<n> queue=<n>
agent.zone.agents                          -> one stable line per agent
agent.zone.authority                       -> AuthorityRegistry::DumpAudit()
agent.zone.ledger [after-seq] [limit]      -> ordered page plus next=<seq> done=<0|1>
agent.zone.metrics                         -> key=value metrics including provider_wait_frame_us=0
agent.zone.snapshot                        -> ZoneState::SaveSnapshot()
agent.zone.wake <agent-id> <reason>        -> request=<id>
agent.zone.inject <fault-name>             -> fault=<name>
agent.zone.player <anchor-name>            -> player=<anchor-name>
agent.zone.face <group-name>               -> facing=<group-name>
agent.zone.damage <group-name> <permille>  -> damaged=<group-name> permille=<n>
```

Reject unknown verbs, paths above 1024 bytes, ledger limits outside `1..256`, fault names outside `timeout|rate_limit|malformed|cancelled|transport_error|none`, permille outside `1..50`, and all debug player verbs before bootstrap. `agent.zone.start live` rejects any provider/model other than `anthropic`/`claude-sonnet-5` and never prints configuration secrets.

- [ ] **Step 4: Add the engine lifecycle wrapper without adding provider waits**

Use this engine-facing contract:

```cpp
// AgentZoneRuntime.h
#pragma once
#include "EngineAPI.h"
#include <cstdint>
#include <string>

class ENGINE_API CAgentZoneRuntime
{
public:
    static void Initialize();
    static void Destroy();
    static void OnFrame();
    static std::string HandleBridgeVerb(const std::string& verb,
        const std::string& payload, bool& ok);
    static void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    static void NotifyDeath(uint16_t targetId, uint16_t sourceId);
};
```

Include `xrSim/xrSimMaterialization.h` from `IGame_Level.h` and add `public xrSim::IAgentZoneLiveAdapter` to `IGame_Level` inheritance. Task 7’s default value-return methods keep the engine buildable until `CLevel` overrides them in Task 9. Initialize `CAgentZoneRuntime` immediately before `CAgentBridge::Initialize()`. In destruction call `CAgentBridge::Destroy()` first so no new verbs arrive, then `CAgentZoneRuntime::Destroy()` to cancel/join workers before console/device teardown. In `CRenderDevice::ProcessFrame`, call `CAgentZoneRuntime::OnFrame()` immediately after `FrameMove()` and before AgentBridge request handling. Route `agent.zone.*` to `CAgentZoneRuntime::HandleBridgeVerb` before the generic `xrSim::HandleBridgeVerb` branch.

- [ ] **Step 5: Build and run pure runtime plus engine regression tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests xr_3da
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: C++ binaries exit `0`, Python tests report `OK`, and `xr_3da` links with no new unresolved xrGame symbol from xrEngine.

- [ ] **Step 6: Commit the orchestration runtime**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/AgentZoneRuntime.h src/xrEngine/AgentZoneRuntime.cpp src/xrEngine/IGame_Level.h src/xrEngine/AgentBridge.cpp src/xrEngine/device.cpp src/xrEngine/x_ray.cpp src/xrEngine/xrSim/xrSimZoneRuntime.h src/xrEngine/xrSim/xrSimZoneRuntime.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: orchestrate the AI zone authority spine"
```

---

### Task 9: CLevel live executor, combat handoff, and player-event ingress

**Files:**
- Modify: `src/xrGame/Level.h`
- Create: `src/xrGame/Level_agent_zone.cpp`
- Modify: `src/xrGame/entity_alive.cpp`
- Modify: `src/xrGame/CMakeLists.txt`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`

**Interfaces:**
- Consumes: `IAgentZoneLiveAdapter`, bindings, commands, and `CAgentZoneRuntime::{NotifyHit,NotifyDeath}`.
- Produces: real object IDs, real movement objectives, deterministic combat release/reacquisition observations, debug player placement/facing, and actual player-sourced bounded damage.

- [ ] **Step 1: Correct the adapter contract so commands carry the stable-to-live binding**

Change Task 7’s signature everywhere, including `FakeLiveAdapter`, before writing game code:

```cpp
virtual LiveAdapterResult ApplyObjective(const LiveBinding& binding,
    const LiveObjectiveCommand& command)
{ (void)binding; (void)command; return {false, "live_adapter_unavailable"}; }
virtual LiveAdapterResult FacePlayerAt(const LiveBinding& binding)
{ (void)binding; return {false, "live_adapter_unavailable"}; }
```

Update `Materializer::ApplyCommittedState` to pass its matching binding. Add a focused fake-adapter assertion that the IDs received by `ApplyObjective` equal the IDs returned by `SpawnGroup`.

- [ ] **Step 2: Build and run to prove the contract refactor is internally consistent**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: build and test exit `0` before adding the xrGame implementation.

- [ ] **Step 3: Declare all CLevel live-adapter overrides**

Task 8 already made `IGame_Level` inherit the default value-return adapter. Declare these concrete `CLevel` overrides in `Level.h`:

```cpp
xrSim::ReadPositionResult CapturePlayerOrigin() override;
xrSim::LiveAdapterResult ValidateSection(const std::string& section) override;
xrSim::SpawnGroupResult SpawnGroup(const xrSim::LiveSpawnRequest& request) override;
xrSim::LiveAdapterResult DestroyObjects(const std::vector<uint16_t>& objectIds) override;
xrSim::LiveAdapterResult ApplyObjective(const xrSim::LiveBinding& binding,
    const xrSim::LiveObjectiveCommand& command) override;
xrSim::ObserveGroupResult ObserveGroup(const xrSim::LiveBinding& binding) override;
xrSim::LiveAdapterResult ReleaseObjective(const xrSim::LiveBinding& binding) override;
xrSim::LiveAdapterResult SetPlayerAt(const xrSim::FixedPosition& position) override;
xrSim::LiveAdapterResult FacePlayerAt(const xrSim::LiveBinding& binding) override;
xrSim::LiveAdapterResult DamageGroupFromPlayer(const xrSim::LiveBinding& binding,
    uint32_t damagePermille) override;
```

- [ ] **Step 4: Implement exact live execution behavior in `Level_agent_zone.cpp`**

`ValidateSection` must call `pSettings->section_exist` and require a `class` line. `SpawnGroup` must resolve the nearest valid level-graph vertex for the fixed anchor, use the existing `F_entity_Create`/server spawn path for each member of one complete group, and return the resulting `u16` IDs; runtime bootstrap invokes at most one group spawn per tick. Any partial spawn returns `ok=false` and includes already-created IDs; `DestroyObjects` marks those objects for normal network destruction so a retry cannot duplicate them.

For each bound ID, resolve `CGameObject` then `CScriptEntity`. Apply the strategic objective with this shape:

```cpp
if (scriptEntity->GetCurrentEnemy())
{
    if (scriptEntity->GetScriptControl() &&
        xr_strcmp(scriptEntity->GetScriptControlName(), "xrSim") == 0)
    {
        scriptEntity->ClearActionQueue();
        scriptEntity->SetScriptControl(false, shared_str("xrSim"));
    }
    continue;
}

scriptEntity->SetScriptControl(true, shared_str("xrSim"));
scriptEntity->ClearActionQueue();
CScriptEntityAction action;
Fvector target;
target.set(command.target.xCm / 100.f,
    command.target.yCm / 100.f, command.target.zCm / 100.f);
if (smart_cast<CBaseMonster*>(object))
{
    CScriptMovementAction movement(MonsterSpace::eMA_WalkFwd, &target, 2.f);
    action.SetAction(movement);
}
else
{
    CScriptMovementAction movement(MonsterSpace::eBodyStateStand,
        MonsterSpace::eMovementTypeWalk, DetailPathManager::eDetailPathTypeSmooth,
        &target, 1.f);
    action.SetAction(movement);
}
scriptEntity->AddAction(&action, true);
```

Normal X-Ray AI is authoritative whenever an enemy exists. `ObserveGroup` reports `inCombat=true` if any member has a current enemy. Materializer releases script control on combat entry, retains the same committed strategic plan, and reacquires only after 5,000 ms continuously calm. It never asks Sonnet for a frame-critical combat action.

`SetPlayerAt` resolves a valid level vertex and calls the current actor’s existing `CScriptGameObject::SetActorPosition`, which uses `CActor::ForceTransform`, only for AgentBridge debugging. `FacePlayerAt` computes yaw with `atan2` from actor to the first live group member and calls `CScriptGameObject::SetActorDirection` so the active camera faces the target. `DamageGroupFromPlayer` chooses the first living member and submits one valid `SHit` with current actor as `who`, wound hit type, and power equal to the requested permille of current maximum health; cap it so the debug hit cannot reduce a full-health target below 900 permille. This is a real gameplay hit and must pass through normal relation/combat handling.

- [ ] **Step 5: Add hit and death notifications after normal gameplay processing**

In `CEntityAlive::Hit`, after `inherited::Hit(&HDS)` and relation registration, notify only when `HDS.who` is non-null:

```cpp
if (HDS.who)
{
    const uint32_t damagePermille =
        static_cast<uint32_t>(clampr(HDS.damage() * 1000.f, 0.f, 1000.f));
    CAgentZoneRuntime::NotifyHit(ID(), HDS.who->ID(), damagePermille);
}
```

In `CEntityAlive::Die`, call `NotifyDeath(ID(), who ? who->ID() : u16(-1))` after `inherited::Die(who)`. The engine wrapper pushes these tiny POD events into a bounded inbox and returns immediately. The runtime ignores IDs outside Gate 1 bindings; when a bound anonymous member is first touched by the player, call `ZoneState::PromoteSalientActor(parentGroup, objectId)` so later episodes keep a stable canonical identity while the materialization binding retains the current live ID. It classifies a current-actor source as `player_damaged_group`/`player_killed_member`, appends the observed event first, then wakes the owning Squad/Pack, Region, and Faction/Ecology minds asynchronously.

- [ ] **Step 6: Build the full game and run non-live regressions**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests xr_3da
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: all commands succeed; compilation verifies the exact `CScriptEntityAction`, monster, stalker, hit, and level-graph APIs against this fork.

- [ ] **Step 7: Commit the bounded live executor**

```bash
git add src/xrEngine/xrSim/xrSimMaterialization.h src/xrEngine/xrSim/xrSimMaterialization.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp src/xrGame/Level.h src/xrGame/Level_agent_zone.cpp src/xrGame/entity_alive.cpp src/xrGame/CMakeLists.txt
git commit -m "feat: execute AI objectives in the live Zone"
```

---

### Task 10: Fixed Garbage scenario, full agent hierarchy, and charters

**Files:**
- Create: `tools/ai_garbage_gate1.xrsim`
- Modify: `src/xrEngine/xrSim/xrSimZoneManifest.h`
- Modify: `src/xrEngine/xrSim/xrSimZoneManifest.cpp`
- Modify: `src/xrEngine/xrSim/xrSimWorldAgents.h`
- Modify: `src/xrEngine/xrSim/xrSimWorldAgents.cpp`
- Modify: `src/xrEngine/xrSim/xrSimZoneRuntime.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`

**Interfaces:**
- Consumes: manifest/state/agent/runtime contracts from earlier tasks, including `CreateWorldAgents(const ZoneManifest&)`.
- Produces: one exact file-backed scenario with every participating scope and charter explicitly declared.

- [ ] **Step 1: Write a failing file-backed scenario and hierarchy test**

```cpp
#include <fstream>
#include <sstream>

bool TestFileManifestCreatesEveryGateOneMind()
{
    std::ifstream input("tools/ai_garbage_gate1.xrsim");
    if (!Expect(input.good(), "Gate 1 manifest file opens"))
        return false;
    std::ostringstream text;
    text << input.rdbuf();
    const xrSim::ZoneManifestParseResult parsed = xrSim::ParseZoneManifest(text.str());
    if (!Expect(parsed.ok, "file manifest parses"))
        return false;
    const std::vector<xrSim::WorldAgentRecord> agents = xrSim::CreateWorldAgents(parsed.manifest);
    return Expect(agents.size() == 9, "all Gate 1 minds created") &&
        Expect(agents.front().agentId == 100, "director order stable") &&
        Expect(agents.back().scope.kind == xrSim::AgentScopeKind::MutantPack,
            "pack mind present") &&
        Expect(!agents.back().charter.empty(), "versioned charter resolved") &&
        Expect(parsed.manifest.groups.size() == 3, "three participating groups fixed");
}
```

- [ ] **Step 2: Run to verify the version-controlled scenario is absent**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: the test prints `FAIL: Gate 1 manifest file opens` and exits `1`.

- [ ] **Step 3: Create the exact version-controlled Gate 1 manifest**

```text
xrsim_zone_manifest_v1
scenario=garbage_gate1
save=radik zagirov - to garbage
level=l02_garbage
seed=14072026
origin=player
anchor=observer,0,0,-6000
anchor=stalker_camp,3500,0,0
anchor=stalker_border,2500,0,500
anchor=bandit_camp,-3500,0,0
anchor=bandit_border,-2500,0,500
anchor=scrapyard_center,0,0,4500
anchor=mutant_den,0,0,6500
anchor=retreat,0,0,-5000
faction=stalker,101
faction=bandit,102
group=stalker_squad,squad,stalker,sim_default_stalker_0,3,stalker_camp,201
group=bandit_squad,squad,bandit,sim_default_bandit_0,3,bandit_camp,202
group=garbage_dogs,mutant_pack,ecology,dog_weak,4,mutant_den,203
resource=scrap_cache,scrap,scrapyard_center,40,5
resource=food_cache,food,mutant_den,24,4
route=stalker_to_scrap,stalker_camp,scrapyard_center
route=bandit_to_scrap,bandit_camp,scrapyard_center
route=den_to_scrap,mutant_den,scrapyard_center
route=west_border,bandit_border,bandit_camp
agent=100,zone,1,zone_director,zone_director_v1
agent=101,faction,1,stalker_faction,stalker_faction_v1
agent=102,faction,2,bandit_faction,bandit_faction_v1
agent=103,ecology,1,garbage_ecology,ecology_v1
agent=104,economy,1,garbage_economy,economy_v1
agent=105,region,1,garbage_region,region_v1
agent=201,squad,1,stalker_squad,squad_v1
agent=202,squad,2,bandit_squad,squad_v1
agent=203,mutant_pack,3,garbage_dogs,pack_v1
```

On bootstrap, validate all three sections against live settings before spawning anything. If either human section is unavailable in this CoC installation, return `invalid section: <name>` and stop the gate; replace the manifest value only after discovering and manually spawn-proving the equivalent `sim_default_*` section through the same `ValidateSection` path.

- [ ] **Step 4: Verify file and inline fixtures resolve identical hierarchy contracts**

Add an assertion that the file and inline fixtures produce the same ordered agent IDs, scope IDs, and charter hashes, while permitting the file to contain the additional anchors, resource, and routes required by the live scenario.

- [ ] **Step 5: Register every scope and perform the explicit cutover**

At bootstrap, create all nine persistent agents and authority records in Observe at canonical revision `1`. `agent.zone.shadow all` transitions them in sorted `ScopeId` order at one tick boundary and advances canonical revision to `2`. `agent.zone.transfer all` verifies bindings for the three entity scopes plus canonical identities for the six strategic scopes, reconciles revision/identity, enters TransferPending for all records, and commits AiOwned for all records atomically at canonical revision `3`; if any validation fails, leave every scope in Shadow at revision `2`. Faction objectives are canonical strategic objectives whose subject is the faction; Squad/Pack objectives are live-materialized objectives whose subject is a group. Parent commits schedule proposal/pressure wakes but never directly mutate a child scope.

- [ ] **Step 6: Run manifest, hierarchy, authority, and full build tests**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xr_3da
./bin/arm64/Release/xrSimAuthoritySpineTests
```

Expected: exit `0`; the file manifest digest matches its in-test fixture, nine minds exist, and all-or-nothing transfer is covered.

- [ ] **Step 7: Commit the fixed scenario and hierarchy**

```bash
git add tools/ai_garbage_gate1.xrsim src/xrEngine/xrSim/xrSimZoneManifest.h src/xrEngine/xrSim/xrSimZoneManifest.cpp src/xrEngine/xrSim/xrSimWorldAgents.h src/xrEngine/xrSim/xrSimWorldAgents.cpp src/xrEngine/xrSim/xrSimZoneRuntime.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp
git commit -m "feat: define the Garbage AI authority scenario"
```

---

### Task 11: Deterministic replay and injected-failure preflight

**Files:**
- Create: `src/xrEngine/xrSim/xrSimReplay.h`
- Create: `src/xrEngine/xrSim/xrSimReplay.cpp`
- Create: `tools/ai_recordings/garbage_gate1/001_stalker_faction.intent`
- Create: `tools/ai_recordings/garbage_gate1/002_bandit_faction.intent`
- Create: `tools/ai_recordings/garbage_gate1/003_mutant_pack.intent`
- Modify: `src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: manifest, recorded provider values, scheduler completions, intent commit, authority audit, ledger, runtime reset, and snapshots.
- Produces: `ReplayGateOne`, deterministic replay reports, and the mandatory fault-injection regression suite.

- [ ] **Step 1: Add exact recorded responses**

`001_stalker_faction.intent`:

```text
xrsim_world_intent_v1
request_id=1001
agent_id=101
scope=faction,1
base_revision=3
horizon_ticks=1200
priority=50
wake_reason=strategic_heartbeat
rationale=secure access to contested salvage
op=set_objective,stalker,hold,scrapyard_center,1200
```

`002_bandit_faction.intent`:

```text
xrsim_world_intent_v1
request_id=1002
agent_id=102
scope=faction,2
base_revision=4
horizon_ticks=1200
priority=55
wake_reason=strategic_heartbeat
rationale=contest the western salvage approach
op=set_objective,bandit,reinforce,bandit_border,1200
```

`003_mutant_pack.intent`:

```text
xrsim_world_intent_v1
request_id=1003
agent_id=203
scope=mutant_pack,3
base_revision=5
horizon_ticks=800
priority=60
wake_reason=food_pressure
rationale=forage near scrap while preserving the den
op=set_objective,garbage_dogs,hunt,scrapyard_center,800
```

- [ ] **Step 2: Write replay-order and failure-continuity tests**

```cpp
#include "xrSim/xrSimReplay.h"

bool TestReplayIsIndependentOfProviderCompletionOrder()
{
    const xrSim::ZoneManifest manifest = xrSim::ParseZoneManifest(kManifest).manifest;
    const std::vector<xrSim::RecordedWake> wakes = xrSim::GateOneRecordedWakesForTest();
    const xrSim::ReplayReport forward = xrSim::ReplayGateOne(manifest, wakes, {0, 1, 2});
    const xrSim::ReplayReport reversed = xrSim::ReplayGateOne(manifest, wakes, {2, 1, 0});
    return Expect(forward.ok && reversed.ok, "both replay schedules commit") &&
        Expect(forward.worldDigest == reversed.worldDigest, "world digest stable") &&
        Expect(forward.ledgerDigest == reversed.ledgerDigest, "ledger order stable");
}

bool TestEveryInjectedProviderFailureCoastsAndRecovers()
{
    const xrSim::InjectedProviderFault faults[] = {
        xrSim::InjectedProviderFault::Timeout,
        xrSim::InjectedProviderFault::RateLimit,
        xrSim::InjectedProviderFault::Malformed,
        xrSim::InjectedProviderFault::Cancelled,
        xrSim::InjectedProviderFault::TransportError,
    };
    for (const xrSim::InjectedProviderFault fault : faults)
    {
        const xrSim::FaultRecoveryReport report = xrSim::RunFaultRecoveryForTest(fault);
        if (!Expect(report.coasted, "fault coasts") ||
            !Expect(report.planPreservedUntilExpiry, "accepted plan preserved") ||
            !Expect(report.recoveredOnValidWake, "later valid wake recovers"))
            return false;
    }
    return true;
}

bool TestResetCancelsWorkAndTransferRejectsEveryLegacyObjective()
{
    const xrSim::ResetReport reset = xrSim::RunResetDuringInflightForTest();
    const xrSim::LegacyWriteReport legacy = xrSim::RunLegacyWritesAfterTransferForTest();
    return Expect(reset.completedUnderMs < 2000 && reset.outstanding == 0,
               "reset cancels and joins") &&
        Expect(legacy.attempted == legacy.rejected && legacy.applied == 0,
            "all legacy objective writes rejected");
}

bool TestSchedulerHasNoCumulativeSessionCallLimit()
{
    const xrSim::LongSessionReport report = xrSim::RunRecordedLongSessionForTest(200);
    return Expect(report.requested == 200 && report.completed == 200,
               "all bounded recorded calls finish") &&
        Expect(report.rejectedForLifetimeLimit == 0,
            "no cumulative call-limit path exists");
}
```

- [ ] **Step 3: Build to verify replay helpers are absent**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests
```

Expected: compilation fails on `xrSimReplay.h`.

- [ ] **Step 4: Implement deterministic replay and test reports**

```cpp
#pragma once
#include "xrSim/xrSimZoneRuntime.h"

namespace xrSim
{
struct RecordedWake { WorldWakeContext context; WorldProviderResult result; };
struct ReplayReport
{
    bool ok = false;
    std::string reason;
    std::string worldDigest;
    std::string ledgerDigest;
};
struct FaultRecoveryReport
{
    bool coasted = false;
    bool planPreservedUntilExpiry = false;
    bool recoveredOnValidWake = false;
};
struct ResetReport { uint32_t completedUnderMs = 0; uint32_t outstanding = 0; };
struct LegacyWriteReport { uint32_t attempted = 0; uint32_t rejected = 0; uint32_t applied = 0; };
struct LongSessionReport
{
    uint32_t requested = 0;
    uint32_t completed = 0;
    uint32_t rejectedForLifetimeLimit = 0;
};
std::vector<RecordedWake> GateOneRecordedWakesForTest();
ReplayReport ReplayGateOne(const ZoneManifest& manifest,
    const std::vector<RecordedWake>& wakes, const std::vector<size_t>& completionOrder);
FaultRecoveryReport RunFaultRecoveryForTest(InjectedProviderFault fault);
ResetReport RunResetDuringInflightForTest();
LegacyWriteReport RunLegacyWritesAfterTransferForTest();
LongSessionReport RunRecordedLongSessionForTest(uint32_t calls);
}
```

`ReplayGateOne` may receive completions in the supplied permutation, but it must insert them into the same dispatch-sequence buffer used by the runtime and commit only contiguous sequences. Each recorded intent is prepared against the revision produced by the previous dispatch. `RunFaultRecoveryForTest` begins with an accepted Hold plan, injects one fault, advances beyond the retry/circuit path, asserts that expiry deactivates the plan and leaves only the executor’s stationary continuity posture, then supplies one valid response and verifies a new commit. `RunRecordedLongSessionForTest` uses `maxConcurrent=3`, `maxQueue=64`, cooldown `0`, RPM `1000`, and pumps virtual minutes until all 200 recorded requests complete; it must not add a special bypass to production scheduling.

- [ ] **Step 5: Run the deterministic preflight under repetition**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests
for i in 1 2 3 4 5; do ./bin/arm64/Release/xrSimAuthoritySpineTests || exit 1; done
./bin/arm64/Release/xrSimWorldStateTests
```

Expected: all six executions exit `0`; reversed completion, fault recovery, cancellation, replay, authority, and 200-call tests remain stable.

- [ ] **Step 6: Commit the deterministic acceptance floor**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimReplay.h src/xrEngine/xrSim/xrSimReplay.cpp src/xrEngine/xrSim/tests/xrSimAuthoritySpineTests.cpp tools/ai_recordings/garbage_gate1/001_stalker_faction.intent tools/ai_recordings/garbage_gate1/002_bandit_faction.intent tools/ai_recordings/garbage_gate1/003_mutant_pack.intent
git commit -m "test: add AI zone replay and fault preflight"
```

---

### Task 12: Immortal-player 60-minute Gate 1 harness and acceptance report

**Files:**
- Create: `tools/ai_zone_soak.py`
- Create: `tools/tests/test_ai_zone_soak.py`

**Interfaces:**
- Consumes: AgentBridge, `tools/ai_garbage_gate1.xrsim`, `xr_3da`, runtime verbs/metrics, bridge state/screenshots, process RSS, game logs, ledger, authority audit, and snapshots.
- Produces: reproducible phase control, `artifacts/ai-zone/<timestamp>/`, stable `artifacts/ai-zone/latest` symlink, and machine-readable `acceptance.json`.

- [ ] **Step 1: Write failing harness parser and acceptance-analysis tests**

```python
import json
import tempfile
import unittest
from pathlib import Path

from tools.ai_zone_soak import (
    GatePhase,
    analyze_acceptance,
    parse_gate_manifest,
    phase_at,
)


class GateManifestTests(unittest.TestCase):
    def test_repo_manifest_has_exact_groups_and_seed(self):
        manifest = parse_gate_manifest(Path("tools/ai_garbage_gate1.xrsim"))
        self.assertEqual(14072026, manifest.seed)
        self.assertEqual("l02_garbage", manifest.level)
        self.assertEqual(
            ["stalker_squad", "bandit_squad", "garbage_dogs"],
            [group.name for group in manifest.groups],
        )


class PhaseTests(unittest.TestCase):
    def test_exact_sixty_minute_phase_boundaries(self):
        self.assertEqual(GatePhase.OBSERVE, phase_at(0))
        self.assertEqual(GatePhase.TRESPASS, phase_at(10 * 60))
        self.assertEqual(GatePhase.WARNING_SHOTS, phase_at(20 * 60))
        self.assertEqual(GatePhase.DAMAGE_AND_WITHDRAW, phase_at(25 * 60))
        self.assertEqual(GatePhase.AWAY, phase_at(30 * 60))
        self.assertEqual(GatePhase.RETURN, phase_at(45 * 60))
        self.assertIsNone(phase_at(60 * 60))


class AcceptanceTests(unittest.TestCase):
    def test_total_calls_and_tokens_are_reported_but_not_gate_failures(self):
        evidence = {
            "duration_sec": 3600,
            "process_exit": 0,
            "player_alive": True,
            "provider_wait_frame_us": 0,
            "frames_advanced_inflight": True,
            "simulation_advanced_inflight": True,
            "accepted_objective_scopes": ["faction:1", "faction:2", "mutant_pack:3"],
            "autonomous_consequence": True,
            "memory_return_response": True,
            "objective_trace_complete": True,
            "consequence_trace_complete": True,
            "legacy_applied_after_transfer": 0,
            "queue_within_bounds": True,
            "preflight_passed": True,
            "fatal_log_findings": [],
            "secret_findings": [],
            "requests": 100000,
            "input_tokens": 9000000,
            "output_tokens": 1000000,
        }
        report = analyze_acceptance(evidence)
        self.assertTrue(report["passed"])
        self.assertEqual(100000, report["usage"]["requests"])
```

- [ ] **Step 2: Run to verify the harness module is missing**

```bash
python3 -m unittest tools.tests.test_ai_zone_soak -v
```

Expected: import fails because `tools.ai_zone_soak` does not exist.

- [ ] **Step 3: Implement strict parsing, phase actions, sampling, and artifact collection**

The CLI is:

```text
python3 tools/ai_zone_soak.py \
  --game-dir "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl" \
  --binary bin/arm64/Release/xr_3da \
  --manifest tools/ai_garbage_gate1.xrsim \
  --artifacts artifacts/ai-zone \
  --duration-seconds 3600 \
  --live
```

Require `XRAY_AGENT_API_KEY` in the inherited environment for `--live`, but never read it into output, command arguments, transcript, or report. Before launching, run `bin/arm64/Release/xrSimAuthoritySpineTests`, capture stdout/stderr and exit status in `preflight.txt`, and refuse the live run when it fails. Then launch `xr_3da -agent_bridge appdata/agent_bridge.sock`, load the manifest save, wait for `scene=game level=l02_garbage`, and execute this startup exactly:

```text
cmd g_god on
agent.zone.bootstrap <absolute-manifest-path>
agent.zone.shadow all
agent.zone.transfer all
agent.zone.start live
agent.zone.player observer
```

Drive the uninterrupted wall-clock phases:

| Time | Required action |
|---|---|
| `00:00` | Screenshot `gate1_00_observe`; no player interference. |
| `10:00` | `agent.zone.player bandit_border`; screenshot `gate1_10_trespass`. |
| `20:00` | `agent.zone.player bandit_border`; `agent.zone.face bandit_squad`; send `mouse btn left tap` three times, two seconds apart; screenshot `gate1_20_warning`. |
| `25:00` | `agent.zone.damage bandit_squad 10`; screenshot `gate1_25_damage`; then `agent.zone.player retreat`. |
| `30:00` | Screenshot `gate1_30_away`; stay away. |
| `45:00` | `agent.zone.player bandit_border`; `agent.zone.face bandit_squad`; screenshot `gate1_45_return`. |
| `60:00` | Screenshot `gate1_60_complete`; collect final artifacts; `agent.zone.stop`; `cmd quit`. |

Every five seconds request bridge `state`, `agent.zone.status`, and `agent.zone.metrics`; sample process RSS; append monotonic timestamps and raw responses to `samples.jsonl`. Every five minutes collect `agent.zone.snapshot`. At every boundary and at shutdown collect `agent.zone.authority`, paginate `agent.zone.ledger <after-seq> 256` until `done=1`, and collect metrics. Record every bridge request/response in `bridge-transcript.jsonl`. On early failure, still collect every reachable artifact and write a failed report before terminating the process.

- [ ] **Step 4: Implement exact acceptance criteria and secret/log scans**

Write `acceptance.json` with `passed`, `criteria`, `evidence`, and `usage`. Every criterion contains `passed`, `summary`, and artifact references. Required criteria are:

```python
REQUIRED_CRITERIA = (
    "session_completed_60m",
    "player_survived",
    "provider_never_waited_on_frame_thread",
    "frames_and_simulation_advanced_inflight",
    "stalker_faction_accepted_objective",
    "bandit_faction_accepted_objective",
    "mutant_pack_accepted_objective",
    "autonomous_world_consequence",
    "memory_informed_return_response",
    "every_live_objective_traced_to_commit",
    "every_claimed_consequence_traced_to_observation",
    "zero_legacy_objectives_after_transfer",
    "request_storm_bounds_held",
    "deterministic_preflight_passed",
    "no_fatal_or_secret_findings",
)
```

`memory_informed_return_response` requires a pre-return `player_damaged_group` observed event in the bandit memory, a post-return wake referencing that event, and either a committed `hold|retreat|avoid|reinforce|hunt` response or live hostility/pursuit observation. `autonomous_world_consequence` requires a territory/resource/population/arrival event after an AI commit and with no player event in its causal links. `request_storm_bounds_held` checks max queue `<=64`, max inflight `<=3`, per-agent inflight `<=1`, retries `<=2` per request, and observed circuit/cooldown behavior when failures occur. Provider errors in the live run pass continuity only when the ledger shows coast and later valid recovery; absence of a spontaneous live provider error is neutral because preflight injection is mandatory.

Scan game logs, bridge transcript, snapshots, ledger, environment-independent process command, and reports for `sk-ant-`, the current key’s SHA-256 digest, `FATAL`, `GL_INVALID`, `lua runtime error`, `deadlock`, `authority_violation_applied`, and `conservation_drift`. Never write the key or its digest; use the digest only in memory for equality scanning. Total calls and tokens appear under `usage` and never create a criterion.

- [ ] **Step 5: Add dry-run and accelerated phase tests**

Implement `--dry-run` to validate the manifest, emit the planned bridge transcript and criterion skeleton, and never launch the game. `--test-mode` uses separate smoke criteria: successful bootstrap/transfer, advancing frame and simulation counters, explicit wakes for agents `101`, `201`, and `203`, at least one non-coast accepted Sonnet commit, usage metadata, clean shutdown, and no secret finding; it does not label Gate 1 passed. Implement `--phase-scale` only when `--dry-run` or `--test-mode` is present so unit tests can exercise every phase without weakening the live 3600-second requirement.

Run:

```bash
python3 -m unittest tools.tests.test_ai_zone_soak -v
python3 tools/ai_zone_soak.py --manifest tools/ai_garbage_gate1.xrsim --artifacts /tmp/openxray-ai-zone-dry --dry-run
```

Expected: tests report `OK`; dry run exits `0` and writes a report skeleton with all fifteen criterion names and exact phase actions.

- [ ] **Step 6: Commit the acceptance harness**

```bash
git add tools/ai_zone_soak.py tools/tests/test_ai_zone_soak.py
git commit -m "test: add immortal-player AI zone soak"
```

---

### Task 13: Full verification, live Sonnet gate, and handover

**Files:**
- Modify: `docs/HANDOVER.md`
- Runtime-generated, untracked: `artifacts/ai-zone/<timestamp>/`
- Runtime-generated, untracked: `artifacts/ai-zone/latest`

**Interfaces:**
- Consumes: all implementation tasks and a rotated Anthropic key supplied only in the process environment.
- Produces: a green deterministic suite, standard bridge soak, live non-coast proof, 60-minute Gate 1 report, visually checked screenshots, and exact handover commands.

- [ ] **Step 1: Run the full deterministic build and test matrix**

```bash
cmake --build build -j10 --target xrSimAuthoritySpineTests xrSimWorldStateTests xr_3da
./bin/arm64/Release/xrSimAuthoritySpineTests
./bin/arm64/Release/xrSimWorldStateTests
python3 -m unittest tools.tests.test_gl_macos_soak tools.tests.test_ai_zone_soak -v
```

Expected: every command exits `0`; Python reports `OK`.

- [ ] **Step 2: Run the existing AgentBridge GL regression gate**

Launch the GL runtime with `-agent_bridge`, load the normal test save, then run:

```bash
python3 tools/agentctl.py "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl/appdata/agent_bridge.sock" --script tools/bridge_soak.txt
```

Expected: movement, UI, screenshot, save/load, and quit complete; the log contains no new fatal, GL, Lua, or hang finding. This regression may use save/load; the Gate 1 live acceptance run in Step 4 may not.

- [ ] **Step 3: Prove a fresh live Sonnet response is accepted before spending one hour**

With `XRAY_AGENT_API_KEY` present only in the parent environment, run a five-minute live harness smoke using the same manifest and `--test-mode`, allowing the harness to request one Faction, one Squad, and one Pack wake. Require at least one response with `provider=anthropic`, `model=claude-sonnet-5`, `coast=0`, a valid usage count, and an accepted commit. If any manifest entity section fails validation, discover a valid equivalent with the live `ValidateSection` path, manually spawn-prove it once, update the version-controlled manifest, rerun deterministic tests, and recommit that manifest correction before continuing.

Run:

```bash
test -n "$XRAY_AGENT_API_KEY"
python3 tools/ai_zone_soak.py \
  --game-dir "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl" \
  --binary bin/arm64/Release/xr_3da \
  --manifest tools/ai_garbage_gate1.xrsim \
  --artifacts artifacts/ai-zone-smoke \
  --duration-seconds 300 \
  --test-mode --live
```

Expected: exit `0`, a non-coast accepted Sonnet commit, advancing frame/simulation counters during the request, and no secret finding.

- [ ] **Step 4: Run the uninterrupted 60-minute live acceptance**

```bash
test -n "$XRAY_AGENT_API_KEY"
python3 tools/ai_zone_soak.py \
  --game-dir "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl" \
  --binary bin/arm64/Release/xr_3da \
  --manifest tools/ai_garbage_gate1.xrsim \
  --artifacts artifacts/ai-zone \
  --duration-seconds 3600 \
  --live
```

Expected: exit `0`; `artifacts/ai-zone/latest/acceptance.json` has `"passed": true` and every required criterion is true. The single session lasts at least 3600 seconds, uses no save/load after startup, and the player remains alive under `g_god on`.

- [ ] **Step 5: Inspect every phase screenshot and cross-check report evidence**

Open the seven JPEGs under `artifacts/ai-zone/latest/screenshots/` and verify they are fresh, non-black, in-game Garbage frames showing the expected observer/trespass/warning/damage/away/return/final phases. Cross-check every report artifact reference exists, the authority audit contains all nine transfer commits and no applied legacy write, and every objective/consequence criterion points to actual ledger/event sequence IDs.

Expected: all seven screenshots are visually valid and every referenced sequence exists.

- [ ] **Step 6: Run a final secret and worktree audit**

```bash
rg -n --hidden --glob '!build/**' --glob '!bin/**' --glob '!artifacts/**' 'sk-ant-' .
git status --short
git diff --check
```

Expected: ripgrep prints no match; `git diff --check` prints nothing; only intentional task files plus the pre-existing user-owned `README.md` and `AGENTS.md` changes are visible before the documentation commit. Rotate any credential ever pasted into chat even when all scans are clean.

- [ ] **Step 7: Update the handover with stable commands and evidence locations**

Document:

- Gate 1 implementation status and the exact commit sequence;
- build/test commands from Step 1;
- private environment-only key setup, with no literal key;
- fixed manifest path and the `g_god on` debug contract;
- all `agent.zone.*` verbs and authority transition sequence;
- five-minute smoke and 60-minute soak commands;
- stable report path `artifacts/ai-zone/latest/acceptance.json`;
- what each acceptance criterion proves;
- Gate 2 remains cross-level continuity and is outside this plan.

- [ ] **Step 8: Re-run documentation-sensitive tests and commit the handover**

```bash
python3 -m unittest tools.tests.test_ai_zone_soak -v
git diff --check
git add docs/HANDOVER.md
git commit -m "docs: hand off the Garbage AI authority gate"
```

Expected: tests report `OK`, diff check is clean, and only `docs/HANDOVER.md` is staged for this commit.

- [ ] **Step 9: Apply completion skills before claiming Gate 1**

Use `superpowers:verification-before-completion` against the exact outputs from Steps 1–6, then `superpowers:requesting-code-review` over the full Gate 1 commit range. Resolve any correctness finding through `superpowers:receiving-code-review`, rerun the affected tests and the acceptance criterion when behavior changes, and use `superpowers:finishing-a-development-branch` only after the 60-minute report remains green.

Gate 1 may be called complete only with current command output and a green `artifacts/ai-zone/latest/acceptance.json`; the existence of code, unit tests, or a prior shorter smoke is insufficient.
