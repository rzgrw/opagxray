#pragma once

#include "xrSim/xrSimFreeplay.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xrSim
{
// Starting values; Task 7 measures and may change them.
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

// Decides which freeplay owners wake next: coalesces wake requests per owner, hands them out
// oldest first under a concurrency bound, and backs off after provider failures. All time
// comes in through nowMs.
class FreeplayScheduler
{
public:
    explicit FreeplayScheduler(const FreeplaySchedulerConfig& config = {});
    void Reset();
    void SetConfig(const FreeplaySchedulerConfig& config);   // owners and their state are kept
    void Forget(uint64_t ownerId);
    void RequestWake(uint64_t ownerId, const std::string& reason, uint64_t nowMs);
    // Hands out at most maxNew queued owners (the call budget); the rest keep their place.
    std::vector<uint64_t> TakeObservationRequests(
        const FreeplayPopulation& population, uint64_t nowMs, size_t maxNew = SIZE_MAX);
    std::vector<uint64_t> TakeTimedOutObservations(uint64_t nowMs);   // AwaitingObservation older than the timeout -> Idle
    void OnObservation(uint64_t ownerId, uint64_t nowMs);
    void OnDecision(uint64_t ownerId, bool providerFailed, uint64_t nowMs);
    bool BackoffActive(uint64_t nowMs) const;
    FreeplayWakeState State(uint64_t ownerId) const;
    std::string Reasons(uint64_t ownerId) const;      // distinct reasons joined by ','
    uint64_t QueuedAtMs(uint64_t ownerId) const;
    size_t InFlight() const;

private:
    struct Wake
    {
        std::vector<std::string> reasons;   // distinct, in arrival order
        uint64_t queuedAtMs = 0;
    };

    struct Entry
    {
        FreeplayWakeState state = FreeplayWakeState::Idle;
        Wake current;    // the wake being handed out or in flight
        Wake pending;    // wakes requested while in flight; queued after the decision
        uint64_t stateSinceMs = 0;
        uint64_t idleSinceMs = 0;   // an owner never seen counts as idle since 0
        bool decided = false;
        uint64_t lastDecisionMs = 0;
    };

    static bool IsInFlight(FreeplayWakeState state);
    void FinishFlight(Entry& entry, uint64_t nowMs);

    FreeplaySchedulerConfig m_config;
    std::map<uint64_t, Entry> m_entries;
    uint32_t m_consecutiveFailures = 0;
    uint64_t m_backoffUntilMs = 0;
};
} // namespace xrSim
