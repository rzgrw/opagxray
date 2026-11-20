#include "xrSim/xrSimFreeplayScheduler.h"

#include <algorithm>
#include <utility>

namespace xrSim
{
FreeplayScheduler::FreeplayScheduler(const FreeplaySchedulerConfig& config) : m_config(config) {}

void FreeplayScheduler::Reset()
{
    m_entries.clear();
    m_consecutiveFailures = 0;
    m_backoffUntilMs = 0;
}

void FreeplayScheduler::Forget(uint64_t ownerId) { m_entries.erase(ownerId); }

void FreeplayScheduler::RequestWake(uint64_t ownerId, const std::string& reason, uint64_t nowMs)
{
    Entry& entry = m_entries[ownerId];
    if (entry.state == FreeplayWakeState::Idle)
    {
        entry.state = FreeplayWakeState::Queued;
        entry.stateSinceMs = nowMs;
        entry.current = Wake();
    }
    // A wake for an owner in flight waits for its decision; the in-flight reasons stay as they were.
    Wake& wake = IsInFlight(entry.state) ? entry.pending : entry.current;
    if (wake.reasons.empty())
        wake.queuedAtMs = nowMs;
    if (std::find(wake.reasons.begin(), wake.reasons.end(), reason) == wake.reasons.end())
        wake.reasons.push_back(reason);
}

std::vector<uint64_t> FreeplayScheduler::TakeObservationRequests(const FreeplayPopulation& population, uint64_t nowMs)
{
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (population.FindOwner(it->first) == nullptr)
            it = m_entries.erase(it);
        else
            ++it;
    }

    for (const FreeplayOwner& owner : population.Owners())
    {
        if (!IsFreeplayTransferable(owner))
            continue;
        const auto found = m_entries.find(owner.ownerId);
        const bool idle = found == m_entries.end() || found->second.state == FreeplayWakeState::Idle;
        const uint64_t idleSinceMs = found == m_entries.end() ? 0 : found->second.idleSinceMs;
        if (idle && nowMs >= idleSinceMs && nowMs - idleSinceMs >= m_config.reconsiderIntervalMs)
            RequestWake(owner.ownerId, "reconsider", nowMs);
    }

    std::vector<std::pair<uint64_t, uint64_t>> queued;   // (queuedAtMs, ownerId)
    for (auto& item : m_entries)
    {
        Entry& entry = item.second;
        if (entry.state != FreeplayWakeState::Queued)
            continue;
        if (!IsFreeplayTransferable(*population.FindOwner(item.first)))
        {
            entry.state = FreeplayWakeState::Idle;
            entry.stateSinceMs = nowMs;
            entry.current = Wake();
            continue;
        }
        queued.emplace_back(entry.current.queuedAtMs, item.first);
    }

    std::vector<uint64_t> taken;
    if (BackoffActive(nowMs))
        return taken;
    std::sort(queued.begin(), queued.end());
    size_t inFlight = InFlight();
    for (const auto& item : queued)
    {
        if (inFlight >= m_config.maxInFlight)
            break;
        Entry& entry = m_entries[item.second];
        if (entry.decided && nowMs < entry.lastDecisionMs + m_config.minWakeIntervalMs)
            continue;
        entry.state = FreeplayWakeState::AwaitingObservation;
        entry.stateSinceMs = nowMs;
        taken.push_back(item.second);
        ++inFlight;
    }
    return taken;
}

std::vector<uint64_t> FreeplayScheduler::TakeTimedOutObservations(uint64_t nowMs)
{
    std::vector<uint64_t> timedOut;
    for (auto& item : m_entries)
    {
        Entry& entry = item.second;
        if (entry.state != FreeplayWakeState::AwaitingObservation || nowMs < entry.stateSinceMs ||
            nowMs - entry.stateSinceMs < m_config.observationTimeoutMs)
            continue;
        FinishFlight(entry, nowMs);
        timedOut.push_back(item.first);
    }
    return timedOut;
}

void FreeplayScheduler::OnObservation(uint64_t ownerId, uint64_t nowMs)
{
    const auto found = m_entries.find(ownerId);
    if (found == m_entries.end() || found->second.state != FreeplayWakeState::AwaitingObservation)
        return;
    found->second.state = FreeplayWakeState::AwaitingDecision;
    found->second.stateSinceMs = nowMs;
}

void FreeplayScheduler::OnDecision(uint64_t ownerId, bool providerFailed, uint64_t nowMs)
{
    const auto found = m_entries.find(ownerId);
    if (found == m_entries.end() || !IsInFlight(found->second.state))
        return;
    Entry& entry = found->second;
    entry.decided = true;
    entry.lastDecisionMs = nowMs;
    FinishFlight(entry, nowMs);

    if (!providerFailed)
    {
        m_consecutiveFailures = 0;
        m_backoffUntilMs = 0;
        return;
    }
    ++m_consecutiveFailures;
    uint64_t backoffMs = m_config.failureBackoffMs;
    for (uint32_t i = 1; i < m_consecutiveFailures && backoffMs < m_config.maxFailureBackoffMs; ++i)
        backoffMs *= 2;
    backoffMs = std::min<uint64_t>(backoffMs, m_config.maxFailureBackoffMs);
    m_backoffUntilMs = nowMs + backoffMs;
}

bool FreeplayScheduler::BackoffActive(uint64_t nowMs) const { return nowMs < m_backoffUntilMs; }

FreeplayWakeState FreeplayScheduler::State(uint64_t ownerId) const
{
    const auto found = m_entries.find(ownerId);
    return found == m_entries.end() ? FreeplayWakeState::Idle : found->second.state;
}

std::string FreeplayScheduler::Reasons(uint64_t ownerId) const
{
    const auto found = m_entries.find(ownerId);
    if (found == m_entries.end())
        return std::string();
    std::string joined;
    for (const std::string& reason : found->second.current.reasons)
    {
        if (!joined.empty())
            joined += ',';
        joined += reason;
    }
    return joined;
}

uint64_t FreeplayScheduler::QueuedAtMs(uint64_t ownerId) const
{
    const auto found = m_entries.find(ownerId);
    return found == m_entries.end() || found->second.state == FreeplayWakeState::Idle ?
        0 : found->second.current.queuedAtMs;
}

size_t FreeplayScheduler::InFlight() const
{
    size_t count = 0;
    for (const auto& item : m_entries)
        if (IsInFlight(item.second.state))
            ++count;
    return count;
}

bool FreeplayScheduler::IsInFlight(FreeplayWakeState state)
{
    return state == FreeplayWakeState::AwaitingObservation || state == FreeplayWakeState::AwaitingDecision;
}

// Ends a flight: a wake requested meanwhile queues the owner again, otherwise it goes idle.
void FreeplayScheduler::FinishFlight(Entry& entry, uint64_t nowMs)
{
    entry.stateSinceMs = nowMs;
    entry.current = std::move(entry.pending);
    entry.pending = Wake();
    if (entry.current.reasons.empty())
    {
        entry.state = FreeplayWakeState::Idle;
        entry.idleSinceMs = nowMs;
    }
    else
        entry.state = FreeplayWakeState::Queued;
}
} // namespace xrSim
