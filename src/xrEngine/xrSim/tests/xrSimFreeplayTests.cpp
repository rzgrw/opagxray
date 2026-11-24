#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimClaudeCliProvider.h"
#include "xrSim/xrSimFreeplay.h"
#include "xrSim/xrSimFreeplayBridge.h"
#include "xrSim/xrSimFreeplayDecision.h"
#include "xrSim/xrSimFreeplayScheduler.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <climits>
#include <cstdio>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

const char* kTwoGroups =
    "xrsim_freeplay_sync_v1\n"
    "group 500 esc_stalkers stalker stalker l01_escape esc_smart_a esc_smart_b sim 1\n"
    "member 500 601 sim_default_stalker_1 1\n"
    "member 500 602 sim_default_stalker_0 0\n"
    "group 510 esc_dogs mutant monster_predatory_day l01_escape - - sim 0\n"
    "member 510 611 dog_weak 1\n"
    "npc 700 esc_m_trader stalker trader l01_escape\n"
    "end\n";

const char* kGroup500Header =
    "xrsim_freeplay_sync_v1\n"
    "group 500 esc_stalkers stalker stalker l01_escape esc_smart_a esc_smart_b sim 1\n";

const char* kGroup510AndTrader =
    "group 510 esc_dogs mutant monster_predatory_day l01_escape - - sim 0\n"
    "member 510 611 dog_weak 1\n"
    "npc 700 esc_m_trader stalker trader l01_escape\n";

void RequireSetup(const xrSim::FreeplayResult& result)
{
    if (!Expect(result.ok, ("test setup: " + result.reason).c_str()))
        std::exit(EXIT_FAILURE);
}

const xrSim::FreeplayOwner& RequireGroup(const xrSim::FreeplayPopulation& population, uint16_t groupId)
{
    const xrSim::FreeplayOwner* owner = population.FindByBinding(xrSim::FreeplayBinding::Group, groupId);
    if (!Expect(owner != nullptr, "test setup: group owner exists"))
        std::exit(EXIT_FAILURE);
    return *owner;
}

std::string SummaryLine(const std::string& census)
{
    const size_t at = census.rfind("summary ");
    if (at == std::string::npos)
        return std::string();
    const size_t end = census.find('\n', at);
    return census.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

bool HasObjective(const xrSim::FreeplayOwner& owner, const char* verb, const char* site,
    xrSim::FreeplayObjectiveSource source)
{
    return owner.objective.verb == verb && owner.objective.site == site && owner.objective.source == source;
}

bool TestSyncEnrollsEveryNpc()
{
    xrSim::FreeplayPopulation population;
    const xrSim::FreeplayResult synced = population.Sync(kTwoGroups);
    if (!Expect(synced.ok, ("valid snapshot syncs: " + synced.reason).c_str()))
        return false;
    bool ok = Expect(population.Owners().size() == 3, "three owners enrolled");
    const xrSim::FreeplayOwner* squad = population.OwnerOfNpc(601);
    const xrSim::FreeplayOwner* dogs = population.OwnerOfNpc(611);
    const xrSim::FreeplayOwner* trader = population.OwnerOfNpc(700);
    if (!Expect(squad && dogs && trader, "every npc has an owner"))
        return false;
    ok = Expect(population.OwnerOfNpc(602) == squad, "squad members share one owner") && ok;
    ok = Expect(std::string(xrSim::FreeplayKindName(squad->kind)) == "squad", "two stalkers form a squad") && ok;
    ok = Expect(std::string(xrSim::FreeplayKindName(dogs->kind)) == "lone_mutant", "one dog is a lone mutant") && ok;
    ok = Expect(std::string(xrSim::FreeplayKindName(trader->kind)) == "lone_stalker", "npc record is a lone stalker") &&
        ok;
    ok = Expect(trader->binding == xrSim::FreeplayBinding::Npc && trader->bindingId == 700 &&
            trader->writer == xrSim::FreeplayWriter::None,
        "npc record binds to the npc with writer none") && ok;
    ok = Expect(HasObjective(*squad, "move_to", "esc_smart_b", xrSim::FreeplayObjectiveSource::Bootstrap) &&
            squad->objective.revision == 1,
        "group with target bootstraps move_to target") && ok;
    ok = Expect(HasObjective(*dogs, "hold", "-", xrSim::FreeplayObjectiveSource::None),
        "group without target holds with source none") && ok;
    ok = Expect(squad->ownerId == 1 && dogs->ownerId == 2 && trader->ownerId == 3, "owner ids start at 1") && ok;
    ok = Expect(xrSim::IsFreeplayTransferable(*squad) && !xrSim::IsFreeplayTransferable(*trader),
        "sim group is transferable, npc owner is not") && ok;
    const std::string census = population.FormatCensus("");
    ok = Expect(SummaryLine(census) == "summary npcs=4 owners=3 transferable=2 script_owned=0 unbound=1",
        "census summary counts") && ok;
    ok = Expect(census.find("npc 601 sim_default_stalker_1 owner=1 kind=squad binding=group:500 writer=sim "
                            "level=l01_escape objective=move_to:esc_smart_b source=bootstrap rev=1\n") !=
            std::string::npos,
        "census npc line format") && ok;
    ok = Expect(census.find("npc 700 esc_m_trader owner=3 kind=lone_stalker binding=npc:700 writer=none "
                            "level=l01_escape objective=hold:- source=none rev=0\n") != std::string::npos,
        "census unbound npc line format") && ok;

    xrSim::FreeplayPopulation semicolons;
    std::string joined = kTwoGroups;
    for (char& c : joined)
        if (c == '\n')
            c = ';';
    RequireSetup(semicolons.Sync(joined));
    return Expect(semicolons.FormatCensus("") == census, "semicolon separated snapshot is equivalent") && ok;
}

bool TestSyncIsAtomicOnBadRecord()
{
    std::string tooManyMembers = kGroup500Header;
    for (int i = 0; i < 65; ++i)
        tooManyMembers += "member 500 " + std::to_string(1000 + i) + " dummy 0\n";
    tooManyMembers += "end\n";

    std::string tooManyGroups = "xrsim_freeplay_sync_v1\n";
    for (int i = 0; i < 4097; ++i)
    {
        const std::string id = std::to_string(10000 + i);
        tooManyGroups += "group " + id + " g stalker stalker l01_escape - - sim 0\n";
        tooManyGroups += "member " + id + " " + std::to_string(20000 + i) + " s 1\n";
    }
    tooManyGroups += "end\n";

    const std::vector<std::pair<const char*, std::string>> invalid{
        {"missing header", std::string("group 500 a stalker stalker l01_escape - - sim 1\n"
                                       "member 500 601 s 1\nend\n")},
        {"wrong header", std::string("xrsim_freeplay_sync_v2\nend\n")},
        {"missing end", std::string(kGroup500Header) + "member 500 601 s 1\n"},
        {"record after end", std::string(kGroup500Header) + "member 500 601 s 1\nend\nnpc 9 s stalker f l\n"},
        {"unknown record", std::string(kGroup500Header) + "member 500 601 s 1\nsquad 1\nend\n"},
        {"short record", std::string(kGroup500Header) + "member 500 601 s\nend\n"},
        {"long record", std::string(kGroup500Header) + "member 500 601 s 1 extra\nend\n"},
        {"bad number", std::string(kGroup500Header) + "member 500 6x1 s 1\nend\n"},
        {"negative number", std::string(kGroup500Header) + "member 500 -601 s 1\nend\n"},
        {"number out of range", std::string(kGroup500Header) + "member 500 65536 s 1\nend\n"},
        {"bad leader flag", std::string(kGroup500Header) + "member 500 601 s 2\nend\n"},
        {"bad online flag", std::string("xrsim_freeplay_sync_v1\n"
                                        "group 500 a stalker stalker l01_escape - - sim yes\n"
                                        "member 500 601 s 1\nend\n")},
        {"unknown species", std::string("xrsim_freeplay_sync_v1\n"
                                        "group 500 a robot stalker l01_escape - - sim 1\n"
                                        "member 500 601 s 1\nend\n")},
        {"unknown writer", std::string("xrsim_freeplay_sync_v1\n"
                                       "group 500 a stalker stalker l01_escape - - lua 1\n"
                                       "member 500 601 s 1\nend\n")},
        {"duplicate group id", std::string(kGroup500Header) + "member 500 601 s 1\n" +
                                   "group 500 b stalker stalker l01_escape - - sim 1\nend\n"},
        {"duplicate object id in groups", std::string(kGroup500Header) + "member 500 601 s 1\n" +
                                              "group 520 b stalker stalker l01_escape - - sim 1\n" +
                                              "member 520 601 s 1\nend\n"},
        {"duplicate object id npc and member", std::string(kGroup500Header) + "member 500 601 s 1\n" +
                                                   "npc 601 s stalker f l01_escape\nend\n"},
        {"member of unknown group", std::string(kGroup500Header) + "member 500 601 s 1\n" +
                                        "member 520 602 s 1\nend\n"},
        {"group with no members", std::string(kGroup500Header) + "end\n"},
        {"more than 64 members", tooManyMembers},
        {"more than 4096 groups", tooManyGroups},
    };

    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    population.TakeChangedOwners();
    const std::string before = population.FormatCensus("");
    bool ok = true;
    for (const auto& item : invalid)
    {
        const xrSim::FreeplayResult result = population.Sync(item.second);
        ok = Expect(!result.ok && !result.reason.empty(), item.first) && ok;
        ok = Expect(population.FormatCensus("") == before, item.first) && ok;
    }
    ok = Expect(population.TakeChangedOwners().empty(), "rejected syncs report no changed owners") && ok;

    const std::string withNewGroup = std::string(kTwoGroups).substr(0, std::string(kTwoGroups).size() - 4) +
        "group 520 c stalker stalker l01_escape - - sim 1\nmember 520 621 s 1\nend\n";
    RequireSetup(population.Sync(withNewGroup));
    return Expect(RequireGroup(population, 520).ownerId == 4, "rejected syncs consume no owner ids") && ok;
}

bool TestLeaderLossKeepsOwnerAndObjective()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayOwner before = RequireGroup(population, 500);
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 602 sim_default_stalker_0 1\n" +
        kGroup510AndTrader + "end\n"));
    const xrSim::FreeplayOwner& after = RequireGroup(population, 500);
    return Expect(after.ownerId == before.ownerId, "leader loss keeps owner id") &&
        Expect(after.kind == xrSim::FreeplayOwnerKind::LoneStalker, "one survivor is a lone stalker") &&
        Expect(after.membershipRevision == 2, "membership revision advances") &&
        Expect(HasObjective(after, "move_to", "esc_smart_b", xrSim::FreeplayObjectiveSource::Bootstrap) &&
                after.objective.revision == 1,
            "objective unchanged") &&
        Expect(population.OwnerOfNpc(601) == nullptr, "dead leader has no owner");
}

bool TestReusedGroupIdGetsNewOwner()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const uint64_t oldId = RequireGroup(population, 500).ownerId;
    RequireSetup(population.Retire(xrSim::FreeplayBinding::Group, 500));
    bool ok = Expect(population.FindByBinding(xrSim::FreeplayBinding::Group, 500) == nullptr, "retired owner gone");
    ok = Expect(!population.Retire(xrSim::FreeplayBinding::Group, 500).ok, "retiring twice fails") && ok;
    RequireSetup(population.Sync(std::string("xrsim_freeplay_sync_v1\n"
                                             "group 500 esc_new stalker stalker l01_escape esc_smart_a - sim 1\n"
                                             "member 500 801 sim_default_stalker_2 1\n"
                                             "member 500 802 sim_default_stalker_3 0\n") +
        kGroup510AndTrader + "end\n"));
    const xrSim::FreeplayOwner& reused = RequireGroup(population, 500);
    ok = Expect(reused.ownerId != oldId && reused.ownerId == 4, "reused group id gets a new owner id") && ok;
    ok = Expect(reused.objective.source == xrSim::FreeplayObjectiveSource::Bootstrap ||
            reused.objective.source == xrSim::FreeplayObjectiveSource::None,
        "reused group objective is bootstrap or none") && ok;
    return Expect(reused.objective.site != "esc_smart_b", "reused group never keeps the old site") && ok;
}

bool TestReusedGroupIdWithoutRetireGetsNewOwner()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const uint64_t oldId = RequireGroup(population, 500).ownerId;
    RequireSetup(population.SetObjective(oldId, "move_to", "esc_smart_c", xrSim::FreeplayObjectiveSource::Recorded));
    RequireSetup(population.AddMemory(oldId, "ambushed_at_bridge"));
    population.TakeChangedOwners();
    RequireSetup(population.Sync(std::string("xrsim_freeplay_sync_v1\n"
                                             "group 500 esc_new stalker stalker l01_escape esc_smart_a esc_smart_d sim 1\n"
                                             "member 500 801 sim_default_stalker_2 1\n"
                                             "member 500 802 sim_default_stalker_3 0\n") +
        kGroup510AndTrader + "end\n"));
    const xrSim::FreeplayOwner& reused = RequireGroup(population, 500);
    bool ok = Expect(reused.ownerId != oldId && reused.ownerId == 4, "reused group id without retire gets a new owner");
    ok = Expect(population.FindOwner(oldId) == nullptr, "previous occupant retired") && ok;
    ok = Expect(HasObjective(reused, "move_to", "esc_smart_d", xrSim::FreeplayObjectiveSource::Bootstrap) &&
            reused.objective.revision == 1,
        "new occupant bootstraps from the snapshot") && ok;
    ok = Expect(reused.memories.empty() && reused.membershipRevision == 1, "new occupant starts with no memories") && ok;
    return Expect(population.TakeChangedOwners() == std::vector<uint64_t>{reused.ownerId},
               "new occupant is reported as changed") && ok;
}

