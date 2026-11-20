#pragma once

#include "xrSim/xrSimActorIntent.h"
#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimFreeplay.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace xrSim
{
struct FreeplaySite { std::string name; uint32_t distanceMeters = 0; std::string kind; uint32_t occupants = 0; };
struct FreeplaySighting { std::string name, species, faction, relation; uint32_t count = 0; uint32_t distanceMeters = 0; };

// What one owner can see when it wakes: the sites it may choose between and the groups nearby.
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
// Checks a provider plan against the live registry and the observation it answered. Never mutates.
FreeplayVerdict ValidateFreeplayDecision(const FreeplayPopulation& population,
    const FreeplayObservation& observation, const ActorIntentPlan& plan);

// Bounded log of every freeplay decision outcome, newest last.
class FreeplayLedger
{
public:
    void Append(FreeplayLedgerEntry entry);          // assigns serial; keeps the newest 512
    std::string Format(size_t maxEntries) const;     // one line per entry, key=value fields named as the struct members
    size_t Size() const;

private:
    std::deque<FreeplayLedgerEntry> m_entries;
    uint64_t m_nextSerial = 1;
};
} // namespace xrSim
