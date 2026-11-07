#include "xrSim/xrSimZoneManifest.h"
#include "xrSim/xrSimZoneState.h"

#include <iostream>
#include <functional>
#include <cstdlib>
#include <limits>
#include <locale>
#include <set>

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

void RequireSetup(const xrSim::ZoneResult& result)
{
    if (!Expect(result.ok, ("test setup: " + result.reason).c_str())) std::exit(EXIT_FAILURE);
}

xrSim::ZoneManifest FixtureManifest()
{
    const auto parsed = xrSim::ParseZoneManifest(kManifest);
    if (!Expect(parsed.ok, "fixture manifest parses")) std::exit(EXIT_FAILURE);
    return parsed.manifest;
}

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
std::string Replace(std::string text, const std::string& before, const std::string& after)
{
    const size_t at = text.find(before);
    if (at == std::string::npos)
    {
        std::cerr << "FAIL: test replacement not found: " << before << '\n';
        std::abort();
    }
    text.replace(at, before.size(), after);
    return text;
}

bool TestStrictManifestValidation()
{
    bool ok = true;
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"seed=14072026", "seed=-1"}, {"seed=14072026", "seed=+1"},
        {"seed=14072026", "seed= 1"}, {"seed=14072026", "seed=4294967296"},
        {"seed=14072026", "seed=99999999999999999999999999999999999"},
        {"3500,0,0", "2147483648,0,0"}, {"3500,0,0", "-2147483649,0,0"},
        {"3500,0,0", "1x,0,0"}, {"3500,0,0", "+1,0,0"},
        {"3500,0,0", "1,0"}, {"3500,0,0", "1,0,0,0"},
        {"origin=player", "origin=arbitrary"},
        {"scenario=garbage_gate1", "scenario="},
        {"scenario=garbage_gate1", "scenario=" + std::string(97, 'x')},
        {"sim_default_stalker_0", std::string(97, 'x')},
        {"scenario=garbage_gate1", "scenario=garbage\tgate1"},
        {"scenario=garbage_gate1", "scenario=garbage\rgate1"},
        {"scenario=garbage_gate1", "scenario=\xc0\xaf"},
        {"scenario=garbage_gate1", "scenario=\xed\xa0\x80"},
        {"scenario=garbage_gate1", "scenario=\xf4\x90\x80\x80"},
        {"scenario=garbage_gate1", "scenario=\x80"},
        {"scenario=garbage_gate1", "scenario=\xe2\x82"},
        {"scenario=garbage_gate1", std::string("scenario=a\0b", 12)},
        {"stalker_camp,3500", "bandit_camp,3500"},
        {"stalker,101", "bandit,101"},
        {"group=stalker_squad", "group=bandit_squad"},
        {"squad,stalker,", "squad,missing,"},
        {"mutant_pack,ecology,dog", "mutant_pack,stalker,dog"},
        {"sim_default_stalker_0,3,", "sim_default_stalker_0,0,"},
        {"sim_default_stalker_0,3,", "sim_default_stalker_0,9,"},
        {"3,stalker_camp,201", "3,missing,201"},
        {"3,stalker_camp,201", "3,stalker_camp,999"},
        {"stalker,101", "stalker,102"},
        {"scrap,scrapyard_center,40,5", "scrap,missing,40,5"},
        {"scrap,scrapyard_center,40,5", "scrap,scrapyard_center,-1,0"},
        {"scrap,scrapyard_center,40,5", "scrap,scrapyard_center,40,-1"},
        {"scrap,scrapyard_center,40,5", "scrap,scrapyard_center,40,41"},
        {"scrap,scrapyard_center,40,5", "scrap,scrapyard_center,1001,0"},
        {"scrap,scrapyard_center,40,5", ",scrapyard_center,40,5"},
        {"route=stalker_to_scrap,stalker_camp", "route=stalker_to_scrap,missing"},
        {"route=stalker_to_scrap,stalker_camp,scrapyard_center", "route=x,stalker_camp,missing"},
        {"agent=100,zone,1", "agent=0,zone,1"},
        {"agent=100,zone,1", "agent=101,zone,1"},
        {"agent=100,zone,1", "agent=100,zone,0"},
        {"agent=100,zone,1", "agent=100,zone,2"},
        {"agent=100,zone,1", "agent=100,unknown,1"},
        {"agent=201,squad,1", "agent=201,squad,2"},
        {"agent=203,mutant_pack,3", "agent=203,mutant_pack,1"},
        {"agent=101,faction,1", "agent=101,faction,3"},
        {"zone_director,zone_director_v1", ",zone_director_v1"},
        {"zone_director,zone_director_v1", std::string(97, 'x') + ",zone_director_v1"},
        {"zone_director,zone_director_v1", "stalker_faction,zone_director_v1"},
        {"zone_director_v1", "unknown_v1"},
        {"zone_director_v1", "pack_v1"},
        {"scenario=garbage_gate1\n", ""}, {"save=radik zagirov - to garbage\n", ""},
        {"level=l02_garbage\n", ""}, {"seed=14072026\n", ""}, {"origin=player\n", ""},
        {"agent=201,squad,1,stalker_squad,squad_v1\n", ""}
    };
    for (const auto& change : invalid)
    {
        const auto result = xrSim::ParseZoneManifest(Replace(kManifest, change.first, change.second));
        ok = Expect(!result.ok && !result.reason.empty(), ("reject: " + change.second).c_str()) && ok;
    }
    for (const std::string extra : {"unknown=x\n", "scenario=x\n", "seed=1\n", "save=x\n",
        "level=x\n", "origin=player\n", "xrsim_zone_manifest_v1\n",
        "resource=scrap_cache,scrap,scrapyard_center,40,5\n",
        "route=stalker_to_scrap,stalker_camp,scrapyard_center\n",
        "agent=999,squad,1,other,squad_v1\n"})
        ok = Expect(!xrSim::ParseZoneManifest(std::string(kManifest) + extra).ok, extra.c_str()) && ok;
    // Every incomplete final agent record must fail, not silently skip the record.
    const std::string text = kManifest;
    const size_t last = text.rfind("agent=203");
    for (size_t end = last; end < text.size() - 1; ++end)
        ok = Expect(!xrSim::ParseZoneManifest(text.substr(0, end)).ok, "truncated final record") && ok;
    std::string crlf;
    for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
    ok = Expect(xrSim::ParseZoneManifest(crlf).ok, "CRLF accepted") && ok;
    ok = Expect(xrSim::ParseZoneManifest(text.substr(0, text.size() - 1)).ok, "final LF optional") && ok;
    ok = Expect(xrSim::ParseZoneManifest(Replace(text, "garbage_gate1", "Зона")).ok, "UTF-8 name accepted") && ok;
    ok = Expect(xrSim::ParseZoneManifest(Replace(Replace(text, "3500,0,0", "-2147483648,2147483647,0"),
        "seed=14072026", "seed=4294967295")).ok, "integer endpoints accepted") && ok;
    ok = Expect(xrSim::ParseZoneManifest(Replace(text, "seed=14072026", "seed=0")).ok, "zero seed valid") && ok;
    return ok;
}

