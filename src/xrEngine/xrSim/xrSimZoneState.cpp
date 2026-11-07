#include "xrSim/xrSimZoneState.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>
#include <utility>

namespace xrSim
{
namespace
{
constexpr uint32_t ZoneIndexMask = 0x00ffffffu;
constexpr uint32_t ZoneGenerationShift = 24;

Handle MakeZoneHandle(uint32_t index, uint8_t generation = 1)
{
    return Handle{ (uint32_t(generation) << ZoneGenerationShift) | (index & ZoneIndexMask) };
}
} // namespace

ZoneResult ZoneState::Bootstrap(const ZoneManifest& manifest)
{
    if (m_epoch == std::numeric_limits<uint64_t>::max())
        return {false, "world epoch exhausted", m_revision};
    const auto validation = ValidateZoneManifest(manifest);
    if (!validation.ok)
        return {false, validation.reason, m_revision};
    ZoneState staged;
    staged.m_manifest = manifest;
    staged.m_epoch = m_epoch + 1;

    for (size_t i = 0; i < manifest.factions.size(); ++i)
    {
        const auto& mf = manifest.factions[i];
        FactionRecord f;
        f.id = MakeZoneHandle(staged.m_nextHandleIndex++, 1);
        f.name = mf.name;
        f.agentId = mf.agentId;
        staged.m_factions.push_back(f);
    }

    for (size_t i = 0; i < manifest.anchors.size(); ++i)
    {
        const auto& ma = manifest.anchors[i];
        LocationRecord loc;
        loc.id = MakeZoneHandle(staged.m_nextHandleIndex++, 1);
        loc.name = ma.name;
        loc.position.xCm = ma.xCm;
        loc.position.yCm = ma.yCm;
        loc.position.zCm = ma.zCm;
        loc.position.levelVertex = 0xffffffffu;
        loc.ownerFaction = Handle::Invalid();
        staged.m_locations.push_back(loc);
    }

    for (size_t i = 0; i < manifest.routes.size(); ++i)
    {
        const auto& mr = manifest.routes[i];
        const Handle from = staged.FindLocationByName(mr.from);
        const Handle to = staged.FindLocationByName(mr.to);
        if (!from.IsValid() || !to.IsValid())
            return ZoneResult{ false, "route references undefined anchor: " + mr.name, 0 };

        RouteRecord route;
        route.id = MakeZoneHandle(staged.m_nextHandleIndex++, 1);
        route.name = mr.name;
        route.from = from;
        route.to = to;
        route.open = true;
        staged.m_routes.push_back(route);
    }

    for (size_t i = 0; i < manifest.resources.size(); ++i)
    {
        const auto& mres = manifest.resources[i];
        const Handle loc = staged.FindLocationByName(mres.anchor);
        if (!loc.IsValid())
            return ZoneResult{ false, "resource references undefined anchor: " + mres.anchor, 0 };

        ResourceRecord res;
        res.id = MakeZoneHandle(staged.m_nextHandleIndex++, 1);
        res.name = mres.name;
        res.kind = mres.kind;
        res.location = loc;
        res.quantity = mres.quantity;
        res.minimum = mres.minimum;
        res.supply = 0;
        res.demand = 0;
        res.maximum = 1000;
        staged.m_resources.push_back(res);
    }

    for (size_t i = 0; i < manifest.groups.size(); ++i)
    {
        const auto& mg = manifest.groups[i];
        const Handle loc = staged.FindLocationByName(mg.anchor);
        if (!loc.IsValid())
            return ZoneResult{ false, "group references undefined anchor: " + mg.anchor, 0 };

        Handle faction = Handle::Invalid();
        GroupKind kind = GroupKind::Squad;
        if (mg.kind == "squad")
        {
            kind = GroupKind::Squad;
            faction = staged.FindFactionByName(mg.ownerDomain);
            if (!faction.IsValid())
                return ZoneResult{ false, "squad references undefined faction: " + mg.ownerDomain, 0 };
        }
        else if (mg.kind == "mutant_pack")
        {
            kind = GroupKind::MutantPack;
            faction = Handle::Invalid();
        }
        else
        {
            return ZoneResult{ false, "unknown group kind: " + mg.kind, 0 };
        }

        GroupRecord grp;
        grp.id = MakeZoneHandle(staged.m_nextHandleIndex++, 1);
        grp.name = mg.name;
        grp.kind = kind;
        grp.faction = faction;
        grp.section = mg.section;
        grp.initialCount = mg.count;
        grp.aliveCount = mg.count;
        grp.location = loc;
        grp.agentId = mg.agentId;
        grp.active = true;
        staged.m_groups.push_back(grp);
    }

    staged.m_revision = 1;
    *this = std::move(staged);
    return ZoneResult{ true, "", m_revision };
}

uint64_t ZoneState::Epoch() const { return m_epoch; }

uint64_t ZoneState::Revision() const
{
    return m_revision;
}

const ZoneManifest& ZoneState::SourceManifest() const { return m_manifest; }
const std::vector<GroupRecord>& ZoneState::Groups() const { return m_groups; }
const std::vector<SalientActorRecord>& ZoneState::SalientActors() const { return m_salientActors; }

Handle ZoneState::ResolveScopeSubject(const std::string& kind, uint32_t scopeId) const
{
    if (scopeId == 0) return Handle::Invalid();
    if (kind == "faction" && scopeId <= m_factions.size()) return m_factions[scopeId - 1].id;
    if ((kind == "squad" || kind == "mutant_pack") && scopeId <= m_groups.size())
    {
        const auto& group = m_groups[scopeId - 1];
        if ((kind == "squad" && group.kind == GroupKind::Squad) ||
            (kind == "mutant_pack" && group.kind == GroupKind::MutantPack)) return group.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindFactionByName(const std::string& name) const
{
    for (const auto& f : m_factions)
    {
        if (f.name == name)
            return f.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindLocationByName(const std::string& name) const
{
    for (const auto& l : m_locations)
    {
        if (l.name == name)
            return l.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindGroupByName(const std::string& name) const
{
    for (const auto& g : m_groups)
    {
        if (g.name == name)
            return g.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindResourceByName(const std::string& name) const
{
    for (const auto& r : m_resources)
    {
        if (r.name == name)
            return r.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindRouteByName(const std::string& name) const
{
    for (const auto& r : m_routes)
    {
        if (r.name == name)
            return r.id;
    }
    return Handle::Invalid();
}

const FactionRecord* ZoneState::FindFaction(Handle id) const
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (const auto& f : m_factions)
    {
        if (f.id == id)
            return &f;
    }
    return nullptr;
}

const LocationRecord* ZoneState::FindLocation(Handle id) const
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (const auto& l : m_locations)
    {
        if (l.id == id)
            return &l;
    }
    return nullptr;
}

LocationRecord* ZoneState::FindLocation(Handle id)
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (auto& l : m_locations)
    {
        if (l.id == id)
            return &l;
    }
    return nullptr;
}

const GroupRecord* ZoneState::FindGroup(Handle id) const
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (const auto& g : m_groups)
    {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

GroupRecord* ZoneState::FindGroup(Handle id)
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (auto& g : m_groups)
    {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

const ResourceRecord* ZoneState::FindResource(Handle id) const
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (const auto& r : m_resources)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

ResourceRecord* ZoneState::FindResource(Handle id)
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (auto& r : m_resources)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

const RouteRecord* ZoneState::FindRoute(Handle id) const
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (const auto& r : m_routes)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

RouteRecord* ZoneState::FindRoute(Handle id)
{
    if (!id.IsValid() || id.Generation() != 1) return nullptr;
    for (auto& r : m_routes)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

ZoneResult ZoneState::AddGroup(const GroupRecord& group)
{
    if (m_revision == 0 || m_revision == std::numeric_limits<uint64_t>::max() ||
        m_nextHandleIndex >= ZoneIndexMask)
        return {false, "world uninitialized or counter exhausted", m_revision};
    if (!IsValidZoneName(group.name) || FindGroupByName(group.name).IsValid() ||
        group.initialCount == 0 || group.initialCount > 8 || group.aliveCount > group.initialCount ||
        !group.active || group.agentId == 0 || !FindLocation(group.location))
        return {false, "invalid group fields or duplicate name", m_revision};
    if ((group.kind == GroupKind::Squad && !FindFaction(group.faction)) ||
        (group.kind == GroupKind::MutantPack && group.faction != Handle::Invalid()) ||
        (group.kind != GroupKind::Squad && group.kind != GroupKind::MutantPack))
        return {false, "invalid group kind or faction", m_revision};
    bool knownSection = false;
    for (const auto& source : m_manifest.groups)
        if (source.section == group.section &&
            ((source.kind == "squad") == (group.kind == GroupKind::Squad))) knownSection = true;
    if (!knownSection) return {false, "group section absent from manifest", m_revision};
    for (const auto& agent : m_manifest.agents)
        if (agent.agentId == group.agentId) return {false, "manifest agent already assigned", m_revision};
    size_t activeGroups = 0;
    for (const auto& existing : m_groups)
    {
        if (existing.agentId == group.agentId) return {false, "group owner already assigned", m_revision};
        if (existing.active) ++activeGroups;
    }
    if (activeGroups >= 32) return {false, "active group limit exceeded", m_revision};
    GroupRecord staged = group;
    staged.id = MakeZoneHandle(m_nextHandleIndex++);
    m_groups.push_back(std::move(staged));
    return {true, "", ++m_revision};
}

ZoneResult ZoneState::DeactivateGroup(Handle group)
{
    GroupRecord* record = FindGroup(group);
    if (!record || record->aliveCount != 0)
        return {false, "group missing or still populated", m_revision};
    for (const auto& objective : m_objectives)
        if (objective.active && (objective.subject == group || objective.target == group))
            return {false, "group referenced by active objective", m_revision};
    for (const auto& child : m_salientActors)
    {
        if (child.parentGroup != group) continue;
        if (child.alive) return {false, "group has live salient child", m_revision};
        for (const auto& objective : m_objectives)
            if (objective.active && objective.subjectKind == ObjectiveSubjectKind::NamedActor &&
                objective.subject == child.id)
                return {false, "salient child referenced by active objective", m_revision};
    }
    if (!record->active) return {true, "", m_revision};
    if (m_revision == std::numeric_limits<uint64_t>::max())
        return {false, "revision exhausted", m_revision};
    record->active = false;
    return {true, "", ++m_revision};
}

ZoneResult ZoneState::SetRelation(Handle first, Handle second, int16_t score)
{
    if (!FindFaction(first) || !FindFaction(second) || first == second || score < -100 || score > 100)
        return {false, "invalid faction relation", m_revision};
    if (second.value < first.value) std::swap(first, second);
    for (auto& relation : m_relations)
    {
        if (relation.first == first && relation.second == second)
        {
            if (relation.score == score) return {true, "", m_revision};
            if (m_revision == std::numeric_limits<uint64_t>::max()) return {false, "revision exhausted", m_revision};
            relation.score = score;
            return {true, "", ++m_revision};
        }
    }
    if (m_revision == std::numeric_limits<uint64_t>::max()) return {false, "revision exhausted", m_revision};
    const RelationRecord record{first, second, score};
    const auto position = std::lower_bound(m_relations.begin(), m_relations.end(), record,
        [](const RelationRecord& a, const RelationRecord& b)
        { return std::make_pair(a.first.value, a.second.value) < std::make_pair(b.first.value, b.second.value); });
    m_relations.insert(position, record);
    return {true, "", ++m_revision};
}

ZoneResult ZoneState::AppendIncident(const IncidentRecord& incident)
{
    if (incident.id == 0 || !FindLocation(incident.location) || !IsValidZoneName(incident.kind) ||
        incident.magnitude < 0 || incident.magnitude > 10 || !incident.pending)
        return {false, "invalid incident", m_revision};
    for (const auto& existing : m_incidents)
        if (existing.id == incident.id) return {false, "duplicate incident ID", m_revision};
    if (m_revision == std::numeric_limits<uint64_t>::max()) return {false, "revision exhausted", m_revision};
    m_incidents.push_back(incident);
    return {true, "", ++m_revision};
}

ZoneResult ZoneState::PromoteSalientActor(Handle group, uint16_t liveObjectId)
{
    const auto* parent = FindGroup(group);
    if (!parent || !parent->active || liveObjectId == 0xffffu)
        return {false, "invalid salient actor binding", m_revision};
    for (const auto& existing : m_salientActors)
    {
        if (existing.lastLiveObjectId != liveObjectId) continue;
        if (existing.parentGroup == group) return {true, "", m_revision};
        return {false, "live object already belongs to another group", m_revision};
    }
    if (m_revision == std::numeric_limits<uint64_t>::max() || m_nextHandleIndex >= ZoneIndexMask)
        return {false, "world counter exhausted", m_revision};
    SalientActorRecord actor;
    actor.id = MakeZoneHandle(m_nextHandleIndex++);
    actor.parentGroup = group;
    actor.lastLiveObjectId = liveObjectId;
    m_salientActors.push_back(actor);
    return {true, "", ++m_revision};
}

const std::vector<ObjectiveRecord>& ZoneState::Objectives() const
{
    return m_objectives;
}

std::vector<ObjectiveRecord>& ZoneState::MutableObjectives()
{
    return m_objectives;
}

const std::vector<WorldEvent>& ZoneState::Events() const
{
    return m_events;
}

bool ZoneState::IsEventParticipant(Handle id) const
{
    if (id == Handle::Invalid()) return true;
    if (!id.IsValid() || id.Generation() != 1) return false;
    if (FindFaction(id) || FindLocation(id) || FindRoute(id) || FindResource(id) || FindGroup(id)) return true;
    for (const auto& actor : m_salientActors)
        if (actor.id == id) return true;
    return false;
}

ZoneResult ZoneState::AppendEvent(const WorldEvent& event)
{
    if (m_revision == 0 || m_revision == std::numeric_limits<uint64_t>::max() ||
        m_nextEventSeq == std::numeric_limits<uint64_t>::max())
        return {false, "world uninitialized or counter exhausted", m_revision};
    if (!IsValidZoneText(event.kind, 96) || !IsValidZoneText(event.source, 512, true) ||
        !IsEventParticipant(event.subject) || !IsEventParticipant(event.object))
        return {false, "invalid event text or participant", m_revision};
    WorldEvent e = event;
    e.seq = m_nextEventSeq++;
    m_events.push_back(e);
    return {true, "", ++m_revision};
}

void ZoneState::SetRevision(uint64_t revision)
{
    m_revision = revision;
}

namespace
{
// Length-prefix strings so spaces, newlines and separators are unambiguous.
// Classic locale and decimal integers make the encoding independent of process locale.
class ZoneSnapshotWriter
{
public:
    ZoneSnapshotWriter() { out.imbue(std::locale::classic()); }
    void Put(const std::string& value) { out << value.size() << ':' << value << ' '; }
    void Put(const char* value) { Put(std::string(value)); }
    void Put(Handle value) { Put(value.value); }
    template <typename T> void Put(T value) { out << +value << ' '; }
    template <typename... T> void Row(const T&... fields) { (Put(fields), ...); out << '\n'; }
    std::ostringstream out;
};
}

std::string ZoneState::Digest() const
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : SaveSnapshot())
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string ZoneState::SaveSnapshot() const
{
    ZoneSnapshotWriter w;
    w.out << "xrsim_zone_snapshot_v1\n";
    w.Row("state", m_epoch, m_revision, m_nextEventSeq, m_nextHandleIndex);
    w.Row("scenario", m_manifest.scenario, m_manifest.saveName, m_manifest.levelName,
        m_manifest.seed, m_manifest.origin);
    // Retain bootstrap provenance, including the agent/scoped ownership contract.
    w.Row("manifest_anchors", m_manifest.anchors.size());
    for (const auto& r : m_manifest.anchors) w.Row(r.name, r.xCm, r.yCm, r.zCm);
    w.Row("manifest_factions", m_manifest.factions.size());
    for (const auto& r : m_manifest.factions) w.Row(r.name, r.agentId);
    w.Row("manifest_groups", m_manifest.groups.size());
    for (const auto& r : m_manifest.groups)
        w.Row(r.name, r.kind, r.ownerDomain, r.section, r.count, r.anchor, r.agentId);
    w.Row("manifest_resources", m_manifest.resources.size());
    for (const auto& r : m_manifest.resources) w.Row(r.name, r.kind, r.anchor, r.quantity, r.minimum);
    w.Row("manifest_routes", m_manifest.routes.size());
    for (const auto& r : m_manifest.routes) w.Row(r.name, r.from, r.to);
    w.Row("agents", m_manifest.agents.size());
    for (const auto& r : m_manifest.agents) w.Row(r.agentId, r.scopeKind, r.scopeId, r.name, r.charterKey);
    w.Row("factions", m_factions.size());
    for (const auto& r : m_factions) w.Row(r.id, r.name, r.agentId);
    w.Row("locations", m_locations.size());
    for (const auto& r : m_locations)
        w.Row(r.id, r.name, r.position.xCm, r.position.yCm, r.position.zCm, r.position.levelVertex, r.ownerFaction);
    w.Row("routes", m_routes.size());
    for (const auto& r : m_routes) w.Row(r.id, r.name, r.from, r.to, r.open);
    w.Row("groups", m_groups.size());
    for (const auto& r : m_groups)
        w.Row(r.id, r.name, uint32_t(r.kind), r.faction, r.section, r.initialCount, r.aliveCount,
            r.location, r.agentId, r.active);
    w.Row("resources", m_resources.size());
    for (const auto& r : m_resources)
        w.Row(r.id, r.name, r.kind, r.location, r.quantity, r.minimum, r.supply, r.demand, r.maximum);
    w.Row("relations", m_relations.size());
    for (const auto& r : m_relations) w.Row(r.first, r.second, r.score);
    w.Row("objectives", m_objectives.size());
    for (const auto& r : m_objectives)
        w.Row(r.planId, r.scopeKind, r.scopeId, uint32_t(r.subjectKind), r.subject, r.subjectName,
            uint32_t(r.kind), r.target, r.expiresAtTick, uint32_t(r.author), r.active);
    w.Row("incidents", m_incidents.size());
    for (const auto& r : m_incidents) w.Row(r.id, r.location, r.kind, r.magnitude, r.pending);
    w.Row("salient_actors", m_salientActors.size());
    for (const auto& r : m_salientActors) w.Row(r.id, r.parentGroup, r.lastLiveObjectId, r.alive);
    w.Row("events", m_events.size());
    for (const auto& r : m_events) w.Row(r.seq, r.tick, r.kind, r.subject, r.object, r.amount, r.source);
    w.out << "end\n";
    return w.out.str();
}
} // namespace xrSim
