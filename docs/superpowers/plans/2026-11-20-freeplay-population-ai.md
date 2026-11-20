# Freeplay Population AI (Cordon, in-session) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every living stalker and mutant in a CoC freeplay session gets exactly one objective owner (squad, pack or lone NPC), and an accepted owner decision visibly moves the real NPCs in Cordon, first from recorded decisions and then from autonomous model wakes.

**Architecture:** `xrSimCore` gains a freeplay layer: an owner registry fed by population snapshots, a decision validator with a ledger, and a wake scheduler on top of the existing `AsyncActorProviderQueue`. A CoC Lua adapter (`xrsim_freeplay.script`) is the only game-side piece. It reports population and observations through one new Lua function, `xrsim_verb`, and carries accepted objectives out through CoC's existing `squad.scripted_target` hook, which bypasses the stock simulation target picker for that squad. No stock CoC script is edited.

**Tech Stack:** C++ (`xrSimCore` portable C++17 subset, no exceptions), luabind/Lua C API in `xrEngine`, CoC 1.4.22 Lua (`axr_main` callbacks), Python 3 `unittest` for tools, agent bridge (`tools/agentctl.py`).

**Spec:** [docs/superpowers/specs/2026-11-20-freeplay-population-ai-design.md](../specs/2026-11-20-freeplay-population-ai-design.md)

**Scope of this plan:** spec delivery stages 1–3 (census and ownership, recorded decisions, autonomous wakes) in one play session on Cordon. Stage 4 (objectives and identities persisted in the save, the saved activation setting, the Cordon–Garbage crossing, Zone-wide coverage, companion follow/wait persistence) is a follow-up plan written after Task 6 records the real population and bindings. Until then: activation is the launch flag `-xrsim_freeplay`, and every load re-enrolls the population from the live world and drops earlier objectives.

## Global Constraints

- `xrSimCore` code stays a portable C++17 subset with no exceptions. Failures are values (`ok` + `reason`), validated in full before any mutation.
- No render or simulation frame waits on a provider. The tick only calls non-blocking `Submit` and `Poll`.
- Without `-xrsim_freeplay` the adapter registers no callbacks and the game behaves as stock CoC.
- Exactly one writer per transferable owner: once an owner is enrolled, its destination comes only from `scripted_target` set by the adapter.
- Stock CoC scripts are not modified. The adapter uses `RegisterScriptCallback` and fields the stock scripts already read (`scripted_target`, `__lock`).
- Model policy: `claude-sonnet-5` is unverified and no substitution is approved. Task 7 does not start until the user names the model ID and provides the key.
- New engine sources go into `src/xrEngine/CMakeLists.txt`, `src/xrEngine/xrEngine.vcxproj` and `xrEngine.vcxproj.filters`.
- Live runs use a private cloned runtime with its own appdata and `-agent_background`. Never run tests in `build/runtime/play`.
- Commit locally on the working branch after each task. Do not push.
- Match the style of the file being edited. Log lines use `Msg("* ...")`, `Msg("! ...")`, `Msg("~ ...")`; the adapter uses `printf` with the prefix `xrsim_freeplay:`.

## Review Focus

1. A squad dies and a new squad receives the same server ID before the next sync: the newcomer must start with a new owner and no inherited objective (Task 1 test `TestReusedGroupIdGetsNewOwner`).
2. A model reply arrives after the player loads another save: it must be rejected and write nothing (Task 4 test `TestReplyAfterBeginWorldIsStale`).
3. The provider is missing, unreachable or failing during live mode: owners keep their last objective, the ledger shows a hold, wakes back off, and activation with no key is refused with a readable reason (Task 3 test `TestProviderFailureBacksOff`, Task 4 test `TestLiveModeRefusedWithoutProvider`).
4. A chosen smart terrain becomes unavailable between decision and application: the adapter keeps the previous target and reports `blocked`, and the owner is woken again (Task 4 test `TestBlockedEventRewakesOwner`, Task 6 live step).
5. The player's companions and story squads with scripted posts: the adapter never writes their destination and the model is never asked (Task 2 test `TestConstrainedOwnerOnlyStays`, Task 3 test `TestConstrainedOwnersAreNotWoken`, Task 6 census check).

---

## File Structure

| File | Responsibility |
|---|---|
| `src/xrEngine/xrSim/xrSimFreeplay.h/.cpp` | Owner registry: snapshot parsing, identity, membership transitions, census text |
| `src/xrEngine/xrSim/xrSimFreeplayDecision.h/.cpp` | Observation parsing, wake prompt, decision validation, ledger |
| `src/xrEngine/xrSim/xrSimFreeplayScheduler.h/.cpp` | Wake coalescing, fairness, concurrency bound, backoff |
| `src/xrEngine/xrSim/xrSimFreeplayBridge.h/.cpp` | `ai.freeplay.*` verbs and the runtime that ties the three together |
| `src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp` | Library tests for all four units |
| `src/xrEngine/xrSimScript.h`, `src/xrEngine/xrSim_script.cpp` | Lua function `xrsim_verb` |
| `res/coc/gamedata/scripts/xrsim_freeplay.script` | CoC adapter |
| `tools/freeplay_runtime.py`, `tools/tests/test_freeplay_runtime.py` | Clone a private test runtime; install the adapter |
| `tools/freeplay_probe.py`, `tools/tests/test_freeplay_probe.py` | Census and recorded-decision experiments over the bridge |

Reference reading for implementers: `src/xrEngine/xrSim/xrSimPackSpawn.cpp` (registry and wake style), `xrSimActorProvider.h` (`AsyncActorProviderQueue`), `xrSimBridge.cpp` (verb dispatch), `src/xrEngine/key_binding_registrator_script.cpp` (script export with raw Lua API). CoC scripts are packed in `database/config/scripts.db`; unpack them to a scratch directory outside the repo with `python3 -I tools/xrdb_unpack.py <runtime>/database/config/scripts.db <scratch> scripts/` and read `sim_squad_scripted.script` (`get_script_target`, `specific_update`, `generic_update`), `sim_board.script`, `axr_main.script`, `axr_companions.script`.

---

### Task 1: Owner registry