bool TestReusedNpcIdWithNewSectionGetsNewOwner()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayOwner* trader = population.FindByBinding(xrSim::FreeplayBinding::Npc, 700);
    if (!Expect(trader != nullptr, "npc owner exists"))
        return false;
    const uint64_t oldId = trader->ownerId;
    RequireSetup(population.AddMemory(oldId, "sold_ammo"));
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "member 500 602 sim_default_stalker_0 0\n" + "group 510 esc_dogs mutant monster_predatory_day l01_escape - - sim 0\n" +
        "member 510 611 dog_weak 1\n" + "npc 700 esc_wolf stalker stalker l01_escape\n" + "end\n"));
    const xrSim::FreeplayOwner* reused = population.FindByBinding(xrSim::FreeplayBinding::Npc, 700);
    return Expect(reused != nullptr && reused->ownerId != oldId && reused->ownerId == 4,
               "reused npc id with a new section gets a new owner") &&
        Expect(reused != nullptr && reused->memories.empty(), "new npc occupant starts with no memories") &&
        Expect(population.FindOwner(oldId) == nullptr, "previous npc occupant retired");
}

bool TestAbsentGroupIsRetired()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "member 500 602 sim_default_stalker_0 0\n" + "npc 700 esc_m_trader stalker trader l01_escape\n" + "end\n"));
    return Expect(population.FindByBinding(xrSim::FreeplayBinding::Group, 510) == nullptr, "absent group retired") &&
        Expect(population.OwnerOfNpc(611) == nullptr, "absent group member has no owner") &&
        Expect(population.Owners().size() == 2, "two owners remain");
}

bool TestSplitInheritsObjective()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const uint64_t owner500 = RequireGroup(population, 500).ownerId;
    RequireSetup(population.SetObjective(owner500, "move_to", "esc_smart_c", xrSim::FreeplayObjectiveSource::Recorded));
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "group 520 esc_split stalker stalker l01_escape esc_smart_a - sim 1\n" +
        "member 520 602 sim_default_stalker_0 1\n" + kGroup510AndTrader + "end\n"));
    const xrSim::FreeplayOwner& split = RequireGroup(population, 520);
    const xrSim::FreeplayOwner& original = RequireGroup(population, 500);
    return Expect(HasObjective(split, "move_to", "esc_smart_c", xrSim::FreeplayObjectiveSource::Inherited),
               "split group inherits verb and site") &&
        Expect(split.ownerId != original.ownerId, "split group is a new owner") &&
        Expect(original.membershipRevision == 2, "original membership revision advances") &&
        Expect(HasObjective(original, "move_to", "esc_smart_c", xrSim::FreeplayObjectiveSource::Recorded) &&
                original.objective.revision == 2,
            "original keeps its recorded objective");
}

bool TestJoinAdoptsGroupObjective()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const uint64_t owner500 = RequireGroup(population, 500).ownerId;
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "member 500 602 sim_default_stalker_0 0\n" + "member 500 611 dog_weak 0\n" +
        "npc 700 esc_m_trader stalker trader l01_escape\n" + "end\n"));
    const xrSim::FreeplayOwner* joined = population.OwnerOfNpc(611);
    return Expect(joined != nullptr && joined->ownerId == owner500, "joining npc belongs to the group owner") &&
        Expect(joined != nullptr && HasObjective(*joined, "move_to", "esc_smart_b",
                                        xrSim::FreeplayObjectiveSource::Bootstrap),
            "joining npc gets the group objective") &&
        Expect(population.FindByBinding(xrSim::FreeplayBinding::Group, 510) == nullptr, "emptied owner retired");
}

bool TestBeginWorldDropsOwnersAndAdvancesEpoch()
{
    xrSim::FreeplayPopulation population;
    const uint32_t epoch = population.Epoch();
    RequireSetup(population.Sync(kTwoGroups));
    uint64_t maxId = 0;
    for (const xrSim::FreeplayOwner& owner : population.Owners())
        maxId = owner.ownerId > maxId ? owner.ownerId : maxId;
    const uint32_t next = population.BeginWorld();
    bool ok = Expect(next == epoch + 1 && population.Epoch() == next, "epoch advances by one") &&
        Expect(population.Owners().empty(), "begin world drops every owner") &&
        Expect(population.TakeChangedOwners().empty(), "begin world drops pending changes");
    RequireSetup(population.Sync(kTwoGroups));
    for (const xrSim::FreeplayOwner& owner : population.Owners())
        ok = Expect(owner.ownerId > maxId, "owner ids are never reused across worlds") && ok;
    return ok;
}

bool TestChangedOwnersReportedOnce()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const std::vector<uint64_t> first = population.TakeChangedOwners();
    bool ok = Expect(first == std::vector<uint64_t>{1, 2, 3}, "first sync reports every owner");
    ok = Expect(population.TakeChangedOwners().empty(), "changes are reported once") && ok;
    RequireSetup(population.Sync(kTwoGroups));
    ok = Expect(population.TakeChangedOwners().empty(), "identical resync reports nothing") && ok;
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 602 sim_default_stalker_0 1\n" +
        kGroup510AndTrader + "end\n"));
    return Expect(population.TakeChangedOwners() == std::vector<uint64_t>{RequireGroup(population, 500).ownerId},
               "leader loss reports owner 500 only") && ok;
}

bool TestCensusLevelFilter()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const std::string garbage = population.FormatCensus("l02_garbage");
    const std::string escape = population.FormatCensus("l01_escape");
    return Expect(SummaryLine(garbage) == "summary npcs=0 owners=0 transferable=0 script_owned=0 unbound=0",
               "other level census is empty") &&
        Expect(garbage.find("npc ") == std::string::npos, "other level census lists no npcs") &&
        Expect(escape == population.FormatCensus(""), "matching level keeps every line");
}

bool TestObjectiveAndMemoryValidation()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const uint64_t owner = RequireGroup(population, 500).ownerId;
    bool ok = Expect(!population.SetObjective(owner, "attack", "x", xrSim::FreeplayObjectiveSource::Live).ok,
        "unknown verb rejected");
    ok = Expect(!population.SetObjective(owner, "move_to", "", xrSim::FreeplayObjectiveSource::Live).ok,
        "empty site rejected") && ok;
    ok = Expect(!population.SetObjective(owner, "move_to", "a b", xrSim::FreeplayObjectiveSource::Live).ok,
        "site with space rejected") && ok;
    ok = Expect(!population.SetObjective(999, "hold", "-", xrSim::FreeplayObjectiveSource::Live).ok,
        "unknown owner rejected") && ok;
    ok = Expect(RequireGroup(population, 500).objective.revision == 1, "rejected objective leaves revision") && ok;
    for (int i = 0; i < 10; ++i)
        RequireSetup(population.AddMemory(owner, "m" + std::to_string(i)));
    const std::vector<std::string>& memories = RequireGroup(population, 500).memories;
    ok = Expect(memories.size() == 8 && memories.front() == "m2" && memories.back() == "m9",
        "memories keep the newest eight") && ok;
    ok = Expect(!population.AddMemory(owner, "two words").ok && !population.AddMemory(owner, "").ok,
        "memory must be one token") && ok;
    return Expect(std::string(xrSim::FreeplayWriterName(xrSim::FreeplayWriter::Hostage)) == "hostage" &&
            std::string(xrSim::FreeplaySourceName(xrSim::FreeplayObjectiveSource::Inherited)) == "inherited",
        "writer and source names") && ok;
}

const char* kSquadObservation =
    "xrsim_freeplay_observation_v1\n"
    "owner 1 1\n"
    "situation quiet\n"
    "site esc_smart_a 0 base 2\n"
    "site esc_smart_b 120 camp 0\n"
    "site esc_smart_c 340 anomaly_field 1\n"
    "sighting esc_dogs mutant monster_predatory_day 1 80 enemy\n"
    "end\n";

std::string ObservationFor(uint64_t ownerId, uint32_t revision, size_t sites, size_t sightings)
{
    std::string text = "xrsim_freeplay_observation_v1\nowner " + std::to_string(ownerId) + " " +
        std::to_string(revision) + "\nsituation quiet\n";
    for (size_t i = 0; i < sites; ++i)
        text += "site esc_smart_" + std::to_string(i) + " " + std::to_string(10 * i) + " camp 0\n";
    for (size_t i = 0; i < sightings; ++i)
        text += "sighting dogs_" + std::to_string(i) + " mutant monster 2 50 enemy\n";
    return text + "end\n";
}

xrSim::FreeplayObservation RequireObservation(const xrSim::FreeplayPopulation& population, const std::string& text)
{
    const xrSim::FreeplayObservationParse parsed = xrSim::ParseFreeplayObservation(population, text);
    if (!Expect(parsed.ok, ("test setup: observation parses: " + parsed.reason).c_str()))
        std::exit(EXIT_FAILURE);
    return parsed.observation;
}

xrSim::ActorIntentPlan PlanOf(const std::vector<std::pair<const char*, const char*>>& actions)
{
    xrSim::ActorIntentPlan plan;
    for (const auto& item : actions)
    {
        xrSim::ActorAction action;
        action.verb = item.first;
        action.target = item.second;
        plan.actions.push_back(action);
    }
    return plan;
}

bool HasReason(const xrSim::FreeplayVerdict& verdict, const char* reason)
{
    return !verdict.ok && verdict.reason == reason;
}

bool TestObservationParseAndLimits()
{
    xrSim::FreeplayPopulation population;
    population.BeginWorld();
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservationParse parsed = xrSim::ParseFreeplayObservation(population, kSquadObservation);
    if (!Expect(parsed.ok, ("valid observation parses: " + parsed.reason).c_str()))
        return false;
    const xrSim::FreeplayObservation& observation = parsed.observation;
    bool ok = Expect(observation.ownerId == 1 && observation.membershipRevision == 1 && observation.epoch == 1,
        "observation stamps owner, revision and epoch");
    ok = Expect(observation.situation == "quiet", "observation situation") && ok;
    ok = Expect(observation.sites.size() == 3 && observation.sites[2].name == "esc_smart_c" &&
            observation.sites[2].distanceMeters == 340 && observation.sites[2].kind == "anomaly_field" &&
            observation.sites[2].occupants == 1,
        "observation sites") && ok;
    ok = Expect(observation.sightings.size() == 1 && observation.sightings[0].name == "esc_dogs" &&
            observation.sightings[0].species == "mutant" &&
            observation.sightings[0].faction == "monster_predatory_day" && observation.sightings[0].count == 1 &&
            observation.sightings[0].distanceMeters == 80 && observation.sightings[0].relation == "enemy",
        "observation sightings") && ok;

    std::string joined = kSquadObservation;
    for (char& c : joined)
        if (c == '\n')
            c = ';';
    ok = Expect(xrSim::ParseFreeplayObservation(population, joined).ok, "semicolon separated observation parses") &&
        ok;
    ok = Expect(xrSim::ParseFreeplayObservation(population, ObservationFor(1, 1, 8, 6)).ok,
        "eight sites and six sightings parse") && ok;

    const std::vector<std::pair<std::string, const char*>> invalid{
        {ObservationFor(1, 1, 9, 0), "too_many_sites"},
        {ObservationFor(1, 1, 0, 7), "too_many_sightings"},
        {ObservationFor(1, 2, 1, 0), "stale_membership"},
        {ObservationFor(99, 1, 1, 0), "unknown_owner"},
        {"xrsim_freeplay_observation_v2\nowner 1 1\nsituation quiet\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nsituation quiet\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsite a 1x camp 0\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsite a 1 camp\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsite - 1 camp 0\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsighting a robot f 1 2 enemy\nend\n",
            "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsighting a mutant f 1 2 hungry\nend\n",
            "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nsquad 1\nend\n", "malformed"},
        {"xrsim_freeplay_observation_v1\nowner 1 1\nsituation quiet\nend\nsite a 1 camp 0\n", "malformed"},
    };
    for (const auto& item : invalid)
    {
        const xrSim::FreeplayObservationParse result = xrSim::ParseFreeplayObservation(population, item.first);
        ok = Expect(!result.ok && result.reason == item.second,
            ("observation rejected with " + std::string(item.second) + ", got " + result.reason).c_str()) && ok;
    }
    return ok;
}

bool TestWakePromptListsSitesAndRules()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    RequireSetup(population.AddMemory(1, "ambushed_at_bridge"));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    const xrSim::FreeplayOwner& squad = RequireGroup(population, 500);
    const xrSim::ActorWakeContext context = xrSim::BuildFreeplayWake(squad, observation, "membership_changed");
    const std::string& prompt = context.prompt;
    bool ok = Expect(prompt.rfind("xrsim_freeplay_wake_v1\n", 0) == 0, "prompt starts with the wake header");
    for (const xrSim::FreeplaySite& site : observation.sites)
        ok = Expect(prompt.find("site " + site.name + " ") != std::string::npos, "prompt lists every site") && ok;
    ok = Expect(prompt.find("sighting esc_dogs mutant monster_predatory_day 1 80 enemy\n") != std::string::npos,
        "prompt lists every sighting") && ok;
    const char* expectedLines[] = {"kind squad\n", "faction stalker\n", "level l01_escape\n", "members 2\n",
        "current_site esc_smart_a\n", "objective move_to esc_smart_b source bootstrap\n",
        "memory ambushed_at_bridge\n", "wake_reasons membership_changed\n",
        "legal_tools stay move_to retreat author_memory\n", "response_record action stay\n",
        "response_record action move_to <site_name>\n", "response_record action retreat <site_name>\n",
        "site_name_must_be_listed\n", "response_first_line xrsim_actor_intent_v1\n", "response_alternative coast\n"};
    for (const char* line : expectedLines)
        ok = Expect(prompt.find(line) != std::string::npos, (std::string("prompt line ") + line).c_str()) && ok;
    // Field names and units come before the first site and sighting line.
    const char* schemaLines[] = {"site_fields name graph_distance kind occupants\n",
        "site_graph_distance game_graph_distance_stock_ranks_targets_by_not_metres\n",
        "sighting_fields name species faction count distance_m relation\n", "sighting_distance_m metres\n"};
    const size_t firstSite = prompt.find("\nsite esc_smart_a ");
    const size_t firstSighting = prompt.find("\nsighting esc_dogs ");
    for (const char* line : schemaLines)
    {
        const size_t at = prompt.find(line);
        ok = Expect(at != std::string::npos && at < firstSite && at < firstSighting,
            (std::string("schema line before the observation: ") + line).c_str()) && ok;
    }
    ok = Expect(prompt.find("adjust_population") == std::string::npos &&
            prompt.find("<legal_tool>") == std::string::npos,
        "prompt drops the actor tool lines") && ok;
    ok = Expect(context.actor.agentId == 1 && context.actor.scope == xrSim::ActorAgentScope::Squad &&
            context.actor.name == "esc_stalkers",
        "wake actor record") && ok;
    ok = Expect(!context.observation.empty() && prompt.find(context.observation) != std::string::npos,
        "prompt embeds the observation") && ok;
    const xrSim::FreeplayOwner* dogs = population.OwnerOfNpc(611);
    const xrSim::ActorWakeContext dogWake = xrSim::BuildFreeplayWake(*dogs, observation, "timer");
    return Expect(dogWake.actor.scope == xrSim::ActorAgentScope::MutantPack, "mutant owners wake as packs") && ok;
}