bool TestDirectBootstrapValidationAndAtomicity()
{
    const auto parsed = xrSim::ParseZoneManifest(kManifest);
    if (!Expect(parsed.ok, "direct bootstrap fixture parses")) return false;
    xrSim::ZoneState world;
    if (!Expect(world.Bootstrap(parsed.manifest).ok, "initial bootstrap")) return false;
    const auto firstFaction = world.FindFactionByName("stalker");
    const auto secondFaction = world.FindFactionByName("bandit");
    const auto firstGroup = world.FindGroupByName("stalker_squad");
    const auto firstLocation = world.FindLocationByName("stalker_camp");
    RequireSetup(world.SetRelation(firstFaction, secondFaction, -10));
    RequireSetup(world.AppendIncident({1, firstLocation, "disturbance", 5, true}));
    RequireSetup(world.PromoteSalientActor(firstGroup, 123));
    xrSim::ObjectiveRecord objective;
    objective.subject = firstGroup; objective.target = firstLocation; objective.active = true;
    world.MutableObjectives().push_back(objective);
    world.FindLocation(firstLocation)->ownerFaction = firstFaction;
    world.FindRoute(world.FindRouteByName("stalker_to_scrap"))->open = false;
    world.FindResource(world.FindResourceByName("scrap_cache"))->demand = 10;
    world.SetRevision(77);
    RequireSetup(world.AppendEvent({0, 20, "observed", {}, {}, 2, "test"}));
    const auto before = world.SaveSnapshot();
    const auto priorRevision = world.Revision();
    const auto priorEpoch = world.Epoch();
    const auto digest = world.Digest();
    const std::vector<std::function<void(xrSim::ZoneManifest&)>> corrupt{
        [](auto& m) { m.scenario.clear(); }, [](auto& m) { m.origin = "unknown"; },
        [](auto& m) { m.groups[0].count = 0; }, [](auto& m) { m.groups[0].count = 9; },
        [](auto& m) { m.groups[2].ownerDomain = "stalker"; },
        [](auto& m) { m.groups[0].kind = "unknown"; },
        [](auto& m) { m.groups[0].agentId = 203; },
        [](auto& m) { m.factions[0].agentId = 201; },
        [](auto& m) { m.agents[0].scopeId = 0; },
        [](auto& m) { m.agents[6].scopeId = 2; },
        [](auto& m) { m.agents.pop_back(); },
        [](auto& m) { m.agents[0].name = std::string(97, 'x'); },
        [](auto& m) { m.anchors[0].name = "x\ny"; },
        [](auto& m) { m.groups[0].section = "x,y"; },
        [](auto& m) { m.resources[0].minimum = 41; },
        [](auto& m) { m.resources[0].quantity = 1001; },
        [](auto& m) { m.routes[0].to = "missing"; },
        [](auto& m) { m.anchors.push_back(m.anchors[0]); },
        [](auto& m) { m.groups.resize(33, m.groups[0]); },
        [](auto& m) { m.agents.resize(33, m.agents[0]); },
        [](auto& m) { m.anchors.resize(65, m.anchors[0]); },
        [](auto& m) { m.routes.resize(65, m.routes[0]); },
        [](auto& m) { m.resources.resize(65, m.resources[0]); }
    };
    bool ok = true;
    for (size_t i = 0; i < corrupt.size(); ++i)
    {
        auto invalid = parsed.manifest;
        corrupt[i](invalid);
        const auto result = world.Bootstrap(invalid);
        ok = Expect(!result.ok && !result.reason.empty() && result.revision == priorRevision,
            ("invalid direct bootstrap " + std::to_string(i)).c_str()) && ok;
        ok = Expect(world.SaveSnapshot() == before && world.Digest() == digest && world.Revision() == priorRevision && world.Epoch() == priorEpoch,
            "failed bootstrap preserves complete previous state") && ok;
    }
    xrSim::ZoneState fresh;
    RequireSetup(fresh.Bootstrap(parsed.manifest));
    ok = Expect(world.Bootstrap(parsed.manifest).ok && world.Revision() == 1 && world.Events().empty() &&
        world.Epoch() == priorEpoch + 1 && world.SaveSnapshot() != fresh.SaveSnapshot(),
        "successful replacement resets state but advances world lifetime") && ok;
    const auto oldHandle = world.FindGroupByName("bandit_squad");
    ok = Expect(world.Bootstrap(parsed.manifest).ok && world.FindGroupByName("bandit_squad") == oldHandle,
        "identical replacement produces identical handles") && ok;
    return ok;
}