**Files:**
- Create: `src/xrEngine/xrSim/xrSimFreeplay.h`, `src/xrEngine/xrSim/xrSimFreeplay.cpp`
- Create: `src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt` (add both files to `xrSimCore`; add `xrSimFreeplayTests` like `xrSimAuthoritySpineTests`), `src/xrEngine/xrEngine.vcxproj`, `src/xrEngine/xrEngine.vcxproj.filters`, `tools/xrsim_standalone/CMakeLists.txt` (test list), `tools/xrsim_standalone/build_and_test.sh` (test loop)

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
namespace xrSim
{
enum class FreeplaySpecies { Stalker, Mutant };
enum class FreeplayOwnerKind { Squad, Pack, LoneStalker, LoneMutant };
enum class FreeplayBinding { Group, Npc };   // squad server object, or an NPC with no squad
enum class FreeplayWriter { Sim, Script, Random, Companion, Hostage, None };
enum class FreeplayObjectiveSource { None, Bootstrap, Inherited, Recorded, Live, Hold };

struct FreeplayMember { uint16_t objectId = 0; std::string section; bool leader = false; };
struct FreeplayObjective
{
    std::string verb = "hold";   // hold | stay | move_to | retreat
    std::string site = "-";
    FreeplayObjectiveSource source = FreeplayObjectiveSource::None;
    uint32_t revision = 0;
};
struct FreeplayOwner
{
    uint64_t ownerId = 0;
    FreeplayBinding binding = FreeplayBinding::Group;
    uint16_t bindingId = 0;
    std::string name, faction, level, site;
    FreeplaySpecies species = FreeplaySpecies::Stalker;
    FreeplayOwnerKind kind = FreeplayOwnerKind::Squad;
    FreeplayWriter writer = FreeplayWriter::Sim;
    bool online = false;
    std::vector<FreeplayMember> members;
    uint32_t membershipRevision = 1;
    FreeplayObjective objective;
    std::vector<std::string> memories;   // newest last, at most 8
};
struct FreeplayResult { bool ok = false; std::string reason; };

const char* FreeplayKindName(FreeplayOwnerKind);       // squad pack lone_stalker lone_mutant
const char* FreeplayWriterName(FreeplayWriter);        // sim script random companion hostage none
const char* FreeplaySourceName(FreeplayObjectiveSource); // none bootstrap inherited recorded live hold
bool IsFreeplayTransferable(const FreeplayOwner&);     // binding == Group && writer == Sim

class FreeplayPopulation
{
public:
    uint32_t BeginWorld();                 // epoch + 1, drops every owner, returns the new epoch
    uint32_t Epoch() const;
    FreeplayResult Sync(const std::string& snapshot);
    FreeplayResult Retire(FreeplayBinding binding, uint16_t bindingId);
    FreeplayResult SetObjective(uint64_t ownerId, const std::string& verb, const std::string& site,
        FreeplayObjectiveSource source);   // revision + 1
    FreeplayResult AddMemory(uint64_t ownerId, const std::string& token);
    const FreeplayOwner* FindOwner(uint64_t ownerId) const;
    const FreeplayOwner* FindByBinding(FreeplayBinding binding, uint16_t bindingId) const;
    const FreeplayOwner* OwnerOfNpc(uint16_t objectId) const;
    const std::vector<FreeplayOwner>& Owners() const;
    std::vector<uint64_t> TakeChangedOwners();   // created or membership changed since the last call
    std::string FormatCensus(const std::string& levelFilter) const;
};
} // namespace xrSim
```

Snapshot text. Records are separated by `\n` or `;`. Every field is one token without spaces; `-` means none.

```
xrsim_freeplay_sync_v1
group <group_id> <name> <stalker|mutant> <faction> <level> <site|-> <target|-> <writer> <online 0|1>
member <group_id> <object_id> <section> <leader 0|1>
npc <object_id> <section> <stalker|mutant> <faction> <level>
end
```

Rules the tests pin:
- A snapshot is the full living population. An owner whose binding is absent from it is retired.
- `Sync` rejects the whole snapshot, with no mutation, on: missing header or `end`, unknown record, bad number, duplicate group ID, duplicate object ID, a `member` naming an unknown group, a group with no members, more than 64 members in a group, more than 4096 groups.
- Owner IDs start at 1, increase by 1 and are never reused, including across `BeginWorld`.
- `kind` is recomputed on every sync: one living member gives `LoneStalker`/`LoneMutant`, more gives `Squad`/`Pack`. An `npc` record gives a `Binding::Npc` owner with `writer = None`.
- A new group starts with `objective = {stay or move_to, target, Bootstrap, revision 1}` (`stay` when `target == site`), or the default hold objective when `target` is `-`.
- A member set change raises `membershipRevision` by 1 and leaves `ownerId` and `objective` alone.
- Split: a new group whose members all belonged, before this sync, to one other owner copies that owner's verb and site with source `Inherited`.
- An NPC that moves into an existing group gets that group's objective simply by membership. Its old owner retires when it has no members left.
- Census, one line per NPC then one summary line:

```
npc <object_id> <section> owner=<owner_id> kind=<kind> binding=<group|npc>:<id> writer=<writer> level=<level> objective=<verb>:<site> source=<source> rev=<n>
summary npcs=<n> owners=<n> transferable=<n> script_owned=<n> unbound=<n>
```

`script_owned` counts group owners whose writer is not `sim`; `unbound` counts `Binding::Npc` owners. A non-empty `levelFilter` keeps only that level.

- [ ] **Step 1: Write the failing tests** in `xrSimFreeplayTests.cpp`, using the `Expect` helper and `main` layout of `xrSimAuthoritySpineTests.cpp`.

```cpp
const char* kTwoGroups =
    "xrsim_freeplay_sync_v1\n"
    "group 500 esc_stalkers stalker stalker l01_escape esc_smart_a esc_smart_b sim 1\n"
    "member 500 601 sim_default_stalker_1 1\n"
    "member 500 602 sim_default_stalker_0 0\n"
    "group 510 esc_dogs mutant monster_predatory_day l01_escape - - sim 0\n"
    "member 510 611 dog_weak 1\n"
    "npc 700 esc_m_trader stalker trader l01_escape\n"
    "end\n";

bool TestSyncEnrollsEveryNpc();          // 3 owners; OwnerOfNpc(601)==OwnerOfNpc(602); kinds squad, lone_mutant, lone_stalker;
                                         // group 500 objective move_to esc_smart_b source bootstrap; group 510 hold source none;
                                         // census summary == "summary npcs=4 owners=3 transferable=2 script_owned=0 unbound=1"
