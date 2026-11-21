#include "xrSim/xrSimFreeplayBridge.h"

#include "xrSim/xrSimActorProvider.h"
#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimAnthropicTransport.h"
#include "xrSim/xrSimFreeplay.h"
#include "xrSim/xrSimFreeplayDecision.h"
#include "xrSim/xrSimFreeplayScheduler.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace xrSim
{
namespace
{
enum class FreeplayMode { Off, Recorded, Live };

// The wake an owner was handed out with; kept until its observation arrives or times out.
struct HandedOutWake
{
    std::string reasons;
    uint64_t queuedAtMs = 0;
    uint64_t handedOutAtMs = 0;
};

struct StoredObservation
{
    FreeplayObservation observation;
    std::string reasons;
    uint64_t queuedAtMs = 0;
    uint64_t observedAtMs = 0;
};

struct PendingRequest
{
    uint64_t ownerId = 0;
    uint32_t epoch = 0;   // world the request was made in; a reply from an older world is stale_world
    StoredObservation stored;
};

struct FreeplayRuntime
{
    FreeplayMode mode = FreeplayMode::Off;
    uint64_t nowMs = 0;   // now_ms of the most recent tick
    FreeplayPopulation population;
    FreeplayScheduler scheduler;
    FreeplayLedger ledger;
    std::map<uint64_t, HandedOutWake> handedOut;
    std::map<uint64_t, StoredObservation> observations;
    std::map<uint64_t, PendingRequest> pending;   // by provider request id
    std::vector<uint64_t> abandoned;              // requests of retired owners: drained, then dropped
    std::set<uint64_t> knownOwners;               // owners that already had their spawn wake
    std::map<uint64_t, uint32_t> appliedRevision; // objective revision of the last apply line per owner
    std::map<uint64_t, FreeplayObjective> previousObjective; // in force before the owner's last committed decision
};

FreeplayRuntime g_freeplay;
std::unique_ptr<AsyncActorProviderQueue> g_freeplayQueue;
bool g_freeplayTestProvider = false;

std::string Fail(bool& ok, const std::string& reason)
{
    ok = false;
    return reason;
}

std::string Succeed(bool& ok, const std::string& text)
{
    ok = true;
    return text;
}

std::vector<std::string> Tokens(const std::string& text)
{
    std::istringstream in(text);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token)
        tokens.push_back(token);
    return tokens;
}

bool ParseU64(const std::string& text, uint64_t maxValue, uint64_t& value)
{
    if (text.empty() || text.size() > 20)
        return false;
    uint64_t parsed = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (maxValue - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool ParseGroupId(const std::string& text, uint16_t& groupId)
{
    uint64_t parsed = 0;
    if (!ParseU64(text, 0xffffull, parsed))
        return false;
    groupId = static_cast<uint16_t>(parsed);
    return true;
}

uint64_t Elapsed(uint64_t fromMs, uint64_t toMs) { return toMs >= fromMs ? toMs - fromMs : 0; }

const char* ModeName(FreeplayMode mode)
{
    switch (mode)
    {
    case FreeplayMode::Recorded: return "recorded";
    case FreeplayMode::Live: return "live";
    default: return "off";
    }
}

const char* WakeStateName(FreeplayWakeState state)
{
    switch (state)
    {
    case FreeplayWakeState::Queued: return "queued";
    case FreeplayWakeState::AwaitingObservation: return "awaiting_observation";
    case FreeplayWakeState::AwaitingDecision: return "awaiting_decision";
    default: return "idle";
    }
}

std::string BindingText(const FreeplayOwner& owner)
{
    return std::string(owner.binding == FreeplayBinding::Group ? "group:" : "npc:") + std::to_string(owner.bindingId);
}

AgentProviderConfig LiveConfig() { return LoadAgentProviderConfigFromEnvironment(); }

// Empty when live mode may start. The model must be named in XRAY_AGENT_MODEL: the provider's built-in
// default model ID is a historical value that needs verification before any live use.
std::string LiveModeRefusal()
{
    if (g_freeplayTestProvider)
        return std::string();
    const AgentProviderConfig config = LiveConfig();
    if (!config.enabled || config.provider != "anthropic")
        return "provider_disabled";
    if (config.apiKey.empty())
        return "missing_api_key";
    const char* model = std::getenv("XRAY_AGENT_MODEL");
    if (model == nullptr || *model == '\0')
        return "missing_model";
    if (!IsAnthropicHttpTransportAvailable())
        return "transport_unavailable";
    return std::string();
}

AsyncActorProviderQueue& EnsureFreeplayQueue()
{
    if (!g_freeplayQueue)
        g_freeplayQueue = std::make_unique<AsyncActorProviderQueue>(CreateLiveActorIntentProvider(LiveConfig()));
    return *g_freeplayQueue;
}

FreeplayLedgerEntry DecisionEntry(uint64_t ownerId, const StoredObservation& stored, const char* source)
{
    FreeplayLedgerEntry entry;
    entry.ownerId = ownerId;
    entry.epoch = stored.observation.epoch;
    entry.membershipRevision = stored.observation.membershipRevision;
    entry.wakeReason = stored.reasons;
    entry.source = source;
    entry.queueAgeMs = Elapsed(stored.queuedAtMs, stored.observedAtMs);
    entry.latencyMs = Elapsed(stored.observedAtMs, g_freeplay.nowMs);
    return entry;
}

void StoreMemories(uint64_t ownerId, const FreeplayVerdict& verdict)
{
    // Memories are single tokens from the intent parser; AddMemory drops anything else.
    for (const std::string& memory : verdict.memories)
        g_freeplay.population.AddMemory(ownerId, memory);
}

// Commits a verdict and returns the ledger verdict: accepted, kept, or why nothing changed. A verdict
// equal to the owner's current objective (same verb and site) is kept: no new revision, so the adapter
// sees no new objective, and memories are still stored.
std::string ApplyVerdict(uint64_t ownerId, const FreeplayVerdict& verdict, FreeplayObjectiveSource source)
{
    if (!verdict.ok)
        return verdict.reason;
    if (verdict.keep)
        return "kept";
    const FreeplayOwner* owner = g_freeplay.population.FindOwner(ownerId);
    const FreeplayObjective previous = owner != nullptr ? owner->objective : FreeplayObjective();
    if (owner != nullptr && previous.verb == verdict.verb && previous.site == verdict.site)
    {
        StoreMemories(ownerId, verdict);
        return "kept";
    }
    const FreeplayResult set = g_freeplay.population.SetObjective(ownerId, verdict.verb, verdict.site, source);
    if (!set.ok)
        return set.reason;
    g_freeplay.previousObjective[ownerId] = previous;
    StoreMemories(ownerId, verdict);
    return "accepted";
}

void SettleReply(const PendingRequest& request, const ActorWakePollResult& polled)
{
    const ActorProviderResult& result = polled.providerResult;
    FreeplayLedgerEntry entry = DecisionEntry(request.ownerId, request.stored, "live");
    entry.provider = result.provider;
    entry.model = result.model;
    if (request.epoch != g_freeplay.population.Epoch())
    {
        // The owner belonged to a world dropped by begin; the scheduler was reset with it. Whatever the
        // reply (success or failure), it has no scheduler effect.
        entry.verdict = "stale_world";
        g_freeplay.ledger.Append(entry);
        return;
    }
    // Failures, as xrSimActorProvider.cpp represents them:
    // * the queue itself failed (Poll not ok): a hold, with backoff;
    // * a coast with a reason comes from the provider (disabled, unsupported provider, missing key,
    //   adapter not linked, transport error, HTTP status), not from the model: a hold, with backoff;
    // * otherwise ok=false is a reply whose body or text did not decode or parse: the model
    //   answered, badly. It is a live decision that changes nothing (malformed:<reason>), and only this
    //   owner waits its minimum wake interval: no global backoff.
    // Only a reasonless coast is the model choosing to keep its objective.
    if (!polled.ok || !result.coastReason.empty())
    {
        entry.source = "hold";
        if (!polled.ok)
            entry.verdict = polled.reason;
        else
            entry.verdict = result.error.empty() ? result.coastReason : result.coastReason + ":" + result.error;
        g_freeplay.ledger.Append(entry);
        g_freeplay.scheduler.OnDecision(request.ownerId, true, g_freeplay.nowMs);
        return;
    }
    if (!result.ok)
    {
        entry.verdict = "malformed:" + (result.error.empty() ? std::string("provider_error") : result.error);
        g_freeplay.ledger.Append(entry);
        g_freeplay.scheduler.OnDecision(request.ownerId, false, g_freeplay.nowMs);
        return;
    }
    const FreeplayVerdict verdict =
        ValidateFreeplayDecision(g_freeplay.population, request.stored.observation, result.plan);
    entry.verb = verdict.verb;
    entry.site = verdict.site;
    entry.verdict = ApplyVerdict(request.ownerId, verdict, FreeplayObjectiveSource::Live);
    g_freeplay.ledger.Append(entry);
    g_freeplay.scheduler.OnDecision(request.ownerId, false, g_freeplay.nowMs);
}

void PollPendingRequests()
{
    if (!g_freeplayQueue)
        return;
    for (auto it = g_freeplay.abandoned.begin(); it != g_freeplay.abandoned.end();)
    {
        const ActorWakePollResult polled = g_freeplayQueue->Poll(*it);
        if (polled.ok && !polled.ready)
            ++it;
        else
            it = g_freeplay.abandoned.erase(it);
    }
    for (auto it = g_freeplay.pending.begin(); it != g_freeplay.pending.end();)
    {
        const ActorWakePollResult polled = g_freeplayQueue->Poll(it->first);
        if (polled.ok && !polled.ready)
        {
            ++it;
            continue;
        }
        const PendingRequest request = std::move(it->second);
        it = g_freeplay.pending.erase(it);
        SettleReply(request, polled);
    }
}

template <typename Map>
void EraseRetired(Map& map)
{
    for (auto it = map.begin(); it != map.end();)
    {
        if (g_freeplay.population.FindOwner(it->first) == nullptr)
            it = map.erase(it);
        else
            ++it;
    }
}

void ForgetRetiredOwners()
{
    EraseRetired(g_freeplay.handedOut);
    EraseRetired(g_freeplay.observations);
    EraseRetired(g_freeplay.appliedRevision);
    EraseRetired(g_freeplay.previousObjective);
    for (auto it = g_freeplay.knownOwners.begin(); it != g_freeplay.knownOwners.end();)
    {
        if (g_freeplay.population.FindOwner(*it) == nullptr)
            it = g_freeplay.knownOwners.erase(it);
        else
            ++it;
    }
}

std::string Mode(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    if (tokens.size() > 1)
        return Fail(ok, "usage: ai.freeplay.mode [off|recorded|live]");
    if (!tokens.empty())
    {
        const std::string& requested = tokens[0];
        if (requested == "off")
            g_freeplay.mode = FreeplayMode::Off;
        else if (requested == "recorded")
            g_freeplay.mode = FreeplayMode::Recorded;
        else if (requested == "live")
        {
            const std::string refusal = LiveModeRefusal();
            if (!refusal.empty())
                return Fail(ok, refusal);
            g_freeplay.mode = FreeplayMode::Live;
        }
        else
            return Fail(ok, "usage: ai.freeplay.mode [off|recorded|live]");
    }
    return Succeed(ok, std::string("mode=") + ModeName(g_freeplay.mode));
}

std::string Begin(const std::string& payload, bool& ok)
{
    if (!Tokens(payload).empty())
        return Fail(ok, "usage: ai.freeplay.begin");
    const uint32_t epoch = g_freeplay.population.BeginWorld();
    // A new world (a load or a new game) starts from a clean scheduler: no wake, minimum interval or
    // backoff of the old world's owners carries over. (now_ms is the adapter's time_global(), the engine's
    // global timer, which keeps running across a load; the reset is about the owners, not the clock.)
    // Pending provider requests stay tracked only so their replies drain; they carry the old epoch and
    // settle as stale_world.
    g_freeplay.scheduler.Reset();
    g_freeplay.handedOut.clear();
    g_freeplay.observations.clear();
    g_freeplay.knownOwners.clear();
    g_freeplay.appliedRevision.clear();
    g_freeplay.previousObjective.clear();
    return Succeed(ok, "epoch=" + std::to_string(epoch));
}

std::string Sync(const std::string& payload, bool& ok)
{
    const FreeplayResult result = g_freeplay.population.Sync(payload);
    if (!result.ok)
        return Fail(ok, result.reason);
    return Succeed(ok, "owners=" + std::to_string(g_freeplay.population.Owners().size()));
}

std::string Retire(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint16_t groupId = 0;
    if (tokens.size() != 1 || !ParseGroupId(tokens[0], groupId))
        return Fail(ok, "usage: ai.freeplay.retire <group_id>");
    const FreeplayOwner* owner = g_freeplay.population.FindByBinding(FreeplayBinding::Group, groupId);
    if (owner == nullptr)
        return Fail(ok, "unknown_group");
    const uint64_t ownerId = owner->ownerId;
    const FreeplayResult result = g_freeplay.population.Retire(FreeplayBinding::Group, groupId);
    if (!result.ok)
        return Fail(ok, result.reason);
    g_freeplay.scheduler.Forget(ownerId);
    g_freeplay.handedOut.erase(ownerId);
    g_freeplay.observations.erase(ownerId);
    g_freeplay.appliedRevision.erase(ownerId);
    g_freeplay.previousObjective.erase(ownerId);
    g_freeplay.knownOwners.erase(ownerId);
    for (auto it = g_freeplay.pending.begin(); it != g_freeplay.pending.end();)
    {
        if (it->second.ownerId != ownerId)
        {
            ++it;
            continue;
        }
        g_freeplay.abandoned.push_back(it->first);
        it = g_freeplay.pending.erase(it);
    }
    return Succeed(ok, "retired");
}

// Continue the last feasible plan: a blocked current destination falls back to the objective that was in
// force before the last committed decision (once), or to the default hold. A different site changes nothing.
void RevertBlockedObjective(uint64_t ownerId, const std::string& site)
{
    const FreeplayOwner* owner = g_freeplay.population.FindOwner(ownerId);
    if (owner == nullptr || owner->objective.site == "-" || owner->objective.site != site)
        return;
    FreeplayObjective restored;   // hold at "-"
    const auto previous = g_freeplay.previousObjective.find(ownerId);
    if (previous != g_freeplay.previousObjective.end())
    {
        restored = previous->second;
        g_freeplay.previousObjective.erase(previous);
    }
    g_freeplay.population.SetObjective(ownerId, restored.verb, restored.site, FreeplayObjectiveSource::Hold);
}

std::string Event(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint16_t groupId = 0;
    if (tokens.size() != 3 || !ParseGroupId(tokens[0], groupId) ||
        (tokens[1] != "arrived" && tokens[1] != "blocked" && tokens[1] != "casualty" && tokens[1] != "sighting"))
        return Fail(ok, "usage: ai.freeplay.event <group_id> <arrived|blocked|casualty|sighting> <detail>");
    const FreeplayOwner* owner = g_freeplay.population.FindByBinding(FreeplayBinding::Group, groupId);
    if (owner == nullptr)
        return Fail(ok, "unknown_group");
    g_freeplay.scheduler.RequestWake(owner->ownerId, tokens[1], g_freeplay.nowMs);
    // Sightings are frequent; they show up only as the wakeReason of the owner's next decision.
    if (tokens[1] == "sighting")
        return Succeed(ok, "queued");

    FreeplayLedgerEntry entry;
    entry.ownerId = owner->ownerId;
    entry.epoch = g_freeplay.population.Epoch();
    entry.membershipRevision = owner->membershipRevision;
    entry.wakeReason = tokens[1];
    entry.source = "event";
    entry.verdict = "event";
    entry.site = tokens[2];
    g_freeplay.ledger.Append(entry);
    if (tokens[1] == "blocked")
        RevertBlockedObjective(owner->ownerId, tokens[2]);
    return Succeed(ok, "queued");
}

std::string Wake(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint64_t ownerId = 0;
    if (tokens.size() != 2 || !ParseU64(tokens[0], ~0ull, ownerId))
        return Fail(ok, "usage: ai.freeplay.wake <owner_id> <reason>");
    if (g_freeplay.population.FindOwner(ownerId) == nullptr)
        return Fail(ok, "unknown_owner");
    g_freeplay.scheduler.RequestWake(ownerId, tokens[1], g_freeplay.nowMs);
    return Succeed(ok, "queued");
}

std::string Observe(const std::string& payload, bool& ok)
{
    if (g_freeplay.mode == FreeplayMode::Off)
        return Fail(ok, "mode_off");
    const FreeplayObservationParse parsed = ParseFreeplayObservation(g_freeplay.population, payload);
    if (!parsed.ok)
        return Fail(ok, parsed.reason);
    const uint64_t ownerId = parsed.observation.ownerId;
    if (g_freeplay.scheduler.State(ownerId) != FreeplayWakeState::AwaitingObservation)
        return Fail(ok, "not_requested");

    StoredObservation stored;
    stored.observation = parsed.observation;
    stored.observedAtMs = g_freeplay.nowMs;
    const auto wake = g_freeplay.handedOut.find(ownerId);
    if (wake != g_freeplay.handedOut.end())
    {
        stored.reasons = wake->second.reasons;
        stored.queuedAtMs = wake->second.queuedAtMs;
        g_freeplay.handedOut.erase(wake);
    }
    g_freeplay.scheduler.OnObservation(ownerId, g_freeplay.nowMs);
    g_freeplay.observations[ownerId] = stored;

    if (g_freeplay.mode == FreeplayMode::Recorded)
    {
        // A recorded decision may come later or never; the slot is freed now.
        g_freeplay.scheduler.OnDecision(ownerId, false, g_freeplay.nowMs);
        return Succeed(ok, "stored");
    }

    const FreeplayOwner* owner = g_freeplay.population.FindOwner(ownerId);
    const ActorWakeContext context = BuildFreeplayWake(*owner, stored.observation, stored.reasons);
    const ActorWakeSubmitResult submitted = EnsureFreeplayQueue().Submit(context);
    if (!submitted.ok)
    {
        FreeplayLedgerEntry entry = DecisionEntry(ownerId, stored, "hold");
        entry.verdict = submitted.reason;
        g_freeplay.ledger.Append(entry);
        g_freeplay.scheduler.OnDecision(ownerId, true, g_freeplay.nowMs);
        return Fail(ok, submitted.reason);
    }
    PendingRequest request;
    request.ownerId = ownerId;
    request.epoch = stored.observation.epoch;
    request.stored = stored;
    g_freeplay.pending[submitted.requestId] = request;
    return Succeed(ok, "submitted request=" + std::to_string(submitted.requestId));
}

std::string Decide(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint64_t ownerId = 0;
    const bool stay = tokens.size() == 2 && tokens[1] == "stay";
    const bool move = tokens.size() == 3 && (tokens[1] == "move_to" || tokens[1] == "retreat");
    if ((!stay && !move) || !ParseU64(tokens[0], ~0ull, ownerId))
        return Fail(ok, "usage: ai.freeplay.decide <owner_id> stay|move_to <site>|retreat <site>");
    const auto stored = g_freeplay.observations.find(ownerId);
    if (stored == g_freeplay.observations.end())
        return Fail(ok, "no_observation");

    ActorIntentPlan plan;
    ActorAction action;
    action.verb = tokens[1];
    if (move)
        action.target = tokens[2];
    plan.actions.push_back(action);
    const FreeplayVerdict verdict = ValidateFreeplayDecision(g_freeplay.population, stored->second.observation, plan);

    FreeplayLedgerEntry entry = DecisionEntry(ownerId, stored->second, "recorded");
    entry.verb = action.verb;
    entry.site = verdict.ok ? verdict.site : action.target;
    entry.verdict = ApplyVerdict(ownerId, verdict, FreeplayObjectiveSource::Recorded);
    g_freeplay.ledger.Append(entry);
    if (entry.verdict != "accepted" && entry.verdict != "kept")
        return Fail(ok, entry.verdict);
    return Succeed(ok, entry.verdict + " " + verdict.verb + " " + verdict.site);
}

std::string Tick(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint64_t nowMs = 0;
    if (tokens.size() != 1 || !ParseU64(tokens[0], ~0ull, nowMs))
        return Fail(ok, "usage: ai.freeplay.tick <now_ms>");
    g_freeplay.nowMs = nowMs;
    FreeplayPopulation& population = g_freeplay.population;
    FreeplayScheduler& scheduler = g_freeplay.scheduler;

    for (const uint64_t ownerId : population.TakeChangedOwners())
    {
        const bool spawned = g_freeplay.knownOwners.insert(ownerId).second;
        scheduler.RequestWake(ownerId, spawned ? "spawn" : "membership", nowMs);
    }

    PollPendingRequests();

    for (const uint64_t ownerId : scheduler.TakeTimedOutObservations(nowMs))
    {
        FreeplayLedgerEntry entry;
        entry.ownerId = ownerId;
        entry.epoch = population.Epoch();
        if (const FreeplayOwner* owner = population.FindOwner(ownerId))
            entry.membershipRevision = owner->membershipRevision;
        const auto wake = g_freeplay.handedOut.find(ownerId);
        if (wake != g_freeplay.handedOut.end())
        {
            entry.wakeReason = wake->second.reasons;
            entry.queueAgeMs = Elapsed(wake->second.queuedAtMs, wake->second.handedOutAtMs);
            entry.latencyMs = Elapsed(wake->second.handedOutAtMs, nowMs);
            g_freeplay.handedOut.erase(wake);
        }
        entry.verdict = "observation_timeout";
        g_freeplay.ledger.Append(entry);
    }

    ForgetRetiredOwners();

    std::string out;
    if (g_freeplay.mode == FreeplayMode::Off)
        return Succeed(ok, out);

    for (const uint64_t ownerId : scheduler.TakeObservationRequests(population, nowMs))
    {
        const FreeplayOwner* owner = population.FindOwner(ownerId);
        HandedOutWake wake;
        wake.reasons = scheduler.Reasons(ownerId);
        wake.queuedAtMs = scheduler.QueuedAtMs(ownerId);
        wake.handedOutAtMs = nowMs;
        g_freeplay.handedOut[ownerId] = wake;
        out += "observe " + std::to_string(ownerId) +
            (owner->binding == FreeplayBinding::Group ? " group " : " npc ") + std::to_string(owner->bindingId) + " " +
            std::to_string(owner->membershipRevision) + "\n";
    }

    for (const FreeplayOwner& owner : population.Owners())
    {
        if (!IsFreeplayTransferable(owner))
            continue;
        const auto applied = g_freeplay.appliedRevision.find(owner.ownerId);
        if (applied != g_freeplay.appliedRevision.end() && applied->second == owner.objective.revision)
            continue;
        g_freeplay.appliedRevision[owner.ownerId] = owner.objective.revision;
        out += "apply " + std::to_string(owner.ownerId) + " " + std::to_string(owner.bindingId) + " " +
            std::to_string(owner.objective.revision) + " " + owner.objective.verb + " " + owner.objective.site + " " +
            FreeplaySourceName(owner.objective.source) + "\n";
    }
    return Succeed(ok, out);
}

std::string Owners(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    if (tokens.size() > 1)
        return Fail(ok, "usage: ai.freeplay.owners [level]");
    const std::string level = tokens.empty() ? std::string() : tokens[0];
    std::string out;
    for (const FreeplayOwner& owner : g_freeplay.population.Owners())
    {
        if (!level.empty() && owner.level != level)
            continue;
        std::string sites;
        const auto stored = g_freeplay.observations.find(owner.ownerId);
        if (stored != g_freeplay.observations.end())
        {
            for (const FreeplaySite& site : stored->second.observation.sites)
                sites += (sites.empty() ? "" : ",") + site.name;
        }
        out += "owner " + std::to_string(owner.ownerId) + " name=" + owner.name +
            " kind=" + FreeplayKindName(owner.kind) + " binding=" + BindingText(owner) +
            " writer=" + FreeplayWriterName(owner.writer) + " level=" + owner.level +
            " members=" + std::to_string(owner.members.size()) +
            " mrev=" + std::to_string(owner.membershipRevision) +
            " state=" + WakeStateName(g_freeplay.scheduler.State(owner.ownerId)) +
            " objective=" + owner.objective.verb + ":" + owner.objective.site +
            " source=" + FreeplaySourceName(owner.objective.source) +
            " rev=" + std::to_string(owner.objective.revision) + " sites=" + (sites.empty() ? "-" : sites) + "\n";
    }
    return Succeed(ok, out);
}

std::string Census(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    if (tokens.size() > 1)
        return Fail(ok, "usage: ai.freeplay.census [level]");
    return Succeed(ok, g_freeplay.population.FormatCensus(tokens.empty() ? std::string() : tokens[0]));
}

std::string Log(const std::string& payload, bool& ok)
{
    const std::vector<std::string> tokens = Tokens(payload);
    uint64_t count = 50;
    if (tokens.size() > 1 || (tokens.size() == 1 && !ParseU64(tokens[0], ~0ull, count)))
        return Fail(ok, "usage: ai.freeplay.log [count]");
    return Succeed(ok, g_freeplay.ledger.Format(static_cast<size_t>(count)));
}
} // namespace

std::string HandleFreeplayVerb(const std::string& verb, const std::string& payload, bool& ok)
{
    if (verb == "ai.freeplay.mode")
        return Mode(payload, ok);
    if (verb == "ai.freeplay.begin")
        return Begin(payload, ok);
    if (verb == "ai.freeplay.sync")
        return Sync(payload, ok);
    if (verb == "ai.freeplay.retire")
        return Retire(payload, ok);
    if (verb == "ai.freeplay.event")
        return Event(payload, ok);
    if (verb == "ai.freeplay.wake")
        return Wake(payload, ok);
    if (verb == "ai.freeplay.observe")
        return Observe(payload, ok);
    if (verb == "ai.freeplay.decide")
        return Decide(payload, ok);
    if (verb == "ai.freeplay.tick")
        return Tick(payload, ok);
    if (verb == "ai.freeplay.owners")
        return Owners(payload, ok);
    if (verb == "ai.freeplay.census")
        return Census(payload, ok);
    if (verb == "ai.freeplay.log")
        return Log(payload, ok);
    return Fail(ok, "unknown ai verb: " + verb);
}

void ResetFreeplay()
{
    if (g_freeplayQueue)
        g_freeplayQueue->Shutdown();
    g_freeplayQueue.reset();
    g_freeplayTestProvider = false;
    g_freeplay = FreeplayRuntime();
}

void SetFreeplayProviderForTests(std::unique_ptr<IActorIntentProvider> provider)
{
    // Requests on the replaced provider can never answer: settle them so no scheduler slot stays held.
    for (const auto& item : g_freeplay.pending)
    {
        FreeplayLedgerEntry entry = DecisionEntry(item.second.ownerId, item.second.stored, "hold");
        entry.verdict = "provider_replaced";
        g_freeplay.ledger.Append(entry);
        g_freeplay.scheduler.OnDecision(item.second.ownerId, false, g_freeplay.nowMs);
    }
    g_freeplay.pending.clear();
    g_freeplay.abandoned.clear();
    if (g_freeplayQueue)
        g_freeplayQueue->Shutdown();
    g_freeplayQueue = std::make_unique<AsyncActorProviderQueue>(std::move(provider));
    g_freeplayTestProvider = true;
}
} // namespace xrSim