bool TestTypedHandleResolution()
{
    xrSim::ZoneState world;
    RequireSetup(world.Bootstrap(FixtureManifest()));
    const auto group = world.FindGroupByName("stalker_squad");
    const auto location = world.FindLocationByName("stalker_camp");
    const auto faction = world.FindFactionByName("stalker");
    const auto route = world.FindRouteByName("stalker_to_scrap");
    const auto resource = world.FindResourceByName("scrap_cache");
    bool ok = Expect(std::set<uint32_t>{group.value, location.value, faction.value, route.value, resource.value}.size() == 5,
        "canonical handle tables cannot alias");
    ok = Expect(!world.FindLocation(route) && !world.FindGroup(faction) && !world.FindFaction(group),
        "wrong-kind lookup rejected") && ok;
    for (const auto bad : {xrSim::Handle{}, xrSim::Handle{group.Index()},
        xrSim::Handle{group.value + (1u << 24)}, xrSim::Handle{0x01fffffeu}})
        ok = Expect(!world.FindGroup(bad), "invalid/stale group handle rejected") && ok;
    auto corruptedStage = world;
    corruptedStage.FindGroup(group)->id = {group.Index()};
    const auto& readOnlyStage = corruptedStage;
    ok = Expect(!readOnlyStage.FindGroup({group.Index()}), "const lookup rejects generation zero even in invalid stage") && ok;
    const auto* record = world.FindGroup(group);
    ok = Expect(record && record->faction == faction && record->location == location && record->aliveCount == 3 &&
        record->initialCount == 3 && record->agentId == 201 && group.Generation() == 1, "fixture squad canonical facts") && ok;
    const auto* pack = world.FindGroup(world.FindGroupByName("garbage_dogs"));
    return Expect(pack && pack->kind == xrSim::GroupKind::MutantPack && !pack->faction.IsValid() && pack->aliveCount == 4,
        "ecology pack has no faction") && ok;
}