bool TestDecisionAcceptsListedSite()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    xrSim::ActorIntentPlan plan = PlanOf({{"move_to", "esc_smart_c"}, {"author_memory", "saw_dogs"}});
    plan.memories.push_back("dogs_near_bridge");
    const xrSim::FreeplayVerdict verdict = xrSim::ValidateFreeplayDecision(population, observation, plan);
    bool ok = Expect(verdict.ok && !verdict.keep && verdict.verb == "move_to" && verdict.site == "esc_smart_c",
        ("move_to listed site accepted: " + verdict.reason).c_str());
    ok = Expect(verdict.memories == std::vector<std::string>{"dogs_near_bridge", "saw_dogs"},
        "verdict carries plan memories, then author_memory targets") && ok;

    const xrSim::ActorIntentParseResult stay =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\naction stay\nmemory quiet_night\nend\n");
    ok = Expect(stay.ok, ("model text with action stay parses: " + stay.reason).c_str()) && ok;
    const xrSim::FreeplayVerdict stayed = xrSim::ValidateFreeplayDecision(population, observation, stay.plan);
    ok = Expect(stayed.ok && stayed.verb == "stay" && stayed.site == "esc_smart_a" &&
            stayed.memories == std::vector<std::string>{"quiet_night"},
        ("parsed stay uses the owner site: " + stayed.reason).c_str()) && ok;

    const xrSim::ActorIntentParseResult move =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ngoal scout\naction move_to esc_smart_b\nend\n");
    ok = Expect(move.ok, ("model text with action move_to parses: " + move.reason).c_str()) && ok;
    const xrSim::FreeplayVerdict moved = xrSim::ValidateFreeplayDecision(population, observation, move.plan);
    ok = Expect(moved.ok && moved.verb == "move_to" && moved.site == "esc_smart_b",
        ("parsed move_to accepted: " + moved.reason).c_str()) && ok;

    const xrSim::FreeplayVerdict retreat =
        xrSim::ValidateFreeplayDecision(population, observation, PlanOf({{"retreat", "esc_smart_b"}}));
    return Expect(retreat.ok && retreat.verb == "retreat" && retreat.site == "esc_smart_b",
               ("retreat to listed site accepted: " + retreat.reason).c_str()) && ok;
}

bool TestDecisionRejections()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    const xrSim::ActorIntentPlan move = PlanOf({{"move_to", "esc_smart_c"}});

    xrSim::FreeplayObservation unknown = observation;
    unknown.ownerId = 99;
    bool ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, unknown, move), "unknown_owner"),
        "unknown_owner");
    xrSim::FreeplayObservation oldWorld = observation;
    oldWorld.epoch = population.Epoch() + 1;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, oldWorld, move), "stale_world"),
        "stale_world") && ok;
    xrSim::FreeplayObservation oldMembers = observation;
    oldMembers.membershipRevision = 7;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, oldMembers, move), "stale_membership"),
        "stale_membership") && ok;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation,
                              PlanOf({{"move_to", "esc_smart_c"}, {"fire_pattern", "x"}})),
                    "illegal_tool"),
        "illegal_tool") && ok;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation,
                              PlanOf({{"author_memory", "saw_dogs"}})),
                    "need_one_objective"),
        "need_one_objective with no movement verb") && ok;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation,
                              PlanOf({{"stay", ""}, {"move_to", "esc_smart_c"}})),
                    "need_one_objective"),
        "need_one_objective with two movement verbs") && ok;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation,
                              PlanOf({{"move_to", "esc_smart_z"}})),
                    "unknown_site"),
        "unknown_site for move_to") && ok;
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation, PlanOf({{"retreat", ""}})),
                    "unknown_site"),
        "unknown_site for retreat without target") && ok;

    const xrSim::FreeplayObservation dogs =
        RequireObservation(population, "xrsim_freeplay_observation_v1\nowner 2 1\nsituation quiet\nend\n");
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, dogs, PlanOf({{"stay", ""}})), "no_site"),
        "no_site") && ok;
    const xrSim::FreeplayObservation trader =
        RequireObservation(population, ObservationFor(3, 1, 3, 0));
    return Expect(HasReason(xrSim::ValidateFreeplayDecision(population, trader, PlanOf({{"move_to", "esc_smart_1"}})),
                      "constrained"),
               "constrained") && ok;
}

bool TestConstrainedOwnerOnlyStays()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync("xrsim_freeplay_sync_v1\n"
                                 "group 500 esc_pals stalker stalker l01_escape esc_smart_a - companion 1\n"
                                 "member 500 601 sim_default_stalker_1 1\n"
                                 "npc 700 esc_m_trader stalker trader l01_escape\n"
                                 "end\n"));
    const xrSim::FreeplayOwner& companion = RequireGroup(population, 500);
    const xrSim::FreeplayObservation companionView =
        RequireObservation(population, ObservationFor(companion.ownerId, 1, 3, 0));
    bool ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, companionView,
                                   PlanOf({{"move_to", "esc_smart_1"}})),
                         "constrained"),
        "writer companion cannot move");
    ok = Expect(HasReason(xrSim::ValidateFreeplayDecision(population, companionView,
                              PlanOf({{"retreat", "esc_smart_1"}})),
                    "constrained"),
        "writer companion cannot retreat") && ok;
    const xrSim::FreeplayVerdict stays =
        xrSim::ValidateFreeplayDecision(population, companionView, PlanOf({{"stay", ""}}));
    ok = Expect(stays.ok && stays.verb == "stay" && stays.site == "esc_smart_a",
        ("writer companion may stay: " + stays.reason).c_str()) && ok;

    const xrSim::FreeplayOwner* trader = population.FindByBinding(xrSim::FreeplayBinding::Npc, 700);
    if (!Expect(trader != nullptr, "npc owner exists"))
        return false;
    const xrSim::FreeplayObservation traderView =
        RequireObservation(population, ObservationFor(trader->ownerId, 1, 3, 0));
    return Expect(HasReason(xrSim::ValidateFreeplayDecision(population, traderView,
                                PlanOf({{"move_to", "esc_smart_1"}})),
                      "constrained"),
               "npc-bound owner cannot move") && ok;
}

bool TestCoastKeepsObjective()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    const xrSim::ActorIntentParseResult coast = xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ncoast\nend\n");
    if (!Expect(coast.ok, "coast text parses"))
        return false;
    const xrSim::FreeplayVerdict verdict = xrSim::ValidateFreeplayDecision(population, observation, coast.plan);
    return Expect(verdict.ok && verdict.keep, "coast keeps the objective");
}

bool TestStaleAfterSplitRejected()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    RequireSetup(population.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "group 520 esc_split stalker stalker l01_escape esc_smart_a - sim 1\n" +
        "member 520 602 sim_default_stalker_0 1\n" + kGroup510AndTrader + "end\n"));
    return Expect(HasReason(xrSim::ValidateFreeplayDecision(population, observation,
                                PlanOf({{"move_to", "esc_smart_c"}})),
                      "stale_membership"),
        "observation taken before a split is stale");
}

bool TestLedgerKeepsNewest512()
{
    xrSim::FreeplayLedger ledger;
    for (int i = 0; i < 600; ++i)
    {
        xrSim::FreeplayLedgerEntry entry;
        entry.ownerId = 1;
        entry.wakeReason = "timer";
        entry.source = "live";
        entry.verdict = i == 599 ? "provider error: timed out" : "accepted";
        entry.verb = "move_to";
        entry.site = "esc_smart_c";
        ledger.Append(entry);
    }
    const std::string last = ledger.Format(1);
    bool ok = Expect(ledger.Size() == 512, "ledger keeps the newest 512");
    ok = Expect(last.find("serial=600 ") != std::string::npos, "newest entry has serial 600") && ok;
    ok = Expect(std::count(last.begin(), last.end(), '\n') == 1, "format(1) prints one line") && ok;
    ok = Expect(last.find("ownerId=1 ") != std::string::npos && last.find("wakeReason=timer ") != std::string::npos &&
            last.find("queueAgeMs=0") != std::string::npos && last.find("verdict=provider_error:_timed_out ") !=
            std::string::npos && last.find("provider=- ") != std::string::npos,
        ("ledger line fields: " + last).c_str()) && ok;
    const std::string all = ledger.Format(1000);
    ok = Expect(std::count(all.begin(), all.end(), '\n') == 512, "format prints at most the kept entries") && ok;
    return Expect(all.rfind("serial=89 ", 0) == 0, "oldest kept entry is serial 89") && ok;
}

// Five transferable one-member squads; owner ids 1..5 in group order.
std::string FiveSquads()
{
    std::string text = "xrsim_freeplay_sync_v1\n";
    for (int i = 0; i < 5; ++i)
    {
        const std::string id = std::to_string(500 + i);
        text += "group " + id + " esc_squad_" + std::to_string(i) + " stalker stalker l01_escape esc_smart_a - sim 1\n";
        text += "member " + id + " " + std::to_string(600 + i) + " sim_default_stalker_" + std::to_string(i) + " 1\n";
    }
    return text + "end\n";
}

using OwnerIds = std::vector<uint64_t>;

// Hands an owner out and settles it with one decision, as Task 4's tick will.
void RequireDecided(xrSim::FreeplayScheduler& scheduler, const xrSim::FreeplayPopulation& population,
    uint64_t ownerId, bool providerFailed, uint64_t takeMs, uint64_t decideMs)
{
    scheduler.RequestWake(ownerId, "timer", takeMs);
    if (!Expect(scheduler.TakeObservationRequests(population, takeMs) == OwnerIds{ownerId},
            "test setup: owner handed out"))
        std::exit(EXIT_FAILURE);
    scheduler.OnObservation(ownerId, takeMs);
    scheduler.OnDecision(ownerId, providerFailed, decideMs);
}

bool TestDuplicateWakesCoalesce()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    xrSim::FreeplayScheduler scheduler;
    scheduler.RequestWake(1, "sighting", 0);
    scheduler.RequestWake(1, "arrived", 1);
    scheduler.RequestWake(1, "sighting", 2);
    bool ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::Queued, "woken owner is queued");
    ok = Expect(scheduler.Reasons(1) == "sighting,arrived", "distinct reasons in arrival order") && ok;
    ok = Expect(scheduler.QueuedAtMs(1) == 0, "queue time is the first wake") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 3) == OwnerIds{1}, "three wakes make one request") && ok;
    ok = Expect(scheduler.InFlight() == 1, "one request in flight") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 4).empty(), "no second request") && ok;

    scheduler.RequestWake(1, "membership_changed", 5);
    ok = Expect(scheduler.Reasons(1) == "sighting,arrived", "in-flight reasons kept until the decision") && ok;
    ok = Expect(scheduler.QueuedAtMs(1) == 0, "in-flight queue time kept until the decision") && ok;
    scheduler.OnObservation(1, 6);
    scheduler.OnDecision(1, false, 7);
    ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::Queued, "wake during flight queues the owner again") &&
        ok;
    return Expect(scheduler.Reasons(1) == "membership_changed" && scheduler.QueuedAtMs(1) == 5,
               "pending wake becomes the queued wake") && ok;
}

bool TestConcurrencyBoundAndFairness()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(FiveSquads()));
    xrSim::FreeplayScheduler scheduler;
    // Queued newest-id first so that age, not id, must decide the order.
    for (uint64_t t = 0; t < 5; ++t)
        scheduler.RequestWake(5 - t, "timer", t);
    bool ok = Expect(scheduler.TakeObservationRequests(population, 4) == OwnerIds{5, 4}, "two oldest handed out");
    ok = Expect(scheduler.InFlight() == 2, "in flight is bounded at two") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 5).empty(), "full slots hand out nothing") && ok;
    scheduler.OnObservation(5, 6);
    ok = Expect(scheduler.State(5) == xrSim::FreeplayWakeState::AwaitingDecision &&
            scheduler.State(4) == xrSim::FreeplayWakeState::AwaitingObservation,
        "observation advances one owner") && ok;
    scheduler.OnDecision(5, false, 7);
    ok = Expect(scheduler.State(5) == xrSim::FreeplayWakeState::Idle && scheduler.Reasons(5).empty(),
        "decided owner is idle with no reasons") && ok;
    return Expect(scheduler.TakeObservationRequests(population, 8) == OwnerIds{3}, "freed slot goes to the third") &&
        ok;
}

bool TestMinWakeInterval()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    xrSim::FreeplayScheduler scheduler;
    RequireDecided(scheduler, population, 1, false, 0, 1000);
    scheduler.RequestWake(1, "sighting", 2000);
    bool ok = Expect(scheduler.TakeObservationRequests(population, 40000).empty(), "recently decided owner waits");
    ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::Queued, "waiting owner stays queued") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 45999).empty(), "still waits 1ms before the interval") &&
        ok;
    return Expect(scheduler.TakeObservationRequests(population, 46000) == OwnerIds{1},
               "owner handed out after the interval") && ok;
}

