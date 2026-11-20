#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class FreeplaySpecies { Stalker, Mutant };
enum class FreeplayOwnerKind { Squad, Pack, LoneStalker, LoneMutant };
enum class FreeplayBinding { Group, Npc };   // squad server object, or an NPC with no squad
enum class FreeplayWriter { Sim, Script, Random, Companion, Hostage, None };
enum class FreeplayObjectiveSource { None, Bootstrap, Inherited, Recorded, Live, Hold };

struct FreeplayMember
{
    uint16_t objectId = 0;
    std::string section;
    bool leader = false;
};

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

struct FreeplayResult
{
    bool ok = false;
    std::string reason;
};

const char* FreeplayKindName(FreeplayOwnerKind kind);
const char* FreeplayWriterName(FreeplayWriter writer);
const char* FreeplaySourceName(FreeplayObjectiveSource source);
bool IsFreeplayTransferable(const FreeplayOwner& owner);

// Registry of AI decision owners for the living freeplay population. Every stalker and
// mutant belongs to exactly one owner: its squad server object, or itself when unbound.
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

private:
    FreeplayOwner* FindMutableOwner(uint64_t ownerId);

    uint32_t m_epoch = 0;
    uint64_t m_nextOwnerId = 1;
    std::vector<FreeplayOwner> m_owners;
    std::vector<uint64_t> m_changed;
};
} // namespace xrSim