bool TestSyncIsAtomicOnBadRecord();      // each rejected case above: !ok and FormatCensus("") unchanged
bool TestLeaderLossKeepsOwnerAndObjective(); // resync without 601: same ownerId, kind lone_stalker, membershipRevision 2, objective unchanged
bool TestReusedGroupIdGetsNewOwner();    // Retire(Group,500), sync same id with other members: new ownerId, objective source bootstrap or none, never the old site
bool TestAbsentGroupIsRetired();         // resync without group 510: FindByBinding(Group,510)==nullptr
bool TestSplitInheritsObjective();       // SetObjective(owner500,"move_to","esc_smart_c",Recorded); resync 500={601}, new 520={602}:
                                         // owner 520 objective move_to esc_smart_c source inherited; owner 500 membershipRevision 2
bool TestJoinAdoptsGroupObjective();     // lone 510 member 611 moves into 500: OwnerOfNpc(611)==owner 500; owner 510 retired
bool TestBeginWorldDropsOwnersAndAdvancesEpoch(); // Owners().empty(); epoch +1; next ownerId greater than any earlier one
bool TestChangedOwnersReportedOnce();    // after first sync returns 3 ids; second call empty; after leader loss returns owner 500 only
bool TestCensusLevelFilter();            // FormatCensus("l02_garbage") has summary npcs=0
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: build error, `xrSim/xrSimFreeplay.h` not found.

- [ ] **Step 3: Implement `xrSimFreeplay.h/.cpp`** to the interface and rules above. Parse the whole snapshot into a staging structure, validate, then swap.

- [ ] **Step 4: Run to verify pass**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: `== xrSimFreeplayTests` followed by `exit=0`, no `FAIL:` lines, and the two existing test binaries still exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/xrSim/xrSimFreeplay.* src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp src/xrEngine/CMakeLists.txt src/xrEngine/xrEngine.vcxproj src/xrEngine/xrEngine.vcxproj.filters tools/xrsim_standalone
git commit -m "feat(xrsim): freeplay owner registry for squads, packs and lone NPCs"
```

---

### Task 2: Observation, decision validation and ledger

**Files:**
- Create: `src/xrEngine/xrSim/xrSimFreeplayDecision.h`, `src/xrEngine/xrSim/xrSimFreeplayDecision.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp`, `src/xrEngine/CMakeLists.txt`, `src/xrEngine/xrEngine.vcxproj`, `src/xrEngine/xrEngine.vcxproj.filters`

**Interfaces:**
- Consumes: Task 1 types; `ActorWakeContext`, `ActorIntentPlan`, `ActorAgentRecord` from the existing xrSim headers.
- Produces:

```cpp
namespace xrSim
{
struct FreeplaySite { std::string name; uint32_t distanceMeters = 0; std::string kind; uint32_t occupants = 0; };
struct FreeplaySighting { std::string name, species, faction, relation; uint32_t count = 0; uint32_t distanceMeters = 0; };
struct FreeplayObservation
{
    uint64_t ownerId = 0;
    uint32_t epoch = 0;
    uint32_t membershipRevision = 0;
    std::string situation;
    std::vector<FreeplaySite> sites;          // at most 8
    std::vector<FreeplaySighting> sightings;  // at most 6
};
struct FreeplayObservationParse { bool ok = false; std::string reason; FreeplayObservation observation; };
struct FreeplayVerdict { bool ok = false; bool keep = false; std::string reason, verb, site; std::vector<std::string> memories; };
struct FreeplayLedgerEntry
{
    uint64_t serial = 0, ownerId = 0, queueAgeMs = 0, latencyMs = 0;
    uint32_t epoch = 0, membershipRevision = 0;
    std::string wakeReason, source, verdict, verb, site, provider, model;
};

FreeplayObservationParse ParseFreeplayObservation(const FreeplayPopulation& population, const std::string& text);
ActorWakeContext BuildFreeplayWake(const FreeplayOwner& owner, const FreeplayObservation& observation,
    const std::string& wakeReasons);
FreeplayVerdict ValidateFreeplayDecision(const FreeplayPopulation& population,
    const FreeplayObservation& observation, const ActorIntentPlan& plan);

class FreeplayLedger
{
public:
    void Append(FreeplayLedgerEntry entry);          // assigns serial; keeps the newest 512
    std::string Format(size_t maxEntries) const;     // one line per entry, key=value fields named as the struct members
    size_t Size() const;
};
} // namespace xrSim
```

Observation text (same separators as the snapshot):

```
xrsim_freeplay_observation_v1
owner <owner_id> <membership_revision>
situation <token>
site <name> <distance_m> <kind> <occupants>
sighting <name> <stalker|mutant> <faction> <count> <distance_m> <enemy|neutral|friend>
end
```

`ParseFreeplayObservation` stamps `epoch` from `population.Epoch()` and fails with `unknown_owner`, `stale_membership` (revision differs from the owner's), `too_many_sites`, `too_many_sightings` or `malformed`.

`ValidateFreeplayDecision` reasons, checked in this order: `unknown_owner`; `stale_world` (epoch differs); `stale_membership`; `coast` plan gives `ok`, `keep = true`; any action other than `stay`, `move_to`, `retreat`, `author_memory` gives `illegal_tool`; not exactly one of the three movement verbs gives `need_one_objective`; a non-transferable owner choosing anything but `stay` gives `constrained`; `stay` with owner site `-` gives `no_site`; `move_to`/`retreat` whose target is not in `observation.sites` gives `unknown_site`. On success `verb` and `site` are set (`stay` uses the owner's site) and `memories` carries the plan's memories.

`BuildFreeplayWake` fills `context.actor` (`agentId` = low 32 bits of the owner ID, scope `Squad` for stalkers and `MutantPack` for mutants, `name`), `context.observation` and `context.prompt`. The prompt starts with `xrsim_freeplay_wake_v1`, lists kind, faction, level, member count, current site, current objective with its source, memories, wake reasons, every site and sighting, then the response rules. Reuse the response grammar lines of `BuildActorWakePrompt` and replace the tool lines with `legal_tools stay move_to retreat author_memory`, `response_record action stay`, `response_record action move_to <site_name>`, `response_record action retreat <site_name>`, `site_name_must_be_listed`.

- [ ] **Step 1: Write the failing tests**

```cpp
bool TestObservationParseAndLimits();     // valid text parses; 9 sites -> too_many_sites; wrong revision -> stale_membership
bool TestWakePromptListsSitesAndRules();  // prompt contains "xrsim_freeplay_wake_v1", each site name, "legal_tools stay move_to retreat author_memory"
bool TestDecisionAcceptsListedSite();     // plan action move_to esc_smart_c -> ok, verb move_to, site esc_smart_c
bool TestDecisionRejections();            // one assertion per reason above, each with its exact reason string
bool TestConstrainedOwnerOnlyStays();     // writer companion: move_to -> constrained; npc-bound owner: move_to -> constrained
bool TestCoastKeepsObjective();           // coast -> ok && keep
bool TestStaleAfterSplitRejected();       // observation taken, then a sync that changes membership -> stale_membership
bool TestLedgerKeepsNewest512();          // append 600: Size()==512, Format(1) contains "serial=600"
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: build error, `xrSim/xrSimFreeplayDecision.h` not found.