bool TestPeriodicReconsider()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    xrSim::FreeplayScheduler scheduler;
    bool ok = Expect(scheduler.TakeObservationRequests(population, 599999).empty(), "no reconsider before interval");
    const OwnerIds taken = scheduler.TakeObservationRequests(population, 600000);
    ok = Expect(taken == OwnerIds{1, 2}, "idle transferable owners reconsidered") && ok;
    ok = Expect(scheduler.Reasons(1) == "reconsider" && scheduler.Reasons(2) == "reconsider",
        "reconsider reason") && ok;
    ok = Expect(scheduler.State(3) == xrSim::FreeplayWakeState::Idle, "npc-bound owner not reconsidered") && ok;

    scheduler.OnObservation(1, 600000);
    scheduler.OnDecision(1, false, 600000);
    ok = Expect(scheduler.TakeObservationRequests(population, 1199999).empty(), "idle clock restarts at decision") &&
        ok;
    return Expect(scheduler.TakeObservationRequests(population, 1200000) == OwnerIds{1},
               "decided owner reconsidered one interval later") && ok;
}

bool TestConstrainedOwnersAreNotWoken()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync("xrsim_freeplay_sync_v1\n"
                                 "group 500 esc_pals stalker stalker l01_escape esc_smart_a - companion 1\n"
                                 "member 500 601 sim_default_stalker_1 1\n"
                                 "npc 700 esc_m_trader stalker trader l01_escape\n"
                                 "end\n"));
    const uint64_t companion = RequireGroup(population, 500).ownerId;
    const xrSim::FreeplayOwner* trader = population.FindByBinding(xrSim::FreeplayBinding::Npc, 700);
    if (!Expect(trader != nullptr, "npc owner exists"))
        return false;
    xrSim::FreeplayScheduler scheduler;
    scheduler.RequestWake(companion, "sighting", 0);
    scheduler.RequestWake(trader->ownerId, "sighting", 0);
    bool ok = Expect(scheduler.TakeObservationRequests(population, 0).empty(), "constrained wakes hand out nothing");
    ok = Expect(scheduler.State(companion) == xrSim::FreeplayWakeState::Idle &&
            scheduler.State(trader->ownerId) == xrSim::FreeplayWakeState::Idle && scheduler.Reasons(companion).empty(),
        "constrained wakes dropped") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 600000).empty(), "constrained owners not reconsidered") &&
        ok;
    return Expect(scheduler.TakeObservationRequests(population, 6000000).empty() && scheduler.InFlight() == 0,
               "constrained owners never woken") && ok;
}

bool TestProviderFailureBacksOff()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(FiveSquads()));
    xrSim::FreeplayScheduler scheduler;
    RequireDecided(scheduler, population, 1, true, 0, 0);
    scheduler.RequestWake(2, "timer", 0);
    bool ok = Expect(scheduler.BackoffActive(0) && scheduler.BackoffActive(29999), "failure starts a backoff");
    ok = Expect(scheduler.TakeObservationRequests(population, 29999).empty(), "nothing handed out during backoff") && ok;
    ok = Expect(scheduler.State(2) == xrSim::FreeplayWakeState::Queued, "wake kept during backoff") && ok;
    ok = Expect(!scheduler.BackoffActive(30000), "backoff ends after 30000") && ok;
    ok = Expect(scheduler.TakeObservationRequests(population, 30000) == OwnerIds{2}, "handed out after backoff") && ok;

    scheduler.OnObservation(2, 30000);
    scheduler.OnDecision(2, true, 30000);
    ok = Expect(scheduler.BackoffActive(89999) && !scheduler.BackoffActive(90000), "second failure doubles to 60000") &&
        ok;
    RequireDecided(scheduler, population, 3, true, 90000, 90000);
    ok = Expect(scheduler.BackoffActive(209999) && !scheduler.BackoffActive(210000), "third failure doubles to 120000") &&
        ok;
    RequireDecided(scheduler, population, 4, false, 210000, 210000);
    ok = Expect(!scheduler.BackoffActive(210000), "success clears the backoff") && ok;
    RequireDecided(scheduler, population, 5, true, 210000, 210000);
    ok = Expect(scheduler.BackoffActive(239999) && !scheduler.BackoffActive(240000), "success resets to 30000") && ok;

    xrSim::FreeplayPopulation single;
    RequireSetup(single.Sync(std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\nend\n"));
    xrSim::FreeplayScheduler capped;
    uint64_t now = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (i > 0)
            now += 600000;
        RequireDecided(capped, single, 1, true, now, now);   // backoffs 30000 .. 480000, then capped
    }
    return Expect(capped.BackoffActive(now + 479999) && !capped.BackoffActive(now + 480000),
               "backoff capped at 480000") && ok;
}

bool TestObservationTimeoutFreesSlot()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    xrSim::FreeplayScheduler scheduler;
    scheduler.RequestWake(1, "sighting", 0);
    scheduler.TakeObservationRequests(population, 0);
    bool ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::AwaitingObservation, "owner awaits observation");
    ok = Expect(scheduler.TakeTimedOutObservations(9999).empty(), "not timed out before the timeout") && ok;
    ok = Expect(scheduler.TakeTimedOutObservations(10000) == OwnerIds{1}, "timed out at the timeout") && ok;
    ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::Idle && scheduler.InFlight() == 0,
        "timed out owner is idle and frees its slot") && ok;
    return Expect(scheduler.TakeTimedOutObservations(20000).empty(), "timeout reported once") && ok;
}

bool TestForgetRemovesOwner()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    xrSim::FreeplayScheduler scheduler;
    scheduler.RequestWake(1, "sighting", 0);
    scheduler.TakeObservationRequests(population, 0);
    scheduler.OnObservation(1, 1);
    bool ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::AwaitingDecision && scheduler.InFlight() == 1,
        "owner awaits decision");
    scheduler.Forget(1);
    ok = Expect(scheduler.InFlight() == 0 && scheduler.State(1) == xrSim::FreeplayWakeState::Idle,
        "forget frees the slot") && ok;
    scheduler.OnDecision(1, true, 2);
    ok = Expect(scheduler.State(1) == xrSim::FreeplayWakeState::Idle && scheduler.InFlight() == 0 &&
            !scheduler.BackoffActive(2) && scheduler.Reasons(1).empty(),
        "decision after forget is a no-op") && ok;

    scheduler.RequestWake(2, "sighting", 3);
    RequireSetup(population.Retire(xrSim::FreeplayBinding::Group, 510));
    ok = Expect(scheduler.TakeObservationRequests(population, 4).empty(), "retired owner not handed out") && ok;
    return Expect(scheduler.State(2) == xrSim::FreeplayWakeState::Idle && scheduler.Reasons(2).empty(),
               "retired owner dropped from the scheduler") && ok;
}

// --- ai.freeplay.* verbs through the bridge ---

struct VerbReply
{
    bool ok = false;
    std::string text;
};

VerbReply Verb(const std::string& verb, const std::string& payload = std::string())
{
    VerbReply reply;
    reply.text = xrSim::HandleBridgeVerb(verb, payload, reply.ok);
    return reply;
}

