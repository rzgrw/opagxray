#include "xrSim/xrSimFreeplay.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace xrSim
{
namespace
{
const char* kSnapshotHeader = "xrsim_freeplay_sync_v1";
constexpr size_t kMaxGroupMembers = 64;
constexpr size_t kMaxGroups = 4096;
constexpr size_t kMaxMemories = 8;

struct StagedOwner
{
    FreeplayBinding binding = FreeplayBinding::Group;
    uint16_t bindingId = 0;
    std::string name, faction, level, site, target;
    FreeplaySpecies species = FreeplaySpecies::Stalker;
    FreeplayWriter writer = FreeplayWriter::Sim;
    bool online = false;
    std::vector<FreeplayMember> members;
};

struct StagedMember
{
    uint16_t groupId = 0;
    FreeplayMember member;
};

FreeplayResult Fail(const std::string& reason)
{
    FreeplayResult result;
    result.reason = reason;
    return result;
}

FreeplayResult Succeed()
{
    FreeplayResult result;
    result.ok = true;
    return result;
}

bool IsSeparator(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// One field of the snapshot or census: non-empty, no whitespace, no record separators.
bool IsToken(const std::string& text)
{
    if (text.empty())
        return false;
    for (const char c : text)
    {
        if (static_cast<unsigned char>(c) <= ' ' || c == ';' || c == 0x7f)
            return false;
    }
    return true;
}

std::vector<std::vector<std::string>> SplitRecords(const std::string& snapshot)
{
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> tokens;
    std::string token;
    for (size_t i = 0; i <= snapshot.size(); ++i)
    {
        const char c = i < snapshot.size() ? snapshot[i] : '\n';
        if (c == '\n' || c == ';' || IsSeparator(c))
        {
            if (!token.empty())
                tokens.push_back(token);
            token.clear();
            if ((c == '\n' || c == ';') && !tokens.empty())
            {
                records.push_back(tokens);
                tokens.clear();
            }
            continue;
        }
        token += c;
    }
    return records;
}

bool ParseId(const std::string& text, uint16_t& value)
{
    if (text.empty() || text.size() > 5)
        return false;
    uint32_t parsed = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        parsed = parsed * 10 + static_cast<uint32_t>(c - '0');
    }
    if (parsed > 0xffff)
        return false;
    value = static_cast<uint16_t>(parsed);
    return true;
}

bool ParseFlag(const std::string& text, bool& value)
{
    if (text != "0" && text != "1")
        return false;
    value = text == "1";
    return true;
}

bool ParseSpecies(const std::string& text, FreeplaySpecies& species)
{
    if (text == "stalker")
        species = FreeplaySpecies::Stalker;
    else if (text == "mutant")
        species = FreeplaySpecies::Mutant;
    else
        return false;
    return true;
}

bool ParseWriter(const std::string& text, FreeplayWriter& writer)
{
    const FreeplayWriter all[] = {FreeplayWriter::Sim, FreeplayWriter::Script, FreeplayWriter::Random,
        FreeplayWriter::Companion, FreeplayWriter::Hostage, FreeplayWriter::None};
    for (const FreeplayWriter candidate : all)
    {
        if (text == FreeplayWriterName(candidate))
        {
            writer = candidate;
            return true;
        }
    }
    return false;
}

bool IsObjectiveVerb(const std::string& verb)
{
    return verb == "hold" || verb == "stay" || verb == "move_to" || verb == "retreat";
}

FreeplayOwnerKind KindFor(FreeplaySpecies species, size_t memberCount)
{
    if (species == FreeplaySpecies::Mutant)
        return memberCount > 1 ? FreeplayOwnerKind::Pack : FreeplayOwnerKind::LoneMutant;
    return memberCount > 1 ? FreeplayOwnerKind::Squad : FreeplayOwnerKind::LoneStalker;
}

std::vector<uint16_t> MemberIds(const std::vector<FreeplayMember>& members)
{
    std::vector<uint16_t> ids;
    ids.reserve(members.size());
    for (const FreeplayMember& member : members)
        ids.push_back(member.objectId);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string RecordName(const std::vector<std::string>& record) { return record.empty() ? "" : record[0]; }

// Parses and validates the whole snapshot without touching the registry.
FreeplayResult ParseSnapshot(const std::string& snapshot, std::vector<StagedOwner>& owners)
{
    const std::vector<std::vector<std::string>> records = SplitRecords(snapshot);
    if (records.empty() || records[0].size() != 1 || records[0][0] != kSnapshotHeader)
        return Fail("missing header " + std::string(kSnapshotHeader));

    std::map<uint16_t, size_t> groupIndex;
    std::set<uint16_t> npcBindings;
    std::vector<StagedMember> members;
    size_t groupCount = 0;
    bool ended = false;
    for (size_t i = 1; i < records.size(); ++i)
    {
        const std::vector<std::string>& record = records[i];
        const std::string& type = record[0];
        if (ended)
            return Fail("record after end: " + type);
        if (type == "end")
        {
            if (record.size() != 1)
                return Fail("end takes no fields");
            ended = true;
        }
        else if (type == "group")
        {
            if (record.size() != 10)
                return Fail("group needs 9 fields");
            StagedOwner owner;
            owner.binding = FreeplayBinding::Group;
            if (!ParseId(record[1], owner.bindingId) || !ParseFlag(record[9], owner.online))
                return Fail("bad number in group " + record[1]);
            if (!ParseSpecies(record[3], owner.species))
                return Fail("unknown species: " + record[3]);
            if (!ParseWriter(record[8], owner.writer))
                return Fail("unknown writer: " + record[8]);
            if (groupIndex.count(owner.bindingId) != 0)
                return Fail("duplicate group id " + record[1]);
            if (++groupCount > kMaxGroups)
                return Fail("more than 4096 groups");
            owner.name = record[2];
            owner.faction = record[4];
            owner.level = record[5];
            owner.site = record[6];
            owner.target = record[7];
            groupIndex[owner.bindingId] = owners.size();
            owners.push_back(owner);
        }
        else if (type == "member")
        {
            if (record.size() != 5)
                return Fail("member needs 4 fields");
            StagedMember staged;
            if (!ParseId(record[1], staged.groupId) || !ParseId(record[2], staged.member.objectId) ||
                !ParseFlag(record[4], staged.member.leader))
                return Fail("bad number in member " + record[2]);
            staged.member.section = record[3];
            members.push_back(staged);
        }
        else if (type == "npc")
        {
            if (record.size() != 6)
                return Fail("npc needs 5 fields");
            StagedOwner owner;
            owner.binding = FreeplayBinding::Npc;
            if (!ParseId(record[1], owner.bindingId))
                return Fail("bad number in npc " + record[1]);
            if (!ParseSpecies(record[3], owner.species))
                return Fail("unknown species: " + record[3]);
            if (!npcBindings.insert(owner.bindingId).second)
                return Fail("duplicate object id " + record[1]);
            owner.name = record[2];
            owner.faction = record[4];
            owner.level = record[5];
            owner.site = "-";
            owner.target = "-";
            owner.writer = FreeplayWriter::None;
            FreeplayMember self;
            self.objectId = owner.bindingId;
            self.section = record[2];
            self.leader = true;
            owner.members.push_back(self);
            owners.push_back(owner);
        }
        else
            return Fail("unknown record: " + RecordName(record));
    }
    if (!ended)
        return Fail("missing end");

    std::set<uint16_t> objectIds = npcBindings;
    for (const StagedMember& staged : members)
    {
        const auto group = groupIndex.find(staged.groupId);
        if (group == groupIndex.end())
            return Fail("member " + std::to_string(staged.member.objectId) + " names unknown group " +
                std::to_string(staged.groupId));
        if (!objectIds.insert(staged.member.objectId).second)
            return Fail("duplicate object id " + std::to_string(staged.member.objectId));
        std::vector<FreeplayMember>& groupMembers = owners[group->second].members;
        if (groupMembers.size() >= kMaxGroupMembers)
            return Fail("more than 64 members in group " + std::to_string(staged.groupId));
        groupMembers.push_back(staged.member);
    }
    for (const StagedOwner& owner : owners)
    {
        if (owner.members.empty())
            return Fail("group " + std::to_string(owner.bindingId) + " has no members");
    }
    return Succeed();
}

using BindingKey = std::pair<FreeplayBinding, uint16_t>;

// A binding still in the snapshot holds a new occupant when nothing ties it to the previous
// one: a group whose member set shares no object ID, or an npc whose section changed.
// Reused server IDs must never inherit the previous occupant's mind.
bool IsNewOccupant(const FreeplayOwner& previous, const StagedOwner& staged)
{
    if (previous.binding == FreeplayBinding::Npc)
        return previous.members.empty() || staged.members.empty() ||
            previous.members[0].section != staged.members[0].section;
    for (const FreeplayMember& before : previous.members)
    {
        for (const FreeplayMember& after : staged.members)
        {
            if (before.objectId == after.objectId)
                return false;
        }
    }
    return true;
}

// A new group whose members all belonged to one other owner before this sync is a split of it.
// The previous occupant of the group's own binding never counts as that owner.
const FreeplayOwner* SplitSource(const StagedOwner& staged, const std::vector<FreeplayOwner>& previous,
    const std::map<uint16_t, size_t>& previousOwnerOfNpc)
{
    const FreeplayOwner* source = nullptr;
    for (const FreeplayMember& member : staged.members)
    {
        const auto found = previousOwnerOfNpc.find(member.objectId);
        if (found == previousOwnerOfNpc.end())
            return nullptr;
        const FreeplayOwner* owner = &previous[found->second];
        if (source != nullptr && source != owner)
            return nullptr;
        source = owner;
    }
    if (source != nullptr && source->binding == staged.binding && source->bindingId == staged.bindingId)
        return nullptr;
    return source;
}

void CopyDescription(const StagedOwner& staged, FreeplayOwner& owner)
{
    owner.name = staged.name;
    owner.faction = staged.faction;
    owner.level = staged.level;
    owner.site = staged.site;
    owner.species = staged.species;
    owner.writer = staged.writer;
    owner.online = staged.online;
    owner.kind = KindFor(staged.species, staged.members.size());
}

void AddChanged(std::vector<uint64_t>& changed, uint64_t ownerId)
{
    if (std::find(changed.begin(), changed.end(), ownerId) == changed.end())
        changed.push_back(ownerId);
}
} // namespace

const char* FreeplayKindName(FreeplayOwnerKind kind)
{
    switch (kind)
    {
    case FreeplayOwnerKind::Squad: return "squad";
    case FreeplayOwnerKind::Pack: return "pack";
    case FreeplayOwnerKind::LoneStalker: return "lone_stalker";
    case FreeplayOwnerKind::LoneMutant: return "lone_mutant";
    }
    return "unknown";
}

const char* FreeplayWriterName(FreeplayWriter writer)
{
    switch (writer)
    {
    case FreeplayWriter::Sim: return "sim";
    case FreeplayWriter::Script: return "script";
    case FreeplayWriter::Random: return "random";
    case FreeplayWriter::Companion: return "companion";
    case FreeplayWriter::Hostage: return "hostage";
    case FreeplayWriter::None: return "none";
    }
    return "unknown";
}

const char* FreeplaySourceName(FreeplayObjectiveSource source)
{
    switch (source)
    {
    case FreeplayObjectiveSource::None: return "none";
    case FreeplayObjectiveSource::Bootstrap: return "bootstrap";
    case FreeplayObjectiveSource::Inherited: return "inherited";
    case FreeplayObjectiveSource::Recorded: return "recorded";
    case FreeplayObjectiveSource::Live: return "live";
    case FreeplayObjectiveSource::Hold: return "hold";
    }
    return "unknown";
}

bool IsFreeplayTransferable(const FreeplayOwner& owner)
{
    return owner.binding == FreeplayBinding::Group && owner.writer == FreeplayWriter::Sim;
}

uint32_t FreeplayPopulation::BeginWorld()
{
    ++m_epoch;
    m_owners.clear();
    m_changed.clear();
    return m_epoch;
}

uint32_t FreeplayPopulation::Epoch() const { return m_epoch; }

FreeplayResult FreeplayPopulation::Sync(const std::string& snapshot)
{
    std::vector<StagedOwner> staged;
    const FreeplayResult parsed = ParseSnapshot(snapshot, staged);
    if (!parsed.ok)
        return parsed;

    std::map<uint16_t, size_t> previousOwnerOfNpc;
    for (size_t i = 0; i < m_owners.size(); ++i)
    {
        for (const FreeplayMember& member : m_owners[i].members)
            previousOwnerOfNpc[member.objectId] = i;
    }

    std::map<BindingKey, size_t> stagedByBinding;
    for (size_t i = 0; i < staged.size(); ++i)
        stagedByBinding[BindingKey(staged[i].binding, staged[i].bindingId)] = i;

    std::vector<FreeplayOwner> owners;
    std::vector<uint64_t> changed = m_changed;
    uint64_t nextOwnerId = m_nextOwnerId;
    std::set<BindingKey> kept;
    for (const FreeplayOwner& previous : m_owners)
    {
        const BindingKey key(previous.binding, previous.bindingId);
        const auto found = stagedByBinding.find(key);
        if (found == stagedByBinding.end())
            continue;   // absent from the living population: retired
        const StagedOwner* source = &staged[found->second];
        if (IsNewOccupant(previous, *source))
            continue;   // reused binding: retire the previous occupant, enrol the new one below
        kept.insert(key);
        FreeplayOwner owner = previous;
        CopyDescription(*source, owner);
        if (MemberIds(owner.members) != MemberIds(source->members))
        {
            ++owner.membershipRevision;
            AddChanged(changed, owner.ownerId);
        }
        owner.members = source->members;
        owners.push_back(owner);
    }

    for (const StagedOwner& source : staged)
    {
        if (kept.count(BindingKey(source.binding, source.bindingId)) != 0)
            continue;
        FreeplayOwner owner;
        owner.ownerId = nextOwnerId++;
        owner.binding = source.binding;
        owner.bindingId = source.bindingId;
        CopyDescription(source, owner);
        owner.members = source.members;
        if (source.binding == FreeplayBinding::Group)
        {
            const FreeplayOwner* parent = SplitSource(source, m_owners, previousOwnerOfNpc);
            if (parent != nullptr)
            {
                owner.objective.verb = parent->objective.verb;
                owner.objective.site = parent->objective.site;
                owner.objective.source = FreeplayObjectiveSource::Inherited;
                owner.objective.revision = 1;
            }
            else if (source.target != "-")
            {
                owner.objective.verb = source.target == source.site ? "stay" : "move_to";
                owner.objective.site = source.target;
                owner.objective.source = FreeplayObjectiveSource::Bootstrap;
                owner.objective.revision = 1;
            }
        }
        AddChanged(changed, owner.ownerId);
        owners.push_back(owner);
    }

    m_owners.swap(owners);
    m_changed.swap(changed);
    m_nextOwnerId = nextOwnerId;
    return Succeed();
}

FreeplayResult FreeplayPopulation::Retire(FreeplayBinding binding, uint16_t bindingId)
{
    for (auto it = m_owners.begin(); it != m_owners.end(); ++it)
    {
        if (it->binding == binding && it->bindingId == bindingId)
        {
            m_owners.erase(it);
            return Succeed();
        }
    }
    return Fail("no owner bound to " + std::string(binding == FreeplayBinding::Group ? "group:" : "npc:") +
        std::to_string(bindingId));
}

FreeplayResult FreeplayPopulation::SetObjective(uint64_t ownerId, const std::string& verb, const std::string& site,
    FreeplayObjectiveSource source)
{
    FreeplayOwner* owner = FindMutableOwner(ownerId);
    if (owner == nullptr)
        return Fail("unknown owner " + std::to_string(ownerId));
    if (!IsObjectiveVerb(verb))
        return Fail("unknown objective verb: " + verb);
    if (!IsToken(site))
        return Fail("objective site must be one token");
    owner->objective.verb = verb;
    owner->objective.site = site;
    owner->objective.source = source;
    ++owner->objective.revision;
    return Succeed();
}

FreeplayResult FreeplayPopulation::AddMemory(uint64_t ownerId, const std::string& token)
{
    FreeplayOwner* owner = FindMutableOwner(ownerId);
    if (owner == nullptr)
        return Fail("unknown owner " + std::to_string(ownerId));
    if (!IsToken(token))
        return Fail("memory must be one token");
    owner->memories.push_back(token);
    if (owner->memories.size() > kMaxMemories)
        owner->memories.erase(owner->memories.begin(), owner->memories.end() - kMaxMemories);
    return Succeed();
}

const FreeplayOwner* FreeplayPopulation::FindOwner(uint64_t ownerId) const
{
    for (const FreeplayOwner& owner : m_owners)
    {
        if (owner.ownerId == ownerId)
            return &owner;
    }
    return nullptr;
}

FreeplayOwner* FreeplayPopulation::FindMutableOwner(uint64_t ownerId)
{
    return const_cast<FreeplayOwner*>(FindOwner(ownerId));
}

const FreeplayOwner* FreeplayPopulation::FindByBinding(FreeplayBinding binding, uint16_t bindingId) const
{
    for (const FreeplayOwner& owner : m_owners)
    {
        if (owner.binding == binding && owner.bindingId == bindingId)
            return &owner;
    }
    return nullptr;
}

const FreeplayOwner* FreeplayPopulation::OwnerOfNpc(uint16_t objectId) const
{
    for (const FreeplayOwner& owner : m_owners)
    {
        for (const FreeplayMember& member : owner.members)
        {
            if (member.objectId == objectId)
                return &owner;
        }
    }
    return nullptr;
}

const std::vector<FreeplayOwner>& FreeplayPopulation::Owners() const { return m_owners; }

std::vector<uint64_t> FreeplayPopulation::TakeChangedOwners()
{
    std::vector<uint64_t> changed;
    for (const uint64_t ownerId : m_changed)
    {
        if (FindOwner(ownerId) != nullptr)
            changed.push_back(ownerId);
    }
    m_changed.clear();
    return changed;
}

std::string FreeplayPopulation::FormatCensus(const std::string& levelFilter) const
{
    std::string out;
    size_t npcs = 0, owners = 0, transferable = 0, scriptOwned = 0, unbound = 0;
    for (const FreeplayOwner& owner : m_owners)
    {
        if (!levelFilter.empty() && owner.level != levelFilter)
            continue;
        ++owners;
        if (IsFreeplayTransferable(owner))
            ++transferable;
        if (owner.binding == FreeplayBinding::Group && owner.writer != FreeplayWriter::Sim)
            ++scriptOwned;
        if (owner.binding == FreeplayBinding::Npc)
            ++unbound;
        const std::string binding = owner.binding == FreeplayBinding::Group ? "group" : "npc";
        for (const FreeplayMember& member : owner.members)
        {
            ++npcs;
            out += "npc " + std::to_string(member.objectId) + " " + member.section +
                " owner=" + std::to_string(owner.ownerId) + " kind=" + FreeplayKindName(owner.kind) +
                " binding=" + binding + ":" + std::to_string(owner.bindingId) +
                " writer=" + FreeplayWriterName(owner.writer) + " level=" + owner.level +
                " objective=" + owner.objective.verb + ":" + owner.objective.site +
                " source=" + FreeplaySourceName(owner.objective.source) +
                " rev=" + std::to_string(owner.objective.revision) + "\n";
        }
    }
    out += "summary npcs=" + std::to_string(npcs) + " owners=" + std::to_string(owners) +
        " transferable=" + std::to_string(transferable) + " script_owned=" + std::to_string(scriptOwned) +
        " unbound=" + std::to_string(unbound) + "\n";
    return out;
}
} // namespace xrSim