bool TestSnapshotsCoverMutations()
{
    xrSim::ZoneState base;
    RequireSetup(base.Bootstrap(FixtureManifest()));
    const auto group = base.FindGroupByName("stalker_squad");
    const auto location = base.FindLocationByName("stalker_camp");
    const auto resource = base.FindResourceByName("scrap_cache");
    const auto route = base.FindRouteByName("stalker_to_scrap");
    const auto first = base.FindFactionByName("stalker");
    const auto second = base.FindFactionByName("bandit");
    const std::vector<std::function<void(xrSim::ZoneState&)>> changes{
        [](auto& w) { w.SetRevision(2); },
        [=](auto& w) { w.FindLocation(location)->ownerFaction = first; },
        [=](auto& w) { w.FindLocation(location)->position.levelVertex = 8; },
        [=](auto& w) { w.FindLocation(location)->position.yCm = 2; },
        [=](auto& w) { w.FindLocation(location)->name += " | x\n"; },
        [=](auto& w) { w.FindGroup(group)->section = "another"; },
        [=](auto& w) { w.FindGroup(group)->agentId = 500; },
        [=](auto& w) { w.FindGroup(group)->active = false; },
        [=](auto& w) { w.FindGroup(group)->location = {}; },
        [=](auto& w) { w.FindGroup(group)->faction = second; },
        [=](auto& w) { w.FindGroup(group)->kind = xrSim::GroupKind::MutantPack; },
        [=](auto& w) { w.FindGroup(group)->id.value += 1u << 24; },
        [=](auto& w) { w.FindGroup(group)->aliveCount = 2; },
        [=](auto& w) { w.FindGroup(group)->initialCount = 8; },
        [=](auto& w) { w.FindRoute(route)->open = false; },
        [=](auto& w) { w.FindRoute(route)->to = location; },
        [=](auto& w) { w.FindRoute(route)->from = {}; },
        [=](auto& w) { w.FindRoute(route)->name = "alternate"; },
        [=](auto& w) { w.FindResource(resource)->minimum = 1; },
        [=](auto& w) { w.FindResource(resource)->maximum = 50; },
        [=](auto& w) { w.FindResource(resource)->supply = 10; },
        [=](auto& w) { w.FindResource(resource)->demand = 10; },
        [=](auto& w) { w.FindResource(resource)->kind = "food"; },
        [=](auto& w) { w.FindResource(resource)->location = location; },
        [=](auto& w) { w.FindResource(resource)->quantity = 42; },
        [=](auto& w) { RequireSetup(w.SetRelation(first, second, -20)); },
        [=](auto& w) { RequireSetup(w.AppendIncident({3, location, "disturbance", 5, true})); },
        [=](auto& w) { RequireSetup(w.PromoteSalientActor(group, 123)); },
        [=](auto& w) { RequireSetup(w.AppendEvent({999, 15, "event with spaces", group, location, 1, "observed"})); },
        [=](auto& w) { xrSim::ObjectiveRecord o; o.planId=1; o.subject=group; o.target=location;
            o.subjectName="stalker_squad"; o.active=true; w.MutableObjectives().push_back(o); }
    };
    bool ok = true;
    for (size_t i = 0; i < changes.size(); ++i)
    {
        auto changed = base;
        changes[i](changed);
        if (i != 0) changed.SetRevision(base.Revision()); // Content must be visible independently of revision.
        ok = Expect(changed.SaveSnapshot() != base.SaveSnapshot() && changed.Digest() != base.Digest(),
            ("snapshot and digest cover mutation " + std::to_string(i)).c_str()) && ok;
    }
    RequireSetup(base.AppendEvent({77, 1, "first", {}, {}, 0, "test"}));
    RequireSetup(base.AppendEvent({77, 2, "second", {}, {}, 0, "test"}));
    ok = Expect(base.Events().size() == 2 && base.Events()[0].seq == 1 && base.Events()[1].seq == 2,
        "events receive canonical sequential identity") && ok;
    auto copy = base;
    RequireSetup(copy.AppendEvent({0, 3, "third", {}, {}, 0, "test"}));
    return Expect(copy.Events().back().seq == 3 && base.Events().size() == 2, "copy preserves independent event sequence") && ok;
}

bool TestScopeOrdinalsAndMutationGuards()
{
    xrSim::ZoneState world;
    RequireSetup(world.Bootstrap(FixtureManifest()));
    const auto first = world.FindFactionByName("stalker");
    const auto second = world.FindFactionByName("bandit");
    const auto group = world.FindGroupByName("stalker_squad");
    const auto location = world.FindLocationByName("stalker_camp");
    bool ok = Expect(world.ResolveScopeSubject("squad", 1) == group &&
        world.ResolveScopeSubject("faction", 1) == first &&
        world.ResolveScopeSubject("mutant_pack", 3) == world.FindGroupByName("garbage_dogs"),
        "scope ordinals resolve by explicit record kind");
    for (const auto& scope : std::vector<std::pair<std::string, uint32_t>>{
        {"squad", 0}, {"squad", 3}, {"mutant_pack", 1}, {"faction", 3}, {"squad", group.value}, {"zone", 1}})
        ok = Expect(!world.ResolveScopeSubject(scope.first, scope.second).IsValid(), "invalid entity scope lookup") && ok;
    const auto before = world.SaveSnapshot();
    const std::vector<std::function<xrSim::ZoneResult(xrSim::ZoneState&)>> invalid{
        [=](auto& w) { return w.AddGroup(*w.FindGroup(group)); },
        [](auto& w) { return w.AddGroup({}); },
        [=](auto& w) { return w.DeactivateGroup(group); },
        [=](auto& w) { return w.DeactivateGroup(location); },
        [=](auto& w) { return w.SetRelation(first, first, 0); },
        [=](auto& w) { return w.SetRelation(first, location, 0); },
        [=](auto& w) { return w.SetRelation(first, second, 101); },
        [=](auto& w) { return w.AppendIncident({1, group, "event", 2, true}); },
        [=](auto& w) { return w.AppendIncident({1, location, "event", 11, true}); },
        [=](auto& w) { return w.PromoteSalientActor(location, 123); },
        [=](auto& w) { return w.PromoteSalientActor(group, 0xffffu); }
    };
    for (size_t i = 0; i < invalid.size(); ++i)
    {
        auto w = world;
        const auto result = invalid[i](w);
        ok = Expect(!result.ok && !result.reason.empty() && w.SaveSnapshot() == before,
            ("invalid canonical mutation is atomic " + std::to_string(i)).c_str()) && ok;
    }
    auto created = *world.FindGroup(group);
    created.name = "reinforcement";
    created.agentId = 300;
    if (!Expect(world.AddGroup(created).ok, "valid group appended")) return false;
    const auto appended = world.FindGroupByName("reinforcement");
    ok = Expect(appended != group && !world.FindLocation(appended) && world.FindGroup(group),
        "append allocates fresh handle, preserves earlier IDs") && ok;
    ok = Expect(world.PromoteSalientActor(group, 123).ok, "actor promoted") && ok;
    auto promoted = world.SaveSnapshot();
    ok = Expect(world.PromoteSalientActor(group, 123).ok && world.SaveSnapshot() == promoted,
        "repeat observation preserves salient identity") && ok;
    ok = Expect(world.SalientActors().size() == 1 && world.SalientActors()[0].id != appended &&
        !world.FindGroup(world.SalientActors()[0].id), "actor handle is globally unique") && ok;
    const auto other = world.FindGroupByName("bandit_squad");
    ok = Expect(!world.PromoteSalientActor(other, 123).ok, "live identity cannot belong to two groups") && ok;
    world.FindGroup(appended)->aliveCount = 0;
    xrSim::ObjectiveRecord objective;
    objective.subject = appended; objective.active = true;
    world.MutableObjectives().push_back(objective);
    ok = Expect(!world.DeactivateGroup(appended).ok, "active objective prevents dissolution") && ok;
    world.MutableObjectives().back().active = false;
    ok = Expect(world.DeactivateGroup(appended).ok && !world.FindGroup(appended)->active,
        "empty unreferenced group deactivates without erasing ID") && ok;
    auto relationA = world, relationB = world;
    RequireSetup(relationA.SetRelation(first, second, -10));
    RequireSetup(relationB.SetRelation(second, first, -10));
    return Expect(relationA.SaveSnapshot() == relationB.SaveSnapshot(), "symmetric relation has canonical order") && ok;
}