- [ ] **Step 3: Implement `xrSimFreeplayDecision.h/.cpp`.**

- [ ] **Step 4: Run to verify pass**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: all three test binaries exit 0 with no `FAIL:` lines.

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/xrSim/xrSimFreeplayDecision.* src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp src/xrEngine/CMakeLists.txt src/xrEngine/xrEngine.vcxproj src/xrEngine/xrEngine.vcxproj.filters
git commit -m "feat(xrsim): validate freeplay decisions against observed sites"
```

---

### Task 3: Wake scheduler

**Files:**
- Create: `src/xrEngine/xrSim/xrSimFreeplayScheduler.h`, `src/xrEngine/xrSim/xrSimFreeplayScheduler.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp`, `src/xrEngine/CMakeLists.txt`, `src/xrEngine/xrEngine.vcxproj`, `src/xrEngine/xrEngine.vcxproj.filters`

**Interfaces:**
- Consumes: `FreeplayPopulation`, `IsFreeplayTransferable`.
- Produces:

```cpp
namespace xrSim
{
struct FreeplaySchedulerConfig
{
    uint32_t maxInFlight = 2;
    uint32_t minWakeIntervalMs = 45000;
    uint32_t reconsiderIntervalMs = 600000;
    uint32_t observationTimeoutMs = 10000;
    uint32_t failureBackoffMs = 30000;
    uint32_t maxFailureBackoffMs = 480000;
};
enum class FreeplayWakeState { Idle, Queued, AwaitingObservation, AwaitingDecision };

class FreeplayScheduler
{
public:
    explicit FreeplayScheduler(const FreeplaySchedulerConfig& config = {});
    void Reset();
    void Forget(uint64_t ownerId);
    void RequestWake(uint64_t ownerId, const std::string& reason, uint64_t nowMs);
    std::vector<uint64_t> TakeObservationRequests(const FreeplayPopulation& population, uint64_t nowMs);
    std::vector<uint64_t> TakeTimedOutObservations(uint64_t nowMs);   // AwaitingObservation older than the timeout -> Idle
    void OnObservation(uint64_t ownerId, uint64_t nowMs);
    void OnDecision(uint64_t ownerId, bool providerFailed, uint64_t nowMs);
    bool BackoffActive(uint64_t nowMs) const;
    FreeplayWakeState State(uint64_t ownerId) const;
    std::string Reasons(uint64_t ownerId) const;      // distinct reasons joined by ','
    uint64_t QueuedAtMs(uint64_t ownerId) const;
    size_t InFlight() const;
};
} // namespace xrSim
```

Behavior the tests pin:
- A wake for an owner that is already queued or in flight adds its reason to that entry and creates no second entry.
- `TakeObservationRequests` first queues a `reconsider` wake for every transferable owner idle for `reconsiderIntervalMs`, then hands out queued owners oldest first while `InFlight() < maxInFlight`. It skips owners decided less than `minWakeIntervalMs` ago, drops wakes for non-transferable owners, and returns nothing while backoff is active.
- A provider failure starts a backoff of `failureBackoffMs`, doubled on each consecutive failure up to `maxFailureBackoffMs`. A success clears it.
- The constants are starting values. Task 7 measures and may change them.

- [ ] **Step 1: Write the failing tests**

```cpp
bool TestDuplicateWakesCoalesce();          // three RequestWake(owner,"sighting"/"arrived"/"sighting") -> one request; Reasons()=="sighting,arrived"
bool TestConcurrencyBoundAndFairness();     // 5 owners queued at t=0..4 -> first take returns the 2 oldest; after OnDecision on one, next take returns the third
bool TestMinWakeInterval();                 // decided at t=1000; wake at t=2000; take at t=40000 empty; take at t=46000 returns it
bool TestPeriodicReconsider();              // idle owner, no wake: take at t=600000 returns it with reason "reconsider"
bool TestConstrainedOwnersAreNotWoken();    // companion-writer owner and npc-bound owner never appear, even after reconsiderIntervalMs
bool TestProviderFailureBacksOff();         // failure at t=0: take at t=29999 empty, t=30000 non-empty; second failure -> 60000; success resets to 30000
bool TestObservationTimeoutFreesSlot();     // owner in AwaitingObservation at t=0: TakeTimedOutObservations(10000) returns it; State()==Idle
bool TestForgetRemovesOwner();              // Forget during AwaitingDecision: InFlight() drops; OnDecision afterwards is a no-op
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: build error, `xrSim/xrSimFreeplayScheduler.h` not found.

- [ ] **Step 3: Implement `xrSimFreeplayScheduler.h/.cpp`.**