std::string RequireVerb(const std::string& verb, const std::string& payload = std::string())
{
    const VerbReply reply = Verb(verb, payload);
    if (!Expect(reply.ok, ("test setup: " + verb + " " + payload + " -> " + reply.text).c_str()))
        std::exit(EXIT_FAILURE);
    return reply.text;
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void SetEnvironment(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

// Every variable the live provider reads, so no test depends on the developer's environment.
void ClearProviderEnvironment()
{
    for (const char* name : {"XRAY_AGENT_PROVIDER", "XRAY_AGENT_MODEL", "XRAY_AGENT_API_KEY", "ANTHROPIC_API_KEY",
             "XRAY_AGENT_TIMEOUT_MS", "XRAY_AGENT_CLI", "XRAY_FREEPLAY_MAX_IN_FLIGHT", "XRAY_FREEPLAY_CALLS_PER_MINUTE",
             "XRAY_FREEPLAY_RECONSIDER_MS", "XRAY_FREEPLAY_MIN_WAKE_MS"})
        SetEnvironment(name, nullptr);
}

// Fresh freeplay runtime in the given mode with kTwoGroups enrolled (owners 1..3 in epoch 1).
void StartFreeplay(const char* mode)
{
    RequireVerb("ai.reset");
    RequireVerb("ai.freeplay.mode", mode);
    RequireVerb("ai.freeplay.begin");
    RequireSetup(xrSim::FreeplayResult{RequireVerb("ai.freeplay.sync", kTwoGroups) == "owners=3", "owners=3"});
}

void StartLiveWith(std::unique_ptr<xrSim::IActorIntentProvider> provider, const std::string& snapshot)
{
    RequireVerb("ai.reset");
    xrSim::SetFreeplayProviderForTests(std::move(provider));
    RequireVerb("ai.freeplay.mode", "live");
    RequireVerb("ai.freeplay.begin");
    RequireSetup(xrSim::FreeplayResult{RequireVerb("ai.freeplay.sync", snapshot).rfind("owners=", 0) == 0, "sync"});
}

void StartLive(const std::vector<std::string>& script)
{
    StartLiveWith(std::make_unique<xrSim::RecordedActorIntentProvider>(script), kTwoGroups);
}

// Answers every wake with a coast; a non-empty reason marks it as provider-originated.
class CoastProvider final : public xrSim::IActorIntentProvider
{
public:
    explicit CoastProvider(const char* reason) : m_reason(reason) {}
    xrSim::ActorProviderResult Wake(const xrSim::ActorWakeContext&) override
    {
        xrSim::ActorProviderResult result;
        result.ok = true;
        result.coast = true;
        result.plan.coast = true;
        result.coastReason = m_reason;
        result.provider = "test-coast";
        result.model = "fixture";
        return result;
    }

private:
    std::string m_reason;
};

// Answers every wake as the Anthropic provider answers a reply whose text does not decode or parse:
// ok=false with the parser's reason and no coast reason (xrSimActorProvider.cpp FailActor).
class MalformedProvider final : public xrSim::IActorIntentProvider
{
public:
    xrSim::ActorProviderResult Wake(const xrSim::ActorWakeContext&) override
    {
        xrSim::ActorProviderResult result;
        result.provider = "test-malformed";
        result.model = "fixture";
        result.error = "invalid action record";
        return result;
    }
};

// Replies from a script in order and keeps every prompt it was sent.
class CapturingProvider final : public xrSim::IActorIntentProvider
{
public:
    CapturingProvider(std::vector<std::string> replies, std::shared_ptr<std::vector<std::string>> prompts)
        : m_replies(std::move(replies)), m_prompts(std::move(prompts))
    {
    }
    xrSim::ActorProviderResult Wake(const xrSim::ActorWakeContext& context) override
    {
        m_prompts->push_back(context.prompt);
        const std::string reply = m_cursor < m_replies.size() ? m_replies[m_cursor++] : std::string();
        xrSim::ActorProviderResult result;
        const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(reply);
        result.ok = parsed.ok;
        result.error = parsed.reason;
        result.plan = parsed.plan;
        result.provider = "test-capture";
        result.model = "fixture";
        return result;
    }

private:
    std::vector<std::string> m_replies;
    size_t m_cursor = 0;
    std::shared_ptr<std::vector<std::string>> m_prompts;
};

// Ticks at nowMs until the ledger contains needle; returns every tick line seen.
std::string TickUntilLogged(const std::string& needle, uint64_t nowMs)
{
    std::string ticks;
    for (int i = 0; i < 200; ++i)
    {
        ticks += RequireVerb("ai.freeplay.tick", std::to_string(nowMs));
        if (Contains(RequireVerb("ai.freeplay.log"), needle))
            return ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Expect(false, ("ledger never showed " + needle).c_str());
    return ticks;
}

std::string OwnerLine(uint64_t ownerId)
{
    std::istringstream lines(RequireVerb("ai.freeplay.owners"));
    std::string line;
    const std::string prefix = "owner " + std::to_string(ownerId) + " ";
    while (std::getline(lines, line))
        if (line.rfind(prefix, 0) == 0)
            return line;
    return std::string();
}

const char* kLiveMoveReply = "xrsim_actor_intent_v1\ngoal regroup\naction move_to esc_smart_c\nend\n";

bool TestRecordedDecisionFlow()
{
    StartFreeplay("recorded");
    const std::string first = RequireVerb("ai.freeplay.tick", "0");
    bool ok = Expect(Contains(first, "observe 1 group 500 1\n"), ("first tick asks owner 1 to observe: " + first).c_str());
    ok = Expect(Contains(first, "apply 1 500 1 move_to esc_smart_b bootstrap\n"), "first tick applies the bootstrap") &&
        ok;
    ok = Expect(!Contains(first, "observe 3 "), "npc-bound owner never observes") && ok;
    ok = Expect(Verb("ai.freeplay.observe", kSquadObservation).text == "stored", "recorded observe stores") && ok;
    const VerbReply decided = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    ok = Expect(decided.ok && decided.text == "accepted move_to esc_smart_c", ("decide: " + decided.text).c_str()) && ok;
    const std::string second = RequireVerb("ai.freeplay.tick", "1");
    ok = Expect(Contains(second, "apply 1 500 2 move_to esc_smart_c recorded\n"),
        ("next tick applies the recorded decision: " + second).c_str()) && ok;
    ok = Expect(!Contains(second, "apply 2 "), "unchanged owner is not applied again") && ok;
    ok = Expect(OwnerLine(1) == "owner 1 name=esc_stalkers kind=squad binding=group:500 writer=sim level=l01_escape "
                                "members=2 mrev=1 state=idle objective=move_to:esc_smart_c source=recorded rev=2 "
                                "sites=esc_smart_a,esc_smart_b,esc_smart_c",
        ("owners line: " + OwnerLine(1)).c_str()) && ok;
    const std::string log = RequireVerb("ai.freeplay.log");
    return Expect(Contains(log, "ownerId=1 epoch=1 membershipRevision=1 wakeReason=spawn source=recorded "
                                "verdict=accepted verb=move_to site=esc_smart_c"),
               ("decision ledger line: " + log).c_str()) && ok;
}

bool TestDecideRejectsUnknownSite()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    const VerbReply early = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    bool ok = Expect(!early.ok && early.text == "no_observation", "decide needs a stored observation");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const VerbReply rejected = Verb("ai.freeplay.decide", "1 move_to nowhere");
    ok = Expect(!rejected.ok && rejected.text == "unknown_site", ("unknown site rejected: " + rejected.text).c_str()) &&
        ok;
    ok = Expect(!Contains(RequireVerb("ai.freeplay.tick", "1"), "apply "), "rejected decision applies nothing") && ok;
    return Expect(Contains(RequireVerb("ai.freeplay.log"), "source=recorded verdict=unknown_site"),
               "rejection is logged") && ok;
}

bool TestLiveDecisionFlow()
{
    StartLive({kLiveMoveReply});
    bool ok = Expect(Contains(RequireVerb("ai.freeplay.tick", "0"), "observe 1 group 500 1\n"), "owner 1 observes");
    ok = Expect(Verb("ai.freeplay.observe", kSquadObservation).text == "submitted request=1", "live observe submits") &&
        ok;
    const std::string ticks = TickUntilLogged("verdict=accepted", 1);
    ok = Expect(Contains(ticks, "apply 1 500 2 move_to esc_smart_c live\n"), ("live apply line: " + ticks).c_str()) &&
        ok;
    const std::string log = RequireVerb("ai.freeplay.log");
    ok = Expect(Contains(log, "source=live") && Contains(log, "verdict=accepted") &&
            Contains(log, "provider=recorded-actor model=fixture"),
        ("live ledger line: " + log).c_str()) && ok;
    return Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_c source=live rev=2"), "live objective stored") &&
        ok;
}

bool TestReplyAfterBeginWorldIsStale()
{
    StartLive({kLiveMoveReply});
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", kTwoGroups);
    const std::string ticks = TickUntilLogged("stale_world", 0);
    bool ok = Expect(!Contains(ticks, " live\n"), "stale reply applies nothing");
    ok = Expect(Contains(ticks, "observe 4 group 500 1\n"), ("re-enrolled owner observes again: " + ticks).c_str()) &&
        ok;
    return Expect(Contains(OwnerLine(4), "source=bootstrap rev=1"), "re-enrolled owner keeps its bootstrap") && ok;
}

bool TestStaleReplyAfterSplit()
{
    StartLive({kLiveMoveReply});
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    RequireVerb("ai.freeplay.sync", std::string(kGroup500Header) + "member 500 601 sim_default_stalker_1 1\n" +
        "group 520 esc_split stalker stalker l01_escape esc_smart_a - sim 1\n" +
        "member 520 602 sim_default_stalker_0 1\n" + kGroup510AndTrader + "end\n");
    const std::string ticks = TickUntilLogged("stale_membership", 1);
    bool ok = Expect(!Contains(ticks, " live\n"), "stale reply applies nothing");
    return Expect(Contains(OwnerLine(1), "mrev=2 ") &&
               Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=bootstrap rev=1"),
               ("objective unchanged after split: " + OwnerLine(1)).c_str()) && ok;
}

// A provider result with ok=false is a reply that did not decode or parse: malformed, not a hold.
bool TestFailedProviderResultIsMalformed()
{
    StartLive({});
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const std::string ticks = TickUntilLogged("verdict=malformed", 1);
    bool ok = Expect(!Contains(ticks, "apply 1 "), "failed result applies nothing");
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=bootstrap rev=1"),
        "objective and revision unchanged") && ok;
    const std::string log = RequireVerb("ai.freeplay.log");
    ok = Expect(!Contains(log, "source=hold"), ("failed result is not a hold: " + log).c_str()) && ok;
    return Expect(Contains(log, "source=live verdict=malformed:recorded_actor_provider_exhausted "),
               ("the provider's reason is the verdict: " + log).c_str()) && ok;
}

// Five squads: tick 0 hands out owners 1 and 2; owner 1 observes and gets a malformed reply.
bool TestMalformedReplyDoesNotBackOff()
{
    StartLiveWith(std::make_unique<MalformedProvider>(), FiveSquads());
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.tick", "1");
    RequireVerb("ai.freeplay.observe", ObservationFor(1, 1, 1, 0));
    const std::string ticks = TickUntilLogged("verdict=malformed", 1);
    const std::string log = RequireVerb("ai.freeplay.log");
    bool ok = Expect(Contains(log, "ownerId=1 epoch=1 membershipRevision=1 wakeReason=spawn source=live "
                                   "verdict=malformed:invalid_action_record verb=- site=- provider=test-malformed "
                                   "model=fixture "),
        ("malformed reply ledger line: " + log).c_str());
    ok = Expect(!Contains(log, "source=hold"), "malformed reply is not a hold") && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=hold:- source=none rev=0"), "objective unchanged") && ok;
    ok = Expect(Contains(OwnerLine(1), "state=idle "), ("malformed owner waits idle: " + OwnerLine(1)).c_str()) && ok;
    return Expect(Contains(ticks, "observe 3 "), ("no backoff: next owner handed out at once: " + ticks).c_str()) &&
        ok;
}

// A reply for a world dropped by begin is stale_world before anything else: a failing reply from the
// old world is not a hold and starts no backoff.
bool TestFailedReplyAfterBeginIsStale()
{
    StartLiveWith(std::make_unique<CoastProvider>("transport_error"), FiveSquads());
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", ObservationFor(1, 1, 1, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));   // the worker answers at once
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", FiveSquads());   // owners 6..10 in epoch 2
    const std::string tick = RequireVerb("ai.freeplay.tick", "0");
    const std::string log = RequireVerb("ai.freeplay.log");
    bool ok = Expect(Contains(log, "ownerId=1 epoch=1 membershipRevision=1 wakeReason=spawn source=live "
                                   "verdict=stale_world "),
        ("old world's failed reply is stale_world: " + log).c_str());
    ok = Expect(!Contains(log, "source=hold"), ("old world's failed reply is not a hold: " + log).c_str()) && ok;
    return Expect(Contains(tick, "observe 6 group 500 1\n") && Contains(tick, "observe 7 group 501 1\n"),
               ("no backoff: the new world is handed out in the same tick: " + tick).c_str()) && ok;
}

void DecideMoveToC();

// The objective owner 1 holds after a recorded move to esc_smart_c: rev 2, no apply line pending.
bool TestIdenticalRecordedDecisionIsKept()
{
    DecideMoveToC();
    const VerbReply again = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    bool ok = Expect(again.ok && again.text == "kept move_to esc_smart_c", ("identical decide: " + again.text).c_str());
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_c source=recorded rev=2 "),
        ("identical decision keeps the revision: " + OwnerLine(1)).c_str()) && ok;
    ok = Expect(!Contains(RequireVerb("ai.freeplay.tick", "2"), "apply 1 "), "identical decision applies nothing") &&
        ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.log"), "source=recorded verdict=kept verb=move_to site=esc_smart_c "),
        "identical decision ledgered as kept") && ok;

    const VerbReply stay = Verb("ai.freeplay.decide", "1 stay");
    ok = Expect(stay.ok && stay.text == "accepted stay esc_smart_a", ("stay commits: " + stay.text).c_str()) && ok;
    const VerbReply stayAgain = Verb("ai.freeplay.decide", "1 stay");
    ok = Expect(stayAgain.ok && stayAgain.text == "kept stay esc_smart_a", ("second stay: " + stayAgain.text).c_str()) &&
        ok;
    ok = Expect(Contains(OwnerLine(1), "objective=stay:esc_smart_a source=recorded rev=3 "),
        ("second stay keeps the revision: " + OwnerLine(1)).c_str()) && ok;
    const std::string tick = RequireVerb("ai.freeplay.tick", "3");
    ok = Expect(Contains(tick, "apply 1 500 3 stay esc_smart_a recorded\n") && !Contains(tick, "apply 1 500 4"),
        ("one apply line for the two stays: " + tick).c_str()) && ok;

    const VerbReply other = Verb("ai.freeplay.decide", "1 move_to esc_smart_b");
    ok = Expect(other.ok && other.text == "accepted move_to esc_smart_b", ("other site commits: " + other.text).c_str()) &&
        ok;
    return Expect(Contains(RequireVerb("ai.freeplay.tick", "4"), "apply 1 500 4 move_to esc_smart_b recorded\n"),
               "a different site raises the revision") && ok;
}

// A live reply equal to the current objective is kept: same revision, no apply line, memories stored.
bool TestIdenticalLiveDecisionIsKeptWithMemories()
{
    auto prompts = std::make_shared<std::vector<std::string>>();
    const char* move = "xrsim_actor_intent_v1\naction move_to esc_smart_c\nmemory first_note\nend\n";
    const char* again = "xrsim_actor_intent_v1\naction move_to esc_smart_c\nmemory second_note\nend\n";
    StartLiveWith(std::make_unique<CapturingProvider>(std::vector<std::string>{move, again, move}, prompts), kTwoGroups);
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    bool ok = Expect(Contains(TickUntilLogged("verdict=accepted", 1), "apply 1 500 2 move_to esc_smart_c live\n"),
        "first live move applied at rev 2");

    RequireVerb("ai.freeplay.wake", "1 probe");
    ok = Expect(Contains(RequireVerb("ai.freeplay.tick", "50000"), "observe 1 group 500 1\n"), "owner 1 woken again") && ok;
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const std::string ticks = TickUntilLogged("verdict=kept", 50000);
    ok = Expect(!Contains(ticks, "apply 1 "), ("identical live reply applies nothing: " + ticks).c_str()) && ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.log"), "source=live verdict=kept verb=move_to site=esc_smart_c "),
        "identical live reply ledgered as kept") && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_c source=live rev=2 "),
        ("identical live reply keeps the revision: " + OwnerLine(1)).c_str()) && ok;

    RequireVerb("ai.freeplay.wake", "1 probe");
    RequireVerb("ai.freeplay.tick", "100000");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    for (int i = 0; i < 200; ++i)   // until the third reply settles: the provider has seen the third prompt
    {
        RequireVerb("ai.freeplay.tick", "100000");
        const std::string log = RequireVerb("ai.freeplay.log");
        size_t kept = 0;
        for (size_t at = log.find("verdict=kept"); at != std::string::npos; at = log.find("verdict=kept", at + 1))
            ++kept;
        if (kept == 2)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return Expect(prompts->size() == 3 && Contains((*prompts)[2], "memory first_note\nmemory second_note\n"),
               "the kept reply's memory is stored") && ok;
}

// Five squads: tick 0 hands out owners 1 and 2; owner 1 observes at 1 and its reply drains at 1.
std::string CoastAfterObserve(const char* reason, const char* needle)
{
    StartLiveWith(std::make_unique<CoastProvider>(reason), FiveSquads());
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.tick", "1");
    RequireVerb("ai.freeplay.observe", ObservationFor(1, 1, 1, 0));
    return TickUntilLogged(needle, 1);
}

bool TestProviderCoastHoldsAndBacksOff()
{
    const std::string ticks = CoastAfterObserve("transport_error", "source=hold");
    const std::string log = RequireVerb("ai.freeplay.log");
    bool ok = Expect(Contains(log, "source=hold verdict=transport_error "), ("provider coast is a hold: " + log).c_str());
    ok = Expect(!Contains(log, "verdict=kept"), "provider coast is not kept") && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=hold:- source=none rev=0"), "objective and revision unchanged") && ok;
    ok = Expect(!Contains(ticks, "observe ") && !Contains(RequireVerb("ai.freeplay.tick", "1"), "observe "),
        "backoff hands out nothing at the same time") && ok;
    return Expect(Contains(RequireVerb("ai.freeplay.tick", "30001"), "observe 3 "),
               "owners handed out again once the 30000 ms backoff ends") && ok;
}

bool TestModelCoastIsKept()
{
    const std::string ticks = CoastAfterObserve("", "verdict=kept");
    const std::string log = RequireVerb("ai.freeplay.log");
    bool ok = Expect(Contains(log, "source=live verdict=kept "), ("model coast is kept: " + log).c_str());
    ok = Expect(!Contains(log, "source=hold"), "model coast is not a hold") && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=hold:- source=none rev=0"), "objective unchanged") && ok;
    return Expect(Contains(ticks, "observe 3 "), ("no backoff: next owner handed out at once: " + ticks).c_str()) && ok;
}

bool TestLiveModeRefusedWithoutProvider()
{
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    const VerbReply live = Verb("ai.freeplay.mode", "live");
    bool ok = Expect(!live.ok && live.text == "missing_api_key", ("live refused: " + live.text).c_str());
    ok = Expect(RequireVerb("ai.freeplay.mode") == "mode=off", "mode still off") && ok;
    const VerbReply observe = Verb("ai.freeplay.observe", kSquadObservation);
    return Expect(!observe.ok && observe.text == "mode_off", "observe refused in mode off") && ok;
}

// Refusal order: provider_disabled, missing_api_key, missing_model, transport_unavailable. No test
// here observes, so no provider queue is created and nothing is sent anywhere.
bool TestLiveModeRefusalOrder()
{
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    SetEnvironment("XRAY_AGENT_PROVIDER", "off");
    SetEnvironment("XRAY_AGENT_API_KEY", "not-a-real-key");
    SetEnvironment("XRAY_AGENT_MODEL", "test-model");
    const VerbReply disabled = Verb("ai.freeplay.mode", "live");
    bool ok = Expect(!disabled.ok && disabled.text == "provider_disabled", ("disabled: " + disabled.text).c_str());
    ClearProviderEnvironment();
    SetEnvironment("XRAY_AGENT_PROVIDER", "anthropic");
    const VerbReply noKey = Verb("ai.freeplay.mode", "live");
    ok = Expect(!noKey.ok && noKey.text == "missing_api_key", ("no key: " + noKey.text).c_str()) && ok;
    SetEnvironment("XRAY_AGENT_API_KEY", "not-a-real-key");
    const VerbReply noModel = Verb("ai.freeplay.mode", "live");
    ok = Expect(!noModel.ok && noModel.text == "missing_model", ("no model: " + noModel.text).c_str()) && ok;
    ok = Expect(RequireVerb("ai.freeplay.mode") == "mode=off", "mode still off without a model") && ok;
    SetEnvironment("XRAY_AGENT_MODEL", "test-model");
    const VerbReply withModel = Verb("ai.freeplay.mode", "live");
    ok = Expect(withModel.ok ? withModel.text.rfind("mode=live ", 0) == 0 : withModel.text == "transport_unavailable",
        ("with a model only the transport can refuse: " + withModel.text).c_str()) && ok;
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    StartLive({});   // an installed test provider needs no environment
    return Expect(RequireVerb("ai.freeplay.mode").rfind("mode=live ", 0) == 0, "test provider satisfies live mode") && ok;
}