bool TestSnapshotFieldSensitivityAndLocale()
{
    xrSim::ZoneState base;
    RequireSetup(base.Bootstrap(FixtureManifest()));
    xrSim::ObjectiveRecord objective;
    objective.subject = base.FindGroupByName("stalker_squad");
    objective.target = base.FindLocationByName("stalker_camp");
    base.MutableObjectives().push_back(objective);
    const std::vector<std::function<void(xrSim::ObjectiveRecord&)>> changes{
        [](auto& o) { o.planId = 1; }, [](auto& o) { o.scopeKind = 1; },
        [](auto& o) { o.scopeId = 1; }, [](auto& o) { o.subjectKind = xrSim::ObjectiveSubjectKind::Faction; },
        [](auto& o) { o.subject = {}; }, [](auto& o) { o.subjectName = "x\ny|z"; },
        [](auto& o) { o.kind = xrSim::ObjectiveKind::Move; }, [](auto& o) { o.target = {}; },
        [](auto& o) { o.expiresAtTick = 1; }, [](auto& o) { o.author = xrSim::ObjectiveAuthor::Ai; },
        [](auto& o) { o.active = true; }
    };
    bool ok = true;
    for (auto& change : changes)
    {
        auto copy = base; change(copy.MutableObjectives()[0]);
        ok = Expect(copy.SaveSnapshot() != base.SaveSnapshot() && copy.Digest() != base.Digest(),
            "every objective field participates in snapshot/digest") && ok;
    }
    // Identical text concatenations with different string boundaries must differ.
    auto a = base, b = base;
    RequireSetup(a.AppendEvent({0, 1, "a b", {}, {}, 1, "c"}));
    RequireSetup(b.AppendEvent({0, 1, "a", {}, {}, 1, "b c"}));
    ok = Expect(a.SaveSnapshot() != b.SaveSnapshot(), "snapshot string boundaries are unambiguous") && ok;
    struct GroupedNumbers : std::numpunct<char>
    {
        char do_thousands_sep() const override { return ','; }
        std::string do_grouping() const override { return "\3"; }
    };
    const auto before = base.SaveSnapshot();
    const auto digest = base.Digest();
    const auto oldLocale = std::locale();
    std::locale::global(std::locale(oldLocale, new GroupedNumbers));
    ok = Expect(base.SaveSnapshot() == before && base.Digest() == digest, "serialization ignores process locale") && ok;
    std::locale::global(oldLocale);
    return ok;
}