- [ ] **Step 4: Run to verify pass**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: all three test binaries exit 0 with no `FAIL:` lines.

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/xrSim/xrSimFreeplayScheduler.* src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp src/xrEngine/CMakeLists.txt src/xrEngine/xrEngine.vcxproj src/xrEngine/xrEngine.vcxproj.filters
git commit -m "feat(xrsim): fair bounded wake scheduler for freeplay owners"
```

---

### Task 4: `ai.freeplay.*` verbs and the Lua entry point

**Files:**
- Create: `src/xrEngine/xrSim/xrSimFreeplayBridge.h`, `src/xrEngine/xrSim/xrSimFreeplayBridge.cpp`
- Create: `src/xrEngine/xrSimScript.h`, `src/xrEngine/xrSim_script.cpp`
- Modify: `src/xrEngine/xrSim/xrSimBridge.cpp` (`HandleBridgeVerb`: forward verbs starting with `ai.freeplay.`; `ai.reset` also resets freeplay), `src/xrEngine/xrSim/tests/xrSimFreeplayTests.cpp`, `src/xrEngine/CMakeLists.txt`, `src/xrEngine/xrEngine.vcxproj`, `src/xrEngine/xrEngine.vcxproj.filters`

**Interfaces:**
- Consumes: Tasks 1–3; `AsyncActorProviderQueue`, `CreateLiveActorIntentProvider`, `LoadAgentProviderConfigFromEnvironment`, `IsAnthropicHttpTransportAvailable`.
- Produces:

```cpp
namespace xrSim
{
std::string HandleFreeplayVerb(const std::string& verb, const std::string& payload, bool& ok);
void ResetFreeplay();
void SetFreeplayProviderForTests(std::unique_ptr<IActorIntentProvider> provider);   // replaces the live provider
} // namespace xrSim
```

```cpp
// src/xrEngine/xrSimScript.h
struct ENGINE_API xrsim_script_registrator { DECLARE_SCRIPT_REGISTER_FUNCTION(); };
```

Lua: `local ok, text = xrsim_verb(verb, payload)`. Register it in `xrsim_script_registrator::script_register` with `lua_pushcfunction` and `lua_setglobal`. Verbs that do not start with `ai.freeplay.` return `false, "forbidden"`. A non-string argument returns `false, "usage"`.

Verbs (the payload is everything after the verb):

| Verb | Payload | Result |
|---|---|---|
| `ai.freeplay.mode` | none, `off`, `recorded` or `live` | `mode=<mode>`. `live` fails with `provider_disabled`, `missing_api_key` or `transport_unavailable` and leaves the mode unchanged |
| `ai.freeplay.begin` | none | `epoch=<n>`. Drops owners, scheduler state, stored observations and pending requests |
| `ai.freeplay.sync` | snapshot | `owners=<n>` |
| `ai.freeplay.retire` | `<group_id>` | `retired` or `unknown_group`. Also forgets the owner in the scheduler |
| `ai.freeplay.event` | `<group_id> <arrived\|blocked\|casualty\|sighting> <detail>` | `queued`. Requests a wake with the event name as the reason and adds a ledger line |
| `ai.freeplay.wake` | `<owner_id> <reason>` | `queued` |
| `ai.freeplay.observe` | observation text | `submitted request=<id>` in live mode, `stored` in recorded mode |
| `ai.freeplay.decide` | `<owner_id> stay` or `<owner_id> move_to\|retreat <site>` | `accepted <verb> <site>`, or the validation reason as an error. Needs a stored observation (`no_observation`). Source `recorded` |
| `ai.freeplay.tick` | `<now_ms>` | records below |
| `ai.freeplay.owners` | optional level | one line per owner: `owner <owner_id> name= kind= binding= writer= level= members= mrev= state= objective= source= rev= sites=<comma list from the stored observation>` |
| `ai.freeplay.census` | optional level | `FormatCensus` |
| `ai.freeplay.log` | optional count, default 50 | `FreeplayLedger::Format` |

Tick, in order:
1. Request a `spawn` wake for each new owner and a `membership` wake for each changed one (`TakeChangedOwners`).
2. Poll every pending provider request. A ready reply goes through `ValidateFreeplayDecision`. Accepted: `SetObjective(..., Live)` and store memories. `keep` or rejected: objective unchanged. Provider error: objective unchanged, ledger source `hold` with the provider error as the verdict, `OnDecision(owner, true, now)`. Every outcome appends one ledger entry with `queueAgeMs` and `latencyMs`.
3. Ledger `observation_timeout` for owners returned by `TakeTimedOutObservations`.
4. Unless the mode is `off`, emit one line per owner from `TakeObservationRequests`.
5. Emit one `apply` line per transferable owner whose objective revision changed since the previous tick, or that has never been emitted.

```
observe <owner_id> <group|npc> <binding_id> <membership_revision>
apply <owner_id> <group_id> <objective_revision> <verb> <site> <source>
```

In mode `off`, `sync` and `census` still work and the tick emits no lines. The epoch at request time is stored with each pending request, so a reply that arrives after `begin` fails as `stale_world`.

- [ ] **Step 1: Write the failing tests** against `HandleBridgeVerb`. For live mode install a `RecordedActorIntentProvider` through `SetFreeplayProviderForTests` and poll `ai.freeplay.tick` in a loop of at most 200 iterations with a 5 ms sleep.

```cpp
bool TestRecordedDecisionFlow();          // mode recorded, begin, sync kTwoGroups, tick 0 -> contains "observe 1 group 500 1" and
                                          // "apply 1 500 1 move_to esc_smart_b bootstrap"; observe with site esc_smart_c;
                                          // decide "1 move_to esc_smart_c" -> accepted; next tick contains "apply 1 500 2 move_to esc_smart_c recorded"
bool TestDecideRejectsUnknownSite();      // decide "1 move_to nowhere" -> !ok, text "unknown_site"; no new apply line
bool TestLiveDecisionFlow();              // provider scripted with "xrsim_actor_intent_v1 ... action move_to esc_smart_c ... end":
                                          // an apply line with source live appears; log has "source=live" and "verdict=accepted"
bool TestReplyAfterBeginWorldIsStale();   // submit, then begin + sync, then tick until the reply drains: no apply with source live; log has "stale_world"
bool TestStaleReplyAfterSplit();          // submit, then a sync that splits group 500: log has "stale_membership"; objective unchanged
bool TestProviderErrorHolds();            // provider script exhausted: objective and revision unchanged; log has "source=hold"
bool TestLiveModeRefusedWithoutProvider();// no test provider; the test first clears XRAY_AGENT_API_KEY and ANTHROPIC_API_KEY
                                          // in its own process: mode live -> !ok; mode still off
bool TestBlockedEventRewakesOwner();      // event "500 blocked esc_smart_c": owners line shows state=queued; log has "blocked"
bool TestModeOffEmitsNothing();           // sync then tick -> empty text; census still lists 4 npcs
bool TestResetClearsFreeplay();           // ai.reset -> census summary npcs=0, mode off
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: build error, `xrSim/xrSimFreeplayBridge.h` not found.

- [ ] **Step 3: Implement `xrSimFreeplayBridge.h/.cpp` and the dispatch in `xrSimBridge.cpp`.**