bool TestBlockedEventRewakesOwner()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    bool ok = Expect(Contains(OwnerLine(1), "state=idle "), "observed owner is idle");
    ok = Expect(Verb("ai.freeplay.event", "500 blocked esc_smart_c").text == "queued", "event queues a wake") && ok;
    ok = Expect(Contains(OwnerLine(1), "state=queued "), ("blocked owner queued: " + OwnerLine(1)).c_str()) && ok;
    const std::string log = RequireVerb("ai.freeplay.log");
    ok = Expect(Contains(log, "ownerId=1 epoch=1 membershipRevision=1 wakeReason=blocked source=event verdict=event "
                              "verb=- site=esc_smart_c"),
        ("event ledger line: " + log).c_str()) && ok;
    const size_t lines = std::count(log.begin(), log.end(), '\n');
    const VerbReply unknown = Verb("ai.freeplay.event", "999 blocked esc_smart_c");
    ok = Expect(!unknown.ok && unknown.text == "unknown_group", "event for unknown group refused") && ok;
    const std::string after = RequireVerb("ai.freeplay.log");
    return Expect(size_t(std::count(after.begin(), after.end(), '\n')) == lines, "refused event writes nothing") && ok;
}

// Owner 1 starts on its bootstrap (move_to esc_smart_b, rev 1) and takes a recorded move to esc_smart_c (rev 2).
void DecideMoveToC()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    RequireVerb("ai.freeplay.decide", "1 move_to esc_smart_c");
    RequireVerb("ai.freeplay.tick", "1");
}

// The last feasible plan: a blocked destination reverts to the objective in force before the decision.
bool TestBlockedObjectiveRevertsToPrevious()
{
    DecideMoveToC();
    bool ok = Expect(Verb("ai.freeplay.event", "500 blocked esc_smart_c").text == "queued", "blocked event queues");
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=hold rev=3 "),
        ("blocked objective reverts to the previous one: " + OwnerLine(1)).c_str()) && ok;
    const std::string tick = RequireVerb("ai.freeplay.tick", "2");
    ok = Expect(Contains(tick, "apply 1 500 3 move_to esc_smart_b hold\n"), ("reverted objective applied: " + tick).c_str()) &&
        ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.log"), "wakeReason=blocked source=event verdict=event verb=- "
                                                         "site=esc_smart_c"),
        "blocked event still logged") && ok;
    ok = Expect(Contains(OwnerLine(1), "state=queued "), "blocked owner still woken") && ok;
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_b");
    return Expect(Contains(OwnerLine(1), "objective=hold:- source=hold rev=4 "),
               ("a reverted objective that is blocked too falls back to hold: " + OwnerLine(1)).c_str()) && ok;
}

bool TestBlockedOtherSiteKeepsObjective()
{
    DecideMoveToC();
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_a");
    bool ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_c source=recorded rev=2 "),
        ("blocked other site changes nothing: " + OwnerLine(1)).c_str());
    ok = Expect(!Contains(RequireVerb("ai.freeplay.tick", "2"), "apply 1 "), "nothing to apply") && ok;
    RequireVerb("ai.freeplay.event", "500 arrived esc_smart_c");
    return Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_c source=recorded rev=2 "),
               "arrived changes no objective") && ok;
}

bool TestBlockedWithoutPreviousObjectiveHolds()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_b");
    bool ok = Expect(Contains(OwnerLine(1), "objective=hold:- source=hold rev=2 "),
        ("blocked bootstrap without an earlier objective holds: " + OwnerLine(1)).c_str());
    return Expect(Contains(RequireVerb("ai.freeplay.tick", "1"), "apply 1 500 2 hold - hold\n"), "hold applied") && ok;
}

// A sighting only requests a wake: it writes no ledger line of its own, and its reason shows on
// the owner's next decision line instead.
bool TestSightingEventWakesWithoutLedger()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const std::string before = RequireVerb("ai.freeplay.log");
    bool ok = Expect(Verb("ai.freeplay.event", "500 sighting bandit_squad_7").text == "queued",
        "sighting queues a wake");
    ok = Expect(Contains(OwnerLine(1), "state=queued "), ("sighting owner queued: " + OwnerLine(1)).c_str()) && ok;
    const std::string after = RequireVerb("ai.freeplay.log");
    ok = Expect(after == before, ("sighting writes no ledger line: " + after).c_str()) && ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.tick", "45000"), "observe 1 group 500 1"),
        "sighting wake handed out once the wake interval passed") && ok;
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    RequireVerb("ai.freeplay.decide", "1 stay");
    const std::string decided = RequireVerb("ai.freeplay.log");
    return Expect(Contains(decided, "wakeReason=sighting source=recorded verdict=accepted verb=stay"),
               ("decision line carries the sighting reason: " + decided).c_str()) && ok;
}

bool TestModeOffEmitsNothing()
{
    RequireVerb("ai.reset");
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", kTwoGroups);
    bool ok = Expect(RequireVerb("ai.freeplay.tick", "0").empty(), "mode off tick emits nothing");
    return Expect(SummaryLine(RequireVerb("ai.freeplay.census")) ==
                   "summary npcs=4 owners=3 transferable=2 script_owned=0 unbound=1",
               "census still works in mode off") && ok;
}

bool TestResetClearsFreeplay()
{
    StartFreeplay("recorded");
    RequireVerb("ai.reset");
    bool ok = Expect(SummaryLine(RequireVerb("ai.freeplay.census")) ==
                         "summary npcs=0 owners=0 transferable=0 script_owned=0 unbound=0",
        "reset drops every owner");
    return Expect(RequireVerb("ai.freeplay.mode") == "mode=off", "reset sets mode off") && ok;
}

// Recorded observations free their scheduler slot, so every owner is served without any decide.
bool TestRecordedObservationsFreeSlots()
{
    RequireVerb("ai.reset");
    RequireVerb("ai.freeplay.mode", "recorded");
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", FiveSquads());
    for (uint64_t now = 0; now < 5; ++now)
    {
        std::istringstream lines(RequireVerb("ai.freeplay.tick", std::to_string(now)));
        std::string kind;
        uint64_t ownerId = 0;
        std::string rest;
        while (lines >> kind >> ownerId && std::getline(lines, rest))
            if (kind == "observe")
                RequireVerb("ai.freeplay.observe", ObservationFor(ownerId, 1, 1, 0));
    }
    bool ok = true;
    for (uint64_t ownerId = 1; ownerId <= 5; ++ownerId)
        ok = Expect(Contains(OwnerLine(ownerId), " sites=esc_smart_0"),
            ("owner has a stored observation: " + OwnerLine(ownerId)).c_str()) && ok;
    RequireVerb("ai.freeplay.wake", "1 probe");
    const VerbReply notRequested = Verb("ai.freeplay.observe", ObservationFor(1, 1, 1, 0));
    return Expect(!notRequested.ok && notRequested.text == "not_requested", "unrequested observation refused") && ok;
}

bool TestRetireForgetsOwner()
{
    StartFreeplay("recorded");
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    bool ok = Expect(Verb("ai.freeplay.retire", "500").text == "retired", "retire known group");
    ok = Expect(OwnerLine(1).empty(), "retired owner listed no more") && ok;
    const VerbReply again = Verb("ai.freeplay.retire", "500");
    ok = Expect(!again.ok && again.text == "unknown_group", "retire twice refused") && ok;
    const VerbReply decide = Verb("ai.freeplay.decide", "1 stay");
    return Expect(!decide.ok && decide.text == "no_observation", "retire discards the stored observation") && ok;
}

// --- claude_cli provider ---

const char* kCliModel = "claude-sonnet-5-5";

// What a fake runner was asked to run.
struct FakeCliCalls
{
    std::vector<std::vector<std::string>> argv;
    std::vector<std::string> stdinText;
    std::vector<uint32_t> timeoutMs;
};

// Answers Run from a script in order (then: not started) and never starts a process.
class FakeCliRunner final : public xrSim::ICliRunner
{
public:
    FakeCliRunner(std::vector<xrSim::CliRunResult> results, std::shared_ptr<FakeCliCalls> calls)
        : m_results(std::move(results)), m_calls(std::move(calls))
    {
    }
    xrSim::CliRunResult Run(const std::vector<std::string>& argv, const std::string& stdinText, uint32_t timeoutMs) override
    {
        m_calls->argv.push_back(argv);
        m_calls->stdinText.push_back(stdinText);
        m_calls->timeoutMs.push_back(timeoutMs);
        if (m_cursor < m_results.size())
            return m_results[m_cursor++];
        xrSim::CliRunResult result;
        result.error = "fake_runner_exhausted";
        return result;
    }
    void Cancel() override {}

private:
    std::vector<xrSim::CliRunResult> m_results;
    size_t m_cursor = 0;
    std::shared_ptr<FakeCliCalls> m_calls;
};

xrSim::CliRunResult CliExit(int exitCode, const std::string& output, const std::string& error = std::string())
{
    xrSim::CliRunResult result;
    result.started = true;
    result.exitCode = exitCode;
    result.output = output;
    result.error = error;
    return result;
}

std::unique_ptr<xrSim::ClaudeCliActorIntentProvider> CliProvider(
    std::vector<xrSim::CliRunResult> results, std::shared_ptr<FakeCliCalls> calls)
{
    xrSim::ClaudeCliConfig config;
    config.model = kCliModel;
    return std::make_unique<xrSim::ClaudeCliActorIntentProvider>(
        config, std::make_unique<FakeCliRunner>(std::move(results), std::move(calls)));
}

xrSim::ActorWakeContext CliWake()
{
    xrSim::ActorWakeContext context;
    context.prompt = "xrsim_freeplay_wake_v1\nowner_id 1\nend\n";
    return context;
}

bool TestCliArgumentsAreExact()
{
    xrSim::ClaudeCliConfig config;
    config.model = kCliModel;
    const std::vector<std::string> expected{"claude", "-p", "--model", "claude-sonnet-5-5", "--tools", "",
        "--no-session-persistence", "--strict-mcp-config", "--setting-sources", "", "--system-prompt",
        "You answer with the requested schema lines only. No prose, no markdown, no code fences."};
    const std::vector<std::string> args = xrSim::BuildClaudeCliArguments(config);
    bool ok = Expect(args == expected, "claude_cli argument vector is exact");
    ok = Expect(std::find(args.begin(), args.end(), "--bare") == args.end(), "no --bare argument") && ok;
    config.executable = "/opt/homebrew/bin/claude";
    const std::vector<std::string> absolute = xrSim::BuildClaudeCliArguments(config);
    return Expect(!absolute.empty() && absolute[0] == "/opt/homebrew/bin/claude", "argv[0] is the executable") && ok;
}

bool TestCliExtractsIntentBlock()
{
    const std::string block = "xrsim_actor_intent_v1\naction move_to esc_smart_c\nend\n";
    const std::string wrapped = "Here is my answer:\n```\n" + block + "```\nHope that helps.\n";
    bool ok = Expect(xrSim::ExtractIntentBlock(wrapped) == block, "block extracted from prose and fence");
    ok = Expect(xrSim::ExtractIntentBlock("xrsim_actor_intent_v1\r\naction stay\r\nend\r\n") ==
            "xrsim_actor_intent_v1\naction stay\nend\n",
        "CRLF lines are accepted") && ok;
    ok = Expect(xrSim::ExtractIntentBlock(block + "xrsim_actor_intent_v1\naction stay\nend\n") == block,
        "only the first block") && ok;
    ok = Expect(xrSim::ExtractIntentBlock("action move_to esc_smart_c\nend\n").empty(), "no header gives nothing") && ok;
    return Expect(xrSim::ExtractIntentBlock("xrsim_actor_intent_v1\naction stay\n").empty(), "no end gives nothing") &&
        ok;
}

bool TestCliProviderAcceptsReply()
{
    auto calls = std::make_shared<FakeCliCalls>();
    auto provider = CliProvider({CliExit(0, "xrsim_actor_intent_v1\naction move_to esc_smart_c\nend\n")}, calls);
    const xrSim::ActorWakeContext context = CliWake();
    const xrSim::ActorProviderResult result = provider->Wake(context);
    bool ok = Expect(result.ok && !result.coast && result.coastReason.empty(), ("reply accepted: " + result.error).c_str());
    ok = Expect(result.plan.actions.size() == 1 && result.plan.actions[0].verb == "move_to" &&
            result.plan.actions[0].target == "esc_smart_c",
        "plan has move_to esc_smart_c") && ok;
    ok = Expect(result.provider == "claude_cli" && result.model == kCliModel, "provider and model named") && ok;
    ok = Expect(calls->stdinText.size() == 1 && calls->stdinText[0] == context.prompt, "the prompt goes to stdin") && ok;
    ok = Expect(calls->argv.size() == 1 && calls->argv[0] == xrSim::BuildClaudeCliArguments(xrSim::ClaudeCliConfig{
                                                                   "claude", kCliModel, 60000}),
        "the runner gets the exact argument vector") && ok;
    return Expect(calls->timeoutMs.size() == 1 && calls->timeoutMs[0] == 60000, "the configured timeout") && ok;
}

bool TestCliProviderMapsFailures()
{
    xrSim::CliRunResult notStarted;
    notStarted.error = "not_found";
    xrSim::CliRunResult timedOut = CliExit(-1, "partial");
    timedOut.timedOut = true;
    auto calls = std::make_shared<FakeCliCalls>();
    auto provider = CliProvider({notStarted, timedOut, CliExit(2, "", "Error: not logged in"), CliExit(0, "hello\n"),
                                    CliExit(0, "xrsim_actor_intent_v1\nteleport now\nend\n")},
        calls);
    const xrSim::ActorProviderResult unavailable = provider->Wake(CliWake());
    bool ok = Expect(unavailable.coast && unavailable.coastReason == "cli_unavailable", "not started -> cli_unavailable");
    const xrSim::ActorProviderResult timeout = provider->Wake(CliWake());
    ok = Expect(timeout.coast && timeout.coastReason == "cli_timeout", "timed out -> cli_timeout") && ok;
    const xrSim::ActorProviderResult exited = provider->Wake(CliWake());
    ok = Expect(exited.coast && exited.coastReason == "cli_exit" && Contains(exited.error, "2") &&
            Contains(exited.error, "not logged in"),
        ("non-zero exit -> cli_exit with code and stderr line: " + exited.error).c_str()) && ok;
    const xrSim::ActorProviderResult prose = provider->Wake(CliWake());
    ok = Expect(!prose.ok && prose.coastReason.empty() && !prose.error.empty(), "exit 0 with prose -> not ok") && ok;
    const xrSim::ActorProviderResult bad = provider->Wake(CliWake());
    ok = Expect(!bad.ok && bad.coastReason.empty() && !bad.error.empty(), "unparsable block -> not ok") && ok;
    for (const xrSim::ActorProviderResult* result : {&unavailable, &timeout, &exited, &prose, &bad})
        ok = Expect(result->provider == "claude_cli" && result->model == kCliModel, "every result names provider and model") &&
            ok;
    return ok;
}