bool TestLifetimeAndRevisionDiscipline()
{
    const auto manifest = FixtureManifest();
    xrSim::ZoneState world;
    bool ok = Expect(world.Epoch() == 0 && world.Revision() == 0, "empty world has no lifetime");
    ok = Expect(world.Bootstrap(manifest).ok && world.Epoch() == 1, "first bootstrap starts lifetime") && ok;
    const auto first = world.FindFactionByName("stalker");
    const auto second = world.FindFactionByName("bandit");
    auto copy = world;
    const auto old = world.SaveSnapshot();
    ok = Expect(world.Bootstrap(manifest).ok && world.Epoch() == 2 && world.Revision() == 1 &&
        world.SaveSnapshot() != old && copy.Epoch() == 1, "replacement invalidates old staged lifetime") && ok;
    const auto revision = world.Revision();
    const auto relation = world.SetRelation(first, second, -10);
    ok = Expect(relation.ok && relation.revision == revision + 1 && world.Revision() == revision + 1,
        "canonical relation mutation advances revision once") && ok;
    ok = Expect(world.SetRelation(second, first, -10).ok && world.Revision() == revision + 1,
        "relation noop does not advance revision") && ok;
    RequireSetup(world.AppendEvent({0, 1, "observed", {}, {}, 0, "test"}));
    ok = Expect(world.Revision() == revision + 2, "event append advances revision") && ok;
    const auto group = world.FindGroupByName("stalker_squad");
    RequireSetup(world.PromoteSalientActor(group, 123));
    ok = Expect(world.Revision() == revision + 3, "promotion advances revision") && ok;
    RequireSetup(world.PromoteSalientActor(group, 123));
    ok = Expect(world.Revision() == revision + 3, "repeat promotion is a noop") && ok;
    world.SetRevision(std::numeric_limits<uint64_t>::max());
    const auto exhausted = world.SaveSnapshot();
    ok = Expect(!world.SetRelation(first, second, 10).ok && world.SaveSnapshot() == exhausted,
        "revision overflow rejects relation atomically") && ok;
    ok = Expect(!world.PromoteSalientActor(group, 124).ok && world.SaveSnapshot() == exhausted,
        "revision overflow rejects promotion atomically") && ok;
    ok = Expect(!world.AppendEvent({0, 2, "overflow", {}, {}, 0, "test"}).ok, "event overflow returns failure") && ok;
    return Expect(world.SaveSnapshot() == exhausted, "revision overflow rejects event atomically") && ok;
}

bool TestManifestLimitsAndCanonicalMetadata()
{
    bool ok = true;
    xrSim::ZoneState baseline;
    RequireSetup(baseline.Bootstrap(FixtureManifest()));
    const std::vector<std::pair<std::string, std::string>> validChanges{
        {"garbage_gate1", "alternate"}, {"radik zagirov - to garbage", "save, with punctuation = ok"},
        {"l02_garbage", "other_level"}, {"seed=14072026", "seed=0"},
        {"zone_director,", "director_renamed,"}, {"agent=100,", "agent=500,"},
        {"stalker_faction_v1", "bandit_faction_v1"}
    };
    for (const auto& change : validChanges)
    {
        const auto result = xrSim::ParseZoneManifest(Replace(kManifest, change.first, change.second));
        xrSim::ZoneState world;
        ok = Expect(result.ok && world.Bootstrap(result.manifest).ok && world.SaveSnapshot() != baseline.SaveSnapshot() &&
            world.Digest() != baseline.Digest(), "all canonical metadata contributes to identity") && ok;
    }
    for (const auto& table : {std::string("anchor"), std::string("route"), std::string("resource")})
    {
        const size_t initial = table == "anchor" ? 3 : 1;
        std::string text = kManifest;
        for (size_t i = initial; i <= 64; ++i)
        {
            if (i == 64) ok = Expect(xrSim::ParseZoneManifest(text).ok, "table accepts exactly 64 records") && ok;
            text += table + "=extra_" + std::to_string(i);
            if (table == "anchor") text += ",0,0,0\n";
            if (table == "route") text += ",stalker_camp,bandit_camp\n";
            if (table == "resource") text += ",scrap,stalker_camp,1000,1000\n";
        }
        ok = Expect(!xrSim::ParseZoneManifest(text).ok, "table rejects record 65") && ok;
    }
    std::string text = kManifest;
    // 9 existing agents + 23 new faction agents = 32, with complete scope references.
    for (size_t i = 0; i <= 23; ++i)
    {
        if (i == 23) ok = Expect(xrSim::ParseZoneManifest(text).ok, "32 agents accepted") && ok;
        const auto id = std::to_string(300 + i);
        text += "faction=f" + id + "," + id + "\nagent=" + id + ",faction," + std::to_string(3 + i) +
            ",a" + id + ",stalker_faction_v1\n";
    }
    ok = Expect(!xrSim::ParseZoneManifest(text).ok, "agent 33 rejected") && ok;
    ok = Expect(xrSim::ParseZoneManifest(Replace(kManifest, "scenario=garbage_gate1",
        "scenario=" + std::string(96, 'x'))).ok, "96-byte name accepted") && ok;
    return ok;
}