- [ ] **Step 4: Run to verify pass**

Run: `tools/xrsim_standalone/build_and_test.sh`
Expected: all three test binaries exit 0 with no `FAIL:` lines.

- [ ] **Step 5: Implement `xrSimScript.h` and `xrSim_script.cpp`, then build the engine**

Run: `cmake --build build -j10`
Expected: build succeeds and `bin/arm64/Release/xrEngine.dylib` is newer than before the build.

- [ ] **Step 6: Commit**

```bash
git add src/xrEngine/xrSim src/xrEngine/xrSimScript.h src/xrEngine/xrSim_script.cpp src/xrEngine/CMakeLists.txt src/xrEngine/xrEngine.vcxproj src/xrEngine/xrEngine.vcxproj.filters
git commit -m "feat(xrsim): ai.freeplay verbs and xrsim_verb Lua entry point"
```

---

### Task 5: CoC adapter and private test runtime

**Files:**
- Create: `res/coc/gamedata/scripts/xrsim_freeplay.script`
- Create: `tools/freeplay_runtime.py`, `tools/tests/test_freeplay_runtime.py`

**Interfaces:**
- Consumes: `xrsim_verb` and the Task 4 verbs and record formats.
- Produces:
  - `python3 tools/freeplay_runtime.py clone --from <runtime> --to <new-dir> --save <name>`
  - `python3 tools/freeplay_runtime.py install --runtime <dir> [--force]`
  - Launch flag `-xrsim_freeplay recorded|live`.

`freeplay_runtime.py`:
- `clone` requires `<new-dir>` to be absent or empty and outside `<runtime>`. It copies `database/`, `gamedata/` and `fsgame.ltx` (try `cp -c -R` on macOS, fall back to `shutil.copytree`), creates `appdata/` with a copy of the source `user.ltx` and only `savedgames/<name>.scop` and `<name>.scoc`, then runs `install`. It fails before writing anything when the save or a source directory is missing.
- `install` copies the adapter to `<dir>/gamedata/scripts/xrsim_freeplay.script`. If a different file is already there it fails unless `--force` is given.

Adapter constants: `TICK_MS = 1000`, `SYNC_MS = 5000`, `LONE_SCAN_MS = 60000`, `MAX_SITES = 8`, `MAX_SIGHTINGS = 6`, `SIGHT_RADIUS_M = 150`.

Adapter behavior:
- `on_game_start()`: read the mode from `command_line()` with the pattern `%-xrsim_freeplay%s+(%a+)`. With no flag, or when `xrsim_verb` is nil, return without registering anything. Otherwise register `actor_on_first_update`, `actor_on_update`, `squad_on_update`, `squad_on_unregister` and `squad_on_npc_death`.
- First update: `ai.freeplay.mode <mode>`. On failure log `! xrsim_freeplay: <reason>`, show it with `SetHudMsg`, and stay inactive for the session. On success call `ai.freeplay.begin` and run a full sync that includes the squadless scan.
- Writer of a squad, first match wins: `axr_companions.companion_squads[squad.id]` gives `companion`; `xrs_kill_wounded.hostage_list[squad:commander_id()]` gives `hostage`; a `scripted_target` the adapter did not set, or `xr_logic.pick_section_from_condlist(db.actor, squad, squad.action_condlist)` returning a section, gives `script`; `squad.random_targets` gives `random`; otherwise `sim`.
- Group record: `squad.id`, `squad:name()`, species by `IsStalker`/`IsMonster` on the commander's clsid, `squad.player_id`, level name of `squad.m_game_vertex_id`, the name of the smart `squad.smart_id` or `-`, the name of the smart `assigned_target_id or current_target_id` or `-`, writer, `squad.online`. Members come from `squad:squad_members()`, living only. Skip squads with `disabled` set or no living members.
- Squadless scan: every `alife_object(id)` for ids 1–65534 that is a living stalker or monster with `group_id == 65535`. Cache the result and refresh it every `LONE_SCAN_MS`.
- Each `TICK_MS`, call `ai.freeplay.tick` with `time_global()` and handle its lines:
  - `observe`: build the observation for that squad. Sites are the squad's current smart plus smarts on the squad's level for which `smart:target_precondition(squad)` holds, nearest first, at most `MAX_SITES`; kind is the first of base, territory, lair, resource present in `smart.props`, else `site`; occupants is `SIMBOARD:get_smart_population(smart)`. Sightings are other squads within `SIGHT_RADIUS_M`, nearest first, with the relation from `game_relations` between the two factions. `situation` is `day` or `night`. Send it with `ai.freeplay.observe`.
  - `apply`: store `objectives[group_id] = {rev, verb, site}`.
- `squad_on_update(squad)`, for a squad with a stored objective:
  - `hold` or site `-`: set `scripted_target` to the squad's current smart name if it has one; otherwise set `squad.__lock = true`.
  - Else look up `SIMBOARD.smarts_by_names[site]`. If it is missing, or the verb is not `stay` and `smart:target_precondition(squad)` is false, send `ai.freeplay.event <id> blocked <site>` once for that revision and leave `scripted_target` as it was. Otherwise set `squad.scripted_target = site` and remember that the adapter set it.
  - When `squad.current_action == 1` and `squad.current_target_id == smart.id` for the first time in that revision, send `ai.freeplay.event <id> arrived <site>`.
- Sighting event: during sync, a hostile squad seen within `SIGHT_RADIUS_M` that was not within it at the previous sync sends `ai.freeplay.event <id> sighting <other_name>`.
- `squad_on_unregister`: `ai.freeplay.retire <id>` and drop the local state. `squad_on_npc_death`: `ai.freeplay.event <id> casualty <npc_id>` and force a sync on the next tick.
- Squads whose writer is not `sim` are reported and never written. If a squad stops being `sim`, clear the `scripted_target` the adapter had set.

- [ ] **Step 1: Write the failing tool tests** in `tools/tests/test_freeplay_runtime.py`, on temporary fixture directories in the style of `test_stage_coc_runtime.py`.

```python
def test_clone_copies_assets_one_save_and_adapter(self):   # database/x.db, fsgame.ltx, gamedata/configs/a.ltx copied; only cordon_start.scop/.scoc in savedgames; adapter present
def test_clone_refuses_nonempty_destination(self):         # nonzero exit, destination untouched
def test_clone_refuses_destination_inside_source(self):
def test_clone_missing_save_fails_before_writing(self):    # destination does not exist afterwards
def test_install_refuses_different_existing_adapter(self): # nonzero exit, file unchanged; with --force it is replaced
def test_install_is_idempotent(self):
```