bool TestCliLiveDecisionFlow()
{
    ClearProviderEnvironment();
    auto calls = std::make_shared<FakeCliCalls>();
    StartLiveWith(CliProvider({CliExit(0, std::string("Sure.\n") + kLiveMoveReply)}, calls), kTwoGroups);
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const std::string ticks = TickUntilLogged("verdict=accepted", 1);
    bool ok = Expect(Contains(ticks, "apply 1 500 2 move_to esc_smart_c live\n"), ("live apply line: " + ticks).c_str());
    const std::string log = RequireVerb("ai.freeplay.log");
    ok = Expect(Contains(log, "source=live verdict=accepted verb=move_to site=esc_smart_c provider=claude_cli "
                              "model=claude-sonnet-5-5 "),
        ("claude_cli ledger line: " + log).c_str()) && ok;
    return Expect(calls->stdinText.size() == 1 && Contains(calls->stdinText[0], "xrsim_freeplay_wake_v1\n"),
               "the wake prompt went to the runner's stdin") && ok;
}

bool TestCliLiveModeRefusals()
{
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    SetEnvironment("XRAY_AGENT_PROVIDER", "claude_cli");
    const VerbReply noModel = Verb("ai.freeplay.mode", "live");
    bool ok = Expect(!noModel.ok && noModel.text == "missing_model", ("no model: " + noModel.text).c_str());
    SetEnvironment("XRAY_AGENT_MODEL", "a b");
    SetEnvironment("XRAY_AGENT_CLI", "/nonexistent/claude");
    const VerbReply badModel = Verb("ai.freeplay.mode", "live");
    ok = Expect(!badModel.ok && badModel.text == "invalid_model", ("model with a space: " + badModel.text).c_str()) && ok;
    SetEnvironment("XRAY_AGENT_MODEL", kCliModel);
    const VerbReply missing = Verb("ai.freeplay.mode", "live");
    ok = Expect(!missing.ok && missing.text == "cli_unavailable", ("missing executable: " + missing.text).c_str()) && ok;
    SetEnvironment("XRAY_AGENT_CLI", "claude;rm");
    const VerbReply badCli = Verb("ai.freeplay.mode", "live");
    ok = Expect(!badCli.ok && badCli.text == "invalid_cli", ("executable with a ';': " + badCli.text).c_str()) && ok;
    SetEnvironment("XRAY_AGENT_CLI", "xrsim_no_such_cli_on_path");
    const VerbReply notOnPath = Verb("ai.freeplay.mode", "live");
    ok = Expect(!notOnPath.ok && notOnPath.text == "cli_unavailable", ("bare name not on PATH: " + notOnPath.text).c_str()) &&
        ok;
    ok = Expect(RequireVerb("ai.freeplay.mode") == "mode=off", "mode still off") && ok;
    SetEnvironment("XRAY_AGENT_PROVIDER", "bogus");
    const VerbReply bogus = Verb("ai.freeplay.mode", "live");
    ok = Expect(!bogus.ok && bogus.text == "provider_disabled", ("other provider: " + bogus.text).c_str()) && ok;
#if !defined(_WIN32)
    // An existing executable and a model are all claude_cli needs: no API key. Nothing observes, so nothing runs.
    SetEnvironment("XRAY_AGENT_PROVIDER", "claude_cli");
    SetEnvironment("XRAY_AGENT_CLI", "/bin/cat");
    const VerbReply live = Verb("ai.freeplay.mode", "live");
    ok = Expect(live.ok && live.text.rfind("mode=live", 0) == 0, ("claude_cli without a key: " + live.text).c_str()) && ok;
#endif
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    return ok;
}

// --- live settings and the call budget ---

VerbReply LiveWithTestProvider(std::unique_ptr<xrSim::IActorIntentProvider> provider)
{
    RequireVerb("ai.reset");
    xrSim::SetFreeplayProviderForTests(std::move(provider));
    return Verb("ai.freeplay.mode", "live");
}

std::unique_ptr<xrSim::IActorIntentProvider> NoReplies()
{
    return std::make_unique<xrSim::RecordedActorIntentProvider>(std::vector<std::string>{});
}

std::string ObserveLines(const std::string& tick)
{
    std::istringstream lines(tick);
    std::string line, out;
    while (std::getline(lines, line))
        if (line.rfind("observe ", 0) == 0)
            out += line + "\n";
    return out;
}

bool TestLiveSettingsFromEnvironment()
{
    ClearProviderEnvironment();
    SetEnvironment("XRAY_FREEPLAY_MAX_IN_FLIGHT", "3");
    SetEnvironment("XRAY_FREEPLAY_CALLS_PER_MINUTE", "10");
    SetEnvironment("XRAY_FREEPLAY_RECONSIDER_MS", "120000");
    SetEnvironment("XRAY_FREEPLAY_MIN_WAKE_MS", "5000");
    const VerbReply live = LiveWithTestProvider(NoReplies());
    const std::string expected = "mode=live provider=anthropic model=- max_in_flight=3 calls_per_minute=10 "
                                 "reconsider_ms=120000 min_wake_ms=5000";
    bool ok = Expect(live.ok && live.text == expected, ("settings echoed: " + live.text).c_str());
    ok = Expect(RequireVerb("ai.freeplay.mode") == expected, "a mode query echoes the settings too") && ok;
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", FiveSquads());
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "0")) ==
            "observe 1 group 500 1\nobserve 2 group 501 1\nobserve 3 group 502 1\n",
        "max_in_flight 3 hands out three owners") && ok;

    const std::vector<std::pair<const char*, const char*>> bad{{"XRAY_FREEPLAY_MAX_IN_FLIGHT", "9"},
        {"XRAY_FREEPLAY_MAX_IN_FLIGHT", "0"}, {"XRAY_FREEPLAY_MAX_IN_FLIGHT", "two"},
        {"XRAY_FREEPLAY_CALLS_PER_MINUTE", "-1"}, {"XRAY_FREEPLAY_RECONSIDER_MS", "59999"},
        {"XRAY_FREEPLAY_RECONSIDER_MS", "86400001"}, {"XRAY_FREEPLAY_MIN_WAKE_MS", "4999"},
        {"XRAY_FREEPLAY_MIN_WAKE_MS", "3600001"}};
    for (const auto& item : bad)
    {
        ClearProviderEnvironment();
        SetEnvironment(item.first, item.second);
        const VerbReply refused = LiveWithTestProvider(NoReplies());
        ok = Expect(!refused.ok && refused.text == std::string("invalid_setting:") + item.first,
            (std::string("bad ") + item.first + "=" + item.second + ": " + refused.text).c_str()) && ok;
        ok = Expect(RequireVerb("ai.freeplay.mode") == "mode=off", "a refused setting leaves the mode off") && ok;
    }

    ClearProviderEnvironment();
    SetEnvironment("XRAY_AGENT_PROVIDER", "claude_cli");
    SetEnvironment("XRAY_AGENT_MODEL", kCliModel);
    const VerbReply cli = LiveWithTestProvider(NoReplies());
    ok = Expect(cli.ok && cli.text == "mode=live provider=claude_cli model=claude-sonnet-5-5 max_in_flight=1 "
                                      "calls_per_minute=6 reconsider_ms=600000 min_wake_ms=45000",
        ("claude_cli defaults: " + cli.text).c_str()) && ok;
    ClearProviderEnvironment();
    const VerbReply anthropic = LiveWithTestProvider(NoReplies());
    ok = Expect(anthropic.ok && anthropic.text == "mode=live provider=anthropic model=- max_in_flight=2 "
                                                  "calls_per_minute=0 reconsider_ms=600000 min_wake_ms=45000",
        ("anthropic defaults: " + anthropic.text).c_str()) && ok;
    ok = Expect(RequireVerb("ai.freeplay.mode", "off") == "mode=off", "mode off has no settings tail") && ok;
    RequireVerb("ai.reset");
    return ok;
}

bool TestCallsPerMinuteCap()
{
    ClearProviderEnvironment();
    SetEnvironment("XRAY_FREEPLAY_CALLS_PER_MINUTE", "2");
    SetEnvironment("XRAY_FREEPLAY_MAX_IN_FLIGHT", "8");
    const VerbReply live = LiveWithTestProvider(std::make_unique<CoastProvider>(""));
    bool ok = Expect(live.ok, ("live with a cap: " + live.text).c_str());
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", FiveSquads());
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "0")) == "observe 1 group 500 1\nobserve 2 group 501 1\n",
        "cap 2: two owners handed out") && ok;
    RequireVerb("ai.freeplay.observe", ObservationFor(1, 1, 1, 0));   // submitted at 0
    RequireVerb("ai.freeplay.tick", "1000");
    RequireVerb("ai.freeplay.observe", ObservationFor(2, 1, 1, 0));   // submitted at 1000
    for (int i = 0; i < 200 && !Contains(OwnerLine(2), "state=idle"); ++i)
    {
        ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "1000")).empty(), "nothing more while settling") && ok;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ok = Expect(Contains(OwnerLine(1), "state=idle") && Contains(OwnerLine(2), "state=idle"),
        "both replies settled and freed their slots") && ok;
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "30000")).empty(), "free slots but no budget at 30000") && ok;
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "59999")).empty(), "no budget 1 ms before the window") && ok;
    ok = Expect(Contains(OwnerLine(3), "state=queued"), "the third owner keeps its place in the queue") && ok;
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "60000")) == "observe 3 group 502 1\n",
        "the first submission leaves the window at 60000: one owner") && ok;
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "60500")).empty(),
        "a hand-out awaiting its observation counts against the cap") && ok;
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "61000")) == "observe 4 group 503 1\n",
        "the second submission leaves the window at 61000") && ok;

    // Recorded mode ignores the cap.
    RequireVerb("ai.reset");
    SetEnvironment("XRAY_FREEPLAY_CALLS_PER_MINUTE", "1");
    RequireVerb("ai.freeplay.mode", "recorded");
    RequireVerb("ai.freeplay.begin");
    RequireVerb("ai.freeplay.sync", FiveSquads());
    ok = Expect(ObserveLines(RequireVerb("ai.freeplay.tick", "0")) == "observe 1 group 500 1\nobserve 2 group 501 1\n",
        "recorded mode ignores the cap") && ok;
    ClearProviderEnvironment();
    RequireVerb("ai.reset");
    return ok;
}

// --- blocked-site memory ---

size_t CountOf(const std::string& text, const std::string& part)
{
    size_t count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        ++count;
    return count;
}

bool TestRecentlyBlockedSiteIsRejected()
{
    DecideMoveToC();
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_c");
    bool ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=hold rev=3 "),
        ("blocked objective reverted: " + OwnerLine(1)).c_str());
    const VerbReply again = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    ok = Expect(!again.ok && again.text == "recently_blocked", ("blocked site rejected: " + again.text).c_str()) && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=hold rev=3 "),
        ("rejection keeps objective and revision: " + OwnerLine(1)).c_str()) && ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.log"), "source=recorded verdict=recently_blocked verb=move_to "
                                                         "site=esc_smart_c "),
        "rejection is logged") && ok;
    ok = Expect(!Contains(RequireVerb("ai.freeplay.tick", "2"), "apply 1 500 4"), "rejection applies nothing") && ok;
    const VerbReply retreat = Verb("ai.freeplay.decide", "1 retreat esc_smart_c");
    ok = Expect(!retreat.ok && retreat.text == "recently_blocked", "retreat to the blocked site rejected too") && ok;
    const VerbReply other = Verb("ai.freeplay.decide", "1 move_to esc_smart_a");
    ok = Expect(other.ok && other.text == "accepted move_to esc_smart_a", ("other site accepted: " + other.text).c_str()) &&
        ok;
    return Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_a source=recorded rev=4 "),
               ("other site raises the revision: " + OwnerLine(1)).c_str()) && ok;
}

bool TestRecentlyBlockedExpiresAndClears()
{
    DecideMoveToC();   // ticks 0 and 1
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_c");   // remembered at 1
    RequireVerb("ai.freeplay.tick", "600000");
    const VerbReply held = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    bool ok = Expect(!held.ok && held.text == "recently_blocked", ("still blocked 1 ms before expiry: " + held.text).c_str());
    RequireVerb("ai.freeplay.tick", "600001");
    const VerbReply expired = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    ok = Expect(expired.ok && expired.text == "accepted move_to esc_smart_c",
        ("accepted after 600000 ms: " + expired.text).c_str()) && ok;

    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_c");
    ok = Expect(Verb("ai.freeplay.decide", "1 move_to esc_smart_c").text == "recently_blocked", "blocked again") && ok;
    RequireVerb("ai.freeplay.event", "500 arrived esc_smart_b");
    const VerbReply cleared = Verb("ai.freeplay.decide", "1 move_to esc_smart_c");
    ok = Expect(cleared.ok && cleared.text == "accepted move_to esc_smart_c",
        ("an arrival clears the memory: " + cleared.text).c_str()) && ok;
    return ok;
}