bool TestDynamicGroupOwnershipAndText()
{
    xrSim::ZoneState base;
    RequireSetup(base.Bootstrap(FixtureManifest()));
    auto candidate = *base.FindGroup(base.FindGroupByName("stalker_squad"));
    candidate.name = "new_group"; candidate.agentId = 300;
    bool ok = true;
    for (const auto& agent : base.SourceManifest().agents)
    {
        auto world = base;
        candidate.agentId = agent.agentId;
        const auto before = world.SaveSnapshot();
        const auto result = world.AddGroup(candidate);
        ok = Expect(!result.ok && !result.reason.empty() && world.SaveSnapshot() == before,
            "dynamic group rejects every manifest agent identity atomically") && ok;
    }
    candidate.agentId = 300;
    const std::vector<std::string> invalidNames{
        "", std::string(97, 'x'), "a,b", "a\tb", "a\nb", "a\rb", "a\x01", "a\x7f",
        std::string("a\0b", 3), "\xff\xfe", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc2\x85"
    };
    for (const auto& name : invalidNames)
    {
        auto world = base; candidate.name = name;
        const auto before = world.SaveSnapshot();
        const auto result = world.AddGroup(candidate);
        ok = Expect(!result.ok && world.SaveSnapshot() == before, "invalid dynamic name rejected atomically") && ok;
    }
    for (const auto& name : {std::string(96, 'x'), std::string("Сталкеры")})
    {
        auto world = base; candidate.name = name;
        ok = Expect(world.AddGroup(candidate).ok && world.FindGroupByName(name).IsValid(),
            "bounded valid UTF-8 dynamic name accepted") && ok;
    }
    return ok;
}

bool TestIncidentAndEventInputValidation()
{
    xrSim::ZoneState base;
    RequireSetup(base.Bootstrap(FixtureManifest()));
    const auto group = base.FindGroupByName("stalker_squad");
    const auto location = base.FindLocationByName("stalker_camp");
    RequireSetup(base.PromoteSalientActor(group, 123));
    bool ok = true;
    for (const auto& bad : std::vector<std::string>{"", std::string(97, 'x'), "x,y", "x\ny", "x\ty",
        "x\x7f", "\xc0\xaf", "\xed\xa0\x80", "\xff"})
    {
        auto world = base;
        const auto before = world.SaveSnapshot();
        ok = Expect(!world.AppendIncident({1, location, bad, 1, true}).ok && world.SaveSnapshot() == before,
            "invalid incident kind rejected atomically") && ok;
    }
    xrSim::WorldEvent event{999, 5, "observed", group, location, 1, "source"};
    const std::vector<std::function<void(xrSim::WorldEvent&)>> corrupt{
        [](auto& e) { e.kind.clear(); }, [](auto& e) { e.kind = std::string(97, 'x'); },
        [](auto& e) { e.kind = "x\ny"; }, [](auto& e) { e.kind = "\xff"; },
        [](auto& e) { e.source = std::string(513, 'x'); }, [](auto& e) { e.source = "x\ty"; },
        [](auto& e) { e.source = "x\ry"; }, [](auto& e) { e.source = "\xed\xa0\x80"; },
        [](auto& e) { e.source = std::string("x\0y", 3); },
        [](auto& e) { e.subject = {0x01fffffeu}; }, [](auto& e) { e.object = {0x01fffffeu}; },
        [](auto& e) { e.subject.value &= 0x00ffffffu; },
        [](auto& e) { e.object.value += 1u << 24; }
    };
    for (const auto& change : corrupt)
    {
        auto world = base; auto invalid = event; change(invalid);
        const auto before = world.SaveSnapshot();
        const auto result = world.AppendEvent(invalid);
        ok = Expect(!result.ok && !result.reason.empty() && world.SaveSnapshot() == before,
            "event validation failure preserves all state and counters") && ok;
    }
    auto world = base;
    RequireSetup(world.AppendIncident({1, location, "Инцидент", 1, true}));
    event.kind = std::string(96, 'x'); event.source = std::string(512, 'x');
    ok = Expect(world.AppendEvent(event).ok, "event text endpoints accepted") && ok;
    const std::vector<xrSim::Handle> participants{xrSim::Handle::Invalid(), group, location,
        base.FindFactionByName("stalker"), base.FindRouteByName("stalker_to_scrap"),
        base.FindResourceByName("scrap_cache"), base.SalientActors()[0].id};
    for (const auto id : participants)
    {
        event.subject = id; event.object = id; event.kind = "Событие"; event.source.clear();
        const auto revision = world.Revision();
        ok = Expect(world.AppendEvent(event).ok && world.Revision() == revision + 1,
            "event accepts canonical handle types or absent participants and empty source") && ok;
    }
    return ok;
}