- [ ] **Step 2: Run to verify failure**

Run: `python3 -m unittest tools.tests.test_freeplay_runtime -v`
Expected: import error, `tools/freeplay_runtime.py` not found.

- [ ] **Step 3: Implement `tools/freeplay_runtime.py`.**

- [ ] **Step 4: Run to verify pass**

Run: `python3 -m unittest tools.tests.test_freeplay_runtime -v`
Expected: 6 tests, `OK`.

- [ ] **Step 5: Write `res/coc/gamedata/scripts/xrsim_freeplay.script`** to the behavior above. Check every CoC name it uses against the unpacked scripts (`command_line`, `SetHudMsg`, `get_smart_population`, `target_precondition`, `smart.props`, the relation helper) and adapt where 1.4.22 differs, keeping the record formats unchanged.

- [ ] **Step 6: Verify stock behavior without the flag**

```bash
REPO="$PWD"
python3 tools/freeplay_runtime.py clone --from build/runtime/play --to build/runtime/freeplay-ai --save cordon_start
cd build/runtime/freeplay-ai
"$REPO/bin/arm64/Release/xr_3da" -agent_bridge appdata/agent_bridge.sock -agent_background &
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock cmd "load cordon_start"
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock state        # repeat until scene=game and actor level=l01_escape
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock ai.freeplay.census
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock cmd quit
```

Bridge waits and the exact load command follow `docs/macos-dev-setup.md` and `tools/gl_load_play_save_load.txt`.

Expected: the census prints `summary npcs=0 owners=0 transferable=0 script_owned=0 unbound=0`, and the log in `appdata/logs/` has no line containing `xrsim_freeplay` and no Lua error.

- [ ] **Step 7: Verify the adapter loads with the flag**

Repeat Step 6 with `-xrsim_freeplay recorded` added to the launch line. Wait 10 seconds after the level loads before the census.
Expected: `summary npcs=<n>` with `n > 0`, the log has no line starting with `! xrsim_freeplay` and no Lua error, and `ai.freeplay.owners l01_escape` lists owners with `source=bootstrap` or `source=none`.

- [ ] **Step 8: Commit**

```bash
git add res/coc/gamedata/scripts/xrsim_freeplay.script tools/freeplay_runtime.py tools/tests/test_freeplay_runtime.py
git commit -m "feat: CoC freeplay adapter and private runtime clone tool"
```

---

### Task 6: Cordon census and recorded decisions (spec stages 1 and 2)

**Files:**
- Create: `tools/freeplay_probe.py`, `tools/tests/test_freeplay_probe.py`
- Create: `artifacts/freeplay-ai/2026-11-cordon/` (evidence; small text and JSON only)
- Modify: `docs/PLAN_STATUS.md`

**Interfaces:**
- Consumes: Task 4 verbs and line formats; `Bridge` from `tools/agentctl.py`.
- Produces:
  - `python3 tools/freeplay_probe.py census <endpoint> --level l01_escape --out <dir>`
  - `python3 tools/freeplay_probe.py move <endpoint> --owner <owner_id> --site <name> --out <dir> [--timeout 900]`
  - `parse_census(text) -> dict` with `npcs` (list of dicts keyed by the census fields) and `summary`
  - `parse_owners(text) -> list[dict]`
  - `pick_owners(owners) -> dict` mapping each of `squad`, `pack`, `lone_stalker`, `lone_mutant` to the first transferable owner of that kind, or `None`

`census` writes `census.txt` and `census.json`, prints the summary plus the owner count per kind and per writer, and exits 1 when the census is empty or any `npc` line lacks an owner. `move` sends `ai.freeplay.wake <owner> probe`, waits until `ai.freeplay.owners` shows that owner with a non-empty `sites=`, sends `ai.freeplay.decide <owner> move_to <site>`, then every 5 seconds records the server position of each member (bridge `lua` verb, `alife_object(id).position`) and the ledger until an `arrived` or `blocked` event for that owner or the timeout. It writes `track.json` (owner, site, member IDs, timestamped positions, final distance to the smart, outcome) and exits 0 only on `arrived` with the leader within 150 m of the smart.

- [ ] **Step 1: Write the failing tests** with a fake `Bridge` returning canned text.

```python
def test_parse_census_fields_and_summary(self):      # the Task 1 kTwoGroups census: 4 npcs, summary["unbound"] == 1
def test_census_exits_1_when_empty(self):
def test_pick_owners_one_per_kind_and_skips_script_owned(self):
def test_move_waits_for_sites_then_decides(self):    # fake returns sites= on the third poll; exactly one decide is sent
def test_move_reports_blocked_as_failure(self):      # ledger line with blocked -> exit 1, track.json outcome "blocked"
def test_move_times_out(self):                       # no event -> exit 1, outcome "timeout"
```

- [ ] **Step 2: Run to verify failure**

Run: `python3 -m unittest tools.tests.test_freeplay_probe -v`
Expected: import error.

- [ ] **Step 3: Implement `tools/freeplay_probe.py`.**

- [ ] **Step 4: Run to verify pass**

Run: `python3 -m unittest tools.tests.test_freeplay_probe tools.tests.test_freeplay_runtime -v`
Expected: 12 tests, `OK`.

- [ ] **Step 5: Stage 1 census.** Launch `build/runtime/freeplay-ai` as in Task 5 Step 7 and run `census ... --out artifacts/freeplay-ai/2026-11-cordon/census`.
Expected: exit 0. Record in the evidence README the totals, the counts per kind and writer, every `script_owned` squad by name and every `unbound` NPC by section. These two lists are the spec's "missing bindings and script-owned destination writers". Confirm by name that Sidorovich and any companion squad are not `writer=sim`. If Cordon lacks one of the four owner kinds among transferable owners, record that and use the nearest available case.

- [ ] **Step 6: Stage 2 moves.** For one owner of each available kind, run `move` to a site A, quit, relaunch from the same `cordon_start` save and run `move` for the same squad name to a different site B. Do one pair with the player near the squad (online) and one with the squad far from the player (offline).
Expected: each run exits 0 and the two tracks of a pair end at different smarts. This is the evidence that the decision, and not ordinary wandering, set the destination.

