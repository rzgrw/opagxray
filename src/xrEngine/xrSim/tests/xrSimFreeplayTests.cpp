#include "xrSim/xrSimFreeplay.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
} // namespace

int main()
{
    bool ok = true;
    ok = TestSyncEnrollsEveryNpc() && ok;
    ok = TestSyncIsAtomicOnBadRecord() && ok;
    ok = TestLeaderLossKeepsOwnerAndObjective() && ok;
    ok = TestReusedGroupIdGetsNewOwner() && ok;
    ok = TestAbsentGroupIsRetired() && ok;
    ok = TestSplitInheritsObjective() && ok;
    ok = TestJoinAdoptsGroupObjective() && ok;
    ok = TestBeginWorldDropsOwnersAndAdvancesEpoch() && ok;
    ok = TestChangedOwnersReportedOnce() && ok;
    ok = TestCensusLevelFilter() && ok;
    ok = TestObjectiveAndMemoryValidation() && ok;
    return ok ? 0 : 1;
}