bool TestDeactivationProtectsSalientChildren()
{
    xrSim::ZoneState base;
    RequireSetup(base.Bootstrap(FixtureManifest()));
    const auto group = base.FindGroupByName("stalker_squad");
    RequireSetup(base.PromoteSalientActor(group, 123));
    base.FindGroup(group)->aliveCount = 0; // Stage observed group outcome independently of actor outcome.
    auto aliveChild = base;
    const auto before = aliveChild.SaveSnapshot();
    bool ok = Expect(!aliveChild.DeactivateGroup(group).ok && aliveChild.SaveSnapshot() == before,
        "live salient child blocks dissolution atomically");
    // This is a non-const test-owned staging copy; no live death API is added for fixture setup.
    const_cast<xrSim::SalientActorRecord&>(base.SalientActors()[0]).alive = false;
    xrSim::ObjectiveRecord objective;
    objective.subjectKind = xrSim::ObjectiveSubjectKind::NamedActor;
    objective.subject = base.SalientActors()[0].id; objective.active = true;
    base.MutableObjectives().push_back(objective);
    auto plannedWorld = base;
    const auto planned = plannedWorld.SaveSnapshot();
    ok = Expect(!plannedWorld.DeactivateGroup(group).ok && plannedWorld.SaveSnapshot() == planned,
        "active named-actor objective on dead child blocks dissolution") && ok;
    base.MutableObjectives().back().active = false;
    const auto revision = base.Revision();
    ok = Expect(base.DeactivateGroup(group).ok && base.Revision() == revision + 1 &&
        !base.FindGroup(group)->active && base.SalientActors().size() == 1,
        "dead child without active objective permits dissolution and retains identities") && ok;
    return ok;
}

bool TestActiveGroupCapacityAndAppendOnlyOrdinals()
{
    xrSim::ZoneState world;
    RequireSetup(world.Bootstrap(FixtureManifest()));
    const auto original = world.FindGroupByName("stalker_squad");
    auto candidate = *world.FindGroup(original);
    for (uint32_t i = 0; i < 29; ++i)
    {
        candidate.name = "dynamic_" + std::to_string(i); candidate.agentId = 300 + i;
        RequireSetup(world.AddGroup(candidate));
    }
    bool ok = Expect(world.Groups().size() == 32 && world.ResolveScopeSubject("squad", 32) ==
        world.FindGroupByName("dynamic_28"), "32 active groups fit with dynamic append-only ordinals");
    candidate.name = "overflow"; candidate.agentId = 400;
    const auto full = world.SaveSnapshot();
    ok = Expect(!world.AddGroup(candidate).ok && world.SaveSnapshot() == full, "active group 33 rejected atomically") && ok;
    // Use a fresh saturated world even against the broken implementation, so each check is independent.
    if (world.Groups().size() != 32) return ok;
    const auto retired = world.FindGroupByName("dynamic_0");
    world.FindGroup(retired)->aliveCount = 0;
    RequireSetup(world.DeactivateGroup(retired));
    RequireSetup(world.AddGroup(candidate));
    ok = Expect(world.Groups().size() == 33 && world.ResolveScopeSubject("squad", 4) == retired &&
        world.ResolveScopeSubject("squad", 33) == world.FindGroupByName("overflow") &&
        world.ResolveScopeSubject("squad", 1) == original && !world.FindGroup(retired)->active,
        "tombstone frees active capacity but preserves ordinal, new group appends ordinal 33") && ok;
    auto identityCheck = world;
    identityCheck.FindGroup(original)->aliveCount = 0;
    RequireSetup(identityCheck.DeactivateGroup(original));
    candidate.name = "reuse_owner"; candidate.agentId = 300;
    const auto before = identityCheck.SaveSnapshot();
    ok = Expect(!identityCheck.AddGroup(candidate).ok && identityCheck.SaveSnapshot() == before,
        "inactive group retains owner identity for lifetime") && ok;
    candidate.name = "dynamic_0"; candidate.agentId = 500;
    ok = Expect(!identityCheck.AddGroup(candidate).ok && identityCheck.SaveSnapshot() == before,
        "inactive group retains name for lifetime") && ok;
    candidate.name = "next";
    RequireSetup(identityCheck.AddGroup(candidate));
    return Expect(identityCheck.Groups().size() == 34 && identityCheck.ResolveScopeSubject("squad", 34) ==
        identityCheck.FindGroupByName("next"), "lifetime records exceed 32 without reusing any ordinal") && ok;
}

}

int main()
{
    bool ok = true;
    ok = TestManifestBootstrapsDeterministically() && ok;
    ok = TestManifestRejectsUnknownAndDuplicateRecords() && ok;
    ok = TestStrictManifestValidation() && ok;
    ok = TestDirectBootstrapValidationAndAtomicity() && ok;
    ok = TestTypedHandleResolution() && ok;
    ok = TestSnapshotsCoverMutations() && ok;
    ok = TestScopeOrdinalsAndMutationGuards() && ok;
    ok = TestSnapshotFieldSensitivityAndLocale() && ok;
    ok = TestLifetimeAndRevisionDiscipline() && ok;
    ok = TestManifestLimitsAndCanonicalMetadata() && ok;
    ok = TestDynamicGroupOwnershipAndText() && ok;
    ok = TestIncidentAndEventInputValidation() && ok;
    ok = TestDeactivationProtectsSalientChildren() && ok;
    ok = TestActiveGroupCapacityAndAppendOnlyOrdinals() && ok;
    return ok ? 0 : 1;
}