- [ ] **Step 7: Stage 2 membership cases.**
  - Leader loss: during a `move`, remove the squad's commander through the bridge `lua` verb. Expected: `ai.freeplay.owners` shows the same owner ID with `mrev` raised by 1, and the squad still arrives.
  - Stale reply after a split, and a lone owner joining a group: create two test squads with `SIMBOARD:create_squad` at one smart and move a member between them with `unregister_member`/`register_member`. Expected: the ledger shows `stale_membership` for a decision requested before the change, and the moved NPC's census line shows the destination squad's owner and objective. If stock scripts reject the membership change, record the error text as an open gap. The library tests in Tasks 1, 2 and 4 remain the coverage for those two cases.
  - Blocked destination: `decide` a site, then set that smart's `disabled = true` through `lua` before the squad's next update. Expected: a `blocked` ledger line, the squad keeps its previous target, and the owner is queued again.

- [ ] **Step 8: Regression gate.** In the same runtime without the flag, run the movement, save, load, inventory, menu and quit scenario adapted from `tools/gl_load_play_save_load.txt` for the `cordon_start` save.
Expected: every step succeeds and the log has no Lua error.

- [ ] **Step 9: Record and commit.** Add a "Freeplay population AI" section to `docs/PLAN_STATUS.md` with the commits, test counts, the census totals, each Step 6–8 result with its artifact path, and each case that failed or was not exercised. State that this is a macOS run and that the Windows build of the new sources is unverified.

```bash
git add tools/freeplay_probe.py tools/tests/test_freeplay_probe.py artifacts/freeplay-ai docs/PLAN_STATUS.md
git commit -m "test: Cordon freeplay census and recorded-decision evidence"
```

---

### Task 7: Autonomous model wakes in Cordon (spec stage 3)

**Precondition:** the user has named the model ID and supplied the API key (Global Constraints). Record the model ID and the date its availability was checked in the evidence README. Do not start otherwise.

**Files:**
- Modify: `tools/freeplay_probe.py`, `tools/tests/test_freeplay_probe.py` (add `watch`)
- Modify: `src/xrEngine/xrSim/xrSimFreeplayScheduler.h` (only if the measurements justify new constants)
- Modify: `docs/PLAN_STATUS.md`, `docs/HANDOVER.md`, `README.md` (launch flag and environment variables)

**Interfaces:**
- Consumes: everything above; environment `XRAY_AGENT_PROVIDER=anthropic`, `XRAY_AGENT_MODEL=<approved id>`, `ANTHROPIC_API_KEY`; bridge verbs `perf.start`, `perf.stop`, `perf.export`.
- Produces: `python3 tools/freeplay_probe.py watch <endpoint> --minutes <n> --out <dir>`. Every 10 seconds it appends new ledger lines to `ledger.txt` and an owners snapshot to `owners.jsonl`. At the end it writes `summary.json` with, per owner kind: accepted live decisions, rejections by reason, holds by reason, arrivals, median and maximum `latencyMs` and `queueAgeMs`. It exits 0 only if every owner kind present among transferable owners has at least one accepted live decision followed by an `arrived` event for the same owner.

- [ ] **Step 1: Write the failing tests**

```python
def test_watch_summarises_ledger_by_kind(self):        # canned ledger: 2 live accepted, 1 stale_membership, 1 hold -> those counts in summary.json
def test_watch_requires_live_decision_and_arrival_per_kind(self):  # squad has both, pack has a decision but no arrival -> exit 1
def test_watch_deduplicates_ledger_lines_by_serial(self):
```

- [ ] **Step 2: Run to verify failure**

Run: `python3 -m unittest tools.tests.test_freeplay_probe -v`
Expected: the three new tests fail because `watch` is unknown.

- [ ] **Step 3: Implement `watch`.**

- [ ] **Step 4: Run to verify pass**

Run: `python3 -m unittest tools.tests.test_freeplay_probe -v`
Expected: 9 tests, `OK`.

- [ ] **Step 5: Activation failure check.** Launch with `-xrsim_freeplay live` and no API key in the environment.
Expected: the log has `! xrsim_freeplay: missing_api_key`, the census summary shows `npcs=0`, and the game plays as stock.

- [ ] **Step 6: Live run.** Launch with `-xrsim_freeplay live` and the approved environment, load `cordon_start`, send `perf.start`, run `watch --minutes 30 --out artifacts/freeplay-ai/2026-11-cordon/live`, then `perf.stop` and `perf.export`. Nothing spawns or wakes owners by hand. Take a screenshot with `shot` when the first squad arrives.
Expected: `watch` exits 0. Summarise the frame-time export with `tools/frame_time_report.py` and store the report beside `summary.json`.

- [ ] **Step 7: Outage run.** Repeat Step 6 for 10 minutes with `XRAY_AGENT_TIMEOUT_MS=1`.
Expected: no accepted live decision, ledger lines with `source=hold`, wake attempts spaced by the backoff, squads still travelling to their bootstrap objectives, and no stall in the frame-time report.

- [ ] **Step 8: Tune only from evidence.** If Step 6 shows a median `queueAgeMs` above `reconsiderIntervalMs`, or provider rate-limit errors, change the constants in `FreeplaySchedulerConfig`, update the Task 3 tests to the new values, rerun `tools/xrsim_standalone/build_and_test.sh`, rebuild and repeat Step 6. Otherwise leave them.

- [ ] **Step 9: Regression gate.** Repeat Task 6 Step 8 on the final build.

- [ ] **Step 10: Document and commit.** In `docs/PLAN_STATUS.md` record the model ID, run lengths, the `summary.json` numbers, provider failures, the frame-time report path, and what remains open: save persistence, the saved activation setting, the Cordon–Garbage crossing, Zone-wide checks, companion order persistence, the Windows build, and every gap listed in Task 6. Update the AI paragraph of `docs/HANDOVER.md` and add the flag and environment variables to `README.md`. Do not describe the Zone as AI-driven beyond what the runs showed.

```bash
git add tools/freeplay_probe.py tools/tests/test_freeplay_probe.py artifacts/freeplay-ai docs/PLAN_STATUS.md docs/HANDOVER.md README.md src/xrEngine/xrSim
git commit -m "feat: autonomous freeplay wakes in Cordon with live evidence"
```