bool TestBlockedStayLoopIsBroken()
{
    const char* stay = "xrsim_actor_intent_v1\naction stay\nend\n";
    auto prompts = std::make_shared<std::vector<std::string>>();
    StartLiveWith(std::make_unique<CapturingProvider>(std::vector<std::string>{stay, stay, stay}, prompts), kTwoGroups);
    RequireVerb("ai.freeplay.tick", "0");
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    bool ok = Expect(Contains(TickUntilLogged("verdict=accepted", 1), "apply 1 500 2 stay esc_smart_a live\n"),
        "first stay accepted at rev 2");

    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_a");   // stock refuses the squad's own smart
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=hold rev=3 ") &&
            Contains(OwnerLine(1), "state=queued "),
        ("blocked stay reverts and wakes: " + OwnerLine(1)).c_str()) && ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.tick", "46000"), "observe 1 group 500 1\n"), "owner 1 woken") && ok;
    RequireVerb("ai.freeplay.observe", kSquadObservation);
    const std::string ticks = TickUntilLogged("verdict=recently_blocked", 46000);
    ok = Expect(!Contains(ticks, "apply 1 "), ("second stay applies nothing: " + ticks).c_str()) && ok;
    ok = Expect(Contains(RequireVerb("ai.freeplay.log"), "source=live verdict=recently_blocked verb=stay site=esc_smart_a "),
        "second stay is recently_blocked") && ok;
    ok = Expect(Contains(OwnerLine(1), "objective=move_to:esc_smart_b source=hold rev=3 ") &&
            Contains(OwnerLine(1), "state=idle "),
        ("no new revision, owner idle: " + OwnerLine(1)).c_str()) && ok;
    ok = Expect(prompts->size() == 2 && !Contains((*prompts)[0], "recently_blocked_site") &&
            Contains((*prompts)[1], "recently_blocked_site esc_smart_a\n"),
        "the second prompt names the blocked site") && ok;

    // The adapter reports the same blocked site again: logged, but no wake, so the loop cannot restart.
    const size_t events = CountOf(RequireVerb("ai.freeplay.log", "100"), "verdict=event");
    RequireVerb("ai.freeplay.event", "500 blocked esc_smart_a");
    ok = Expect(CountOf(RequireVerb("ai.freeplay.log", "100"), "verdict=event") == events + 1,
        "repeated blocked event still logged") && ok;
    ok = Expect(Contains(OwnerLine(1), "state=idle "), ("repeated blocked event wakes nobody: " + OwnerLine(1)).c_str()) &&
        ok;
    for (const char* now : {"100000", "400000", "645999"})
        ok = Expect(!Contains(RequireVerb("ai.freeplay.tick", now), "observe 1 "),
            (std::string("no wake before the reconsider interval at ") + now).c_str()) && ok;
    ok = Expect(prompts->size() == 2, "no further provider call") && ok;
    return Expect(Contains(RequireVerb("ai.freeplay.tick", "646000"), "observe 1 group 500 1\n"),
               "woken again by the reconsider interval") && ok;
}

bool TestWakePromptNamesBlockedSite()
{
    xrSim::FreeplayPopulation population;
    RequireSetup(population.Sync(kTwoGroups));
    const xrSim::FreeplayObservation observation = RequireObservation(population, kSquadObservation);
    const xrSim::FreeplayOwner& squad = RequireGroup(population, 500);
    const std::string plain = xrSim::BuildFreeplayWake(squad, observation, "blocked").prompt;
    const std::string named = xrSim::BuildFreeplayWake(squad, observation, "blocked", "esc_smart_c").prompt;
    bool ok = Expect(!Contains(plain, "recently_blocked_site") && !Contains(plain, "do_not_choose_recently_blocked_site"),
        "no blocked lines without a memory");
    ok = Expect(Contains(named, "\nrecently_blocked_site esc_smart_c\n"), "blocked site line") && ok;
    const size_t rules = named.find("\nrules\n");
    const size_t rule = named.find("\ndo_not_choose_recently_blocked_site\n");
    ok = Expect(rule != std::string::npos && rules != std::string::npos && rule > rules, "rule line among the rules") && ok;
    std::string stripped = named;
    for (const char* line : {"recently_blocked_site esc_smart_c\n", "do_not_choose_recently_blocked_site\n"})
    {
        const size_t at = stripped.find(line);
        if (at != std::string::npos)
            stripped.erase(at, std::string(line).size());
    }
    return Expect(stripped == plain, "otherwise the same prompt") && ok;
}

#if !defined(_WIN32)
// True when this process has no child left, reaped or not.
bool NoChildLeft()
{
    int status = 0;
    return waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD;
}

size_t OpenDescriptorCount()
{
    size_t count = 0;
    for (int fd = 0; fd < 1024; ++fd)
        if (fcntl(fd, F_GETFD) != -1)
            ++count;
    return count;
}

long ElapsedMs(std::chrono::steady_clock::time_point since)
{
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count());
}

bool TestProcessRunnerEchoAndTimeout()
{
    const size_t descriptors = OpenDescriptorCount();
    auto runner = xrSim::CreateProcessCliRunner();
    const xrSim::CliRunResult echo = runner->Run({"/bin/cat"}, "hello\nworld\n", 5000);
    bool ok = Expect(echo.started && !echo.timedOut && echo.exitCode == 0 && echo.output == "hello\nworld\n",
        ("/bin/cat echoes stdin: " + echo.error).c_str());

    std::string big;
    for (int i = 0; big.size() < 300000; ++i)
        big += "line " + std::to_string(i) + " of a reply larger than a pipe buffer\n";
    const xrSim::CliRunResult large = runner->Run({"/bin/cat"}, big, 10000);
    ok = Expect(large.exitCode == 0 && large.output == big, "output larger than a pipe buffer arrives whole") && ok;

    const xrSim::CliRunResult failed = runner->Run({"/bin/sh", "-c", "echo first line >&2; echo second >&2; exit 3"}, "", 5000);
    ok = Expect(failed.started && failed.exitCode == 3 && failed.error == "first line",
        ("exit code and first stderr line: " + failed.error).c_str()) && ok;

    char tempPath[] = "/tmp/xrsim_cli_fd_XXXXXX";
    const int leak = mkstemp(tempPath);   // no close-on-exec
    if (leak >= 0)
    {
        const std::string probe = "if true >&" + std::to_string(leak) + " 2>/dev/null; then echo open; else echo closed; fi";
        const xrSim::CliRunResult inherited = runner->Run({"/bin/sh", "-c", probe}, "", 5000);
        ok = Expect(inherited.output == "closed\n", ("child inherits no other descriptor: " + inherited.output).c_str()) && ok;
        close(leak);
        unlink(tempPath);
    }

    const xrSim::CliRunResult where = runner->Run({"/bin/pwd"}, "", 5000);
    char cwd[PATH_MAX] = {};
    ok = Expect(where.exitCode == 0 && getcwd(cwd, sizeof(cwd)) != nullptr && where.output != std::string(cwd) + "\n",
        ("child runs outside the game's directory: " + where.output).c_str()) && ok;

    auto started = std::chrono::steady_clock::now();
    const xrSim::CliRunResult slow = runner->Run({"/bin/sleep", "5"}, "", 200);
    ok = Expect(slow.started && slow.timedOut && ElapsedMs(started) < 2000, "sleep 5 times out within 2 s") && ok;
    ok = Expect(NoChildLeft(), "timed out child reaped") && ok;

    started = std::chrono::steady_clock::now();
    const xrSim::CliRunResult stubborn = runner->Run({"/bin/sh", "-c", "trap '' TERM; sleep 5"}, "", 200);
    ok = Expect(stubborn.timedOut && ElapsedMs(started) < 2000, "a child that ignores SIGTERM is killed within 2 s") && ok;
    ok = Expect(NoChildLeft(), "stubborn child reaped") && ok;

    const xrSim::CliRunResult missing = runner->Run({"/nonexistent/xrsim_cli"}, "x", 1000);
    ok = Expect(!missing.started && !missing.error.empty(), "missing executable does not start") && ok;
    const xrSim::CliRunResult notOnPath = runner->Run({"xrsim_no_such_cli_on_path"}, "x", 1000);
    ok = Expect(!notOnPath.started, "bare name not on PATH does not start") && ok;

    // Cancel from another thread while Run waits on a child.
    xrSim::CliRunResult cancelled;
    started = std::chrono::steady_clock::now();
    std::thread worker([&] { cancelled = runner->Run({"/bin/sleep", "5"}, "", 10000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runner->Cancel();
    worker.join();
    ok = Expect(cancelled.started && !cancelled.timedOut && cancelled.exitCode != 0 && ElapsedMs(started) < 2000,
        "Cancel ends a running child promptly") && ok;
    ok = Expect(NoChildLeft(), "cancelled child reaped") && ok;
    started = std::chrono::steady_clock::now();
    const xrSim::CliRunResult afterCancel = runner->Run({"/bin/sleep", "5"}, "", 10000);
    ok = Expect(!afterCancel.started && ElapsedMs(started) < 500, "a cancelled runner starts nothing") && ok;

    // Cancel before Run: the race where shutdown cancels just before the worker starts its child.
    auto early = xrSim::CreateProcessCliRunner();
    early->Cancel();
    ok = Expect(!early->Run({"/bin/sleep", "5"}, "", 10000).started, "Cancel before Run starts nothing") && ok;

    runner.reset();
    early.reset();
    ok = Expect(xrSim::IsCliExecutableAvailable("/bin/cat") && xrSim::IsCliExecutableAvailable("sh") &&
            !xrSim::IsCliExecutableAvailable("/nonexistent/claude") &&
            !xrSim::IsCliExecutableAvailable("xrsim_no_such_cli_on_path") && !xrSim::IsCliExecutableAvailable("/tmp") &&
            !xrSim::IsCliExecutableAvailable("bin/cat") && !xrSim::IsCliExecutableAvailable(""),
        "executable lookup") && ok;
    return Expect(OpenDescriptorCount() == descriptors, "no descriptor leaked") && ok;
}
#else
bool TestProcessRunnerEchoAndTimeout()
{
    const xrSim::CliRunResult result = xrSim::CreateProcessCliRunner()->Run({"cmd"}, "", 1000);
    return Expect(!result.started && result.error == "unsupported_platform", "windows runner is a stub");
}
#endif
} // namespace

int main()
{
    ClearProviderEnvironment();   // no test depends on the developer's environment
    bool ok = true;
    ok = TestSyncEnrollsEveryNpc() && ok;
    ok = TestSyncIsAtomicOnBadRecord() && ok;
    ok = TestLeaderLossKeepsOwnerAndObjective() && ok;
    ok = TestReusedGroupIdGetsNewOwner() && ok;
    ok = TestReusedGroupIdWithoutRetireGetsNewOwner() && ok;
    ok = TestReusedNpcIdWithNewSectionGetsNewOwner() && ok;
    ok = TestAbsentGroupIsRetired() && ok;
    ok = TestSplitInheritsObjective() && ok;
    ok = TestJoinAdoptsGroupObjective() && ok;
    ok = TestBeginWorldDropsOwnersAndAdvancesEpoch() && ok;
    ok = TestChangedOwnersReportedOnce() && ok;
    ok = TestCensusLevelFilter() && ok;
    ok = TestObjectiveAndMemoryValidation() && ok;
    ok = TestObservationParseAndLimits() && ok;
    ok = TestWakePromptListsSitesAndRules() && ok;
    ok = TestDecisionAcceptsListedSite() && ok;
    ok = TestDecisionRejections() && ok;
    ok = TestConstrainedOwnerOnlyStays() && ok;
    ok = TestCoastKeepsObjective() && ok;
    ok = TestStaleAfterSplitRejected() && ok;
    ok = TestLedgerKeepsNewest512() && ok;
    ok = TestDuplicateWakesCoalesce() && ok;
    ok = TestConcurrencyBoundAndFairness() && ok;
    ok = TestMinWakeInterval() && ok;
    ok = TestPeriodicReconsider() && ok;
    ok = TestConstrainedOwnersAreNotWoken() && ok;
    ok = TestProviderFailureBacksOff() && ok;
    ok = TestObservationTimeoutFreesSlot() && ok;
    ok = TestForgetRemovesOwner() && ok;
    ok = TestRecordedDecisionFlow() && ok;
    ok = TestDecideRejectsUnknownSite() && ok;
    ok = TestLiveDecisionFlow() && ok;
    ok = TestReplyAfterBeginWorldIsStale() && ok;
    ok = TestStaleReplyAfterSplit() && ok;
    ok = TestFailedProviderResultIsMalformed() && ok;
    ok = TestMalformedReplyDoesNotBackOff() && ok;
    ok = TestFailedReplyAfterBeginIsStale() && ok;
    ok = TestIdenticalRecordedDecisionIsKept() && ok;
    ok = TestIdenticalLiveDecisionIsKeptWithMemories() && ok;
    ok = TestProviderCoastHoldsAndBacksOff() && ok;
    ok = TestModelCoastIsKept() && ok;
    ok = TestLiveModeRefusedWithoutProvider() && ok;
    ok = TestLiveModeRefusalOrder() && ok;
    ok = TestBlockedEventRewakesOwner() && ok;
    ok = TestBlockedObjectiveRevertsToPrevious() && ok;
    ok = TestBlockedOtherSiteKeepsObjective() && ok;
    ok = TestBlockedWithoutPreviousObjectiveHolds() && ok;
    ok = TestSightingEventWakesWithoutLedger() && ok;
    ok = TestModeOffEmitsNothing() && ok;
    ok = TestResetClearsFreeplay() && ok;
    ok = TestRecordedObservationsFreeSlots() && ok;
    ok = TestRetireForgetsOwner() && ok;
    ok = TestCliArgumentsAreExact() && ok;
    ok = TestCliExtractsIntentBlock() && ok;
    ok = TestCliProviderAcceptsReply() && ok;
    ok = TestCliProviderMapsFailures() && ok;
    ok = TestCliLiveDecisionFlow() && ok;
    ok = TestCliLiveModeRefusals() && ok;
    ok = TestProcessRunnerEchoAndTimeout() && ok;
    ok = TestLiveSettingsFromEnvironment() && ok;
    ok = TestCallsPerMinuteCap() && ok;
    ok = TestRecentlyBlockedSiteIsRejected() && ok;
    ok = TestRecentlyBlockedExpiresAndClears() && ok;
    ok = TestBlockedStayLoopIsBroken() && ok;
    ok = TestWakePromptNamesBlockedSite() && ok;
    RequireVerb("ai.reset");
    return ok ? 0 : 1;
}
