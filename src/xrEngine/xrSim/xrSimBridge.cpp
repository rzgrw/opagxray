#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimAnthropicTransport.h"
#include "xrSim/xrSimActorProvider.h"
#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimFreeplayBridge.h"
#include "xrSim/xrSimNullAgent.h"
#include "xrSim/xrSimPackSpawn.h"
#include "xrSim/xrSimWorldState.h"

#include <cstdio>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace xrSim
{
namespace
{
WorldState g_debugWorld;
NullAgentRuntime g_nullAgent;
ActorRuntime g_actorRuntime;
ActorAgentRecord g_debugSquad;
ActorAgentRecord g_debugMutantPack;
std::unique_ptr<AsyncActorProviderQueue> g_liveActorQueue;

enum class PendingActorWakeKind
{
    DebugActor,
    SessionPack,
};

struct PendingActorWake
{
    PendingActorWakeKind kind = PendingActorWakeKind::DebugActor;
    std::string actorName;
    uint32_t packId = 0;
    uint32_t gameDay = 0;
};

std::unordered_map<uint64_t, PendingActorWake> g_pendingActorWakes;
uint32_t g_nextSeq = 1;
bool g_initialized = false;

void ResetLiveActorQueue()
{
    if (g_liveActorQueue)
        g_liveActorQueue->Shutdown();
    g_liveActorQueue.reset();
    g_pendingActorWakes.clear();
}

void ResetDebugWorld()
{
    ResetLiveActorQueue();
    g_debugWorld = WorldState{};
    g_nullAgent = NullAgentRuntime{};
    g_actorRuntime = ActorRuntime{};
    g_debugSquad = MakeDebugSquadAgent();
    g_debugMutantPack = MakeDebugMutantPackAgent();
    const Handle region = g_debugWorld.CreateRegion("debug_region", 100);
    const Handle species = g_debugWorld.CreateSpecies("blind_dog");
    g_debugWorld.SetPopulation(region, species, 50);
    g_nextSeq = 1;
    g_initialized = true;
    ResetSessionPacks();
}

AsyncActorProviderQueue* EnsureLiveActorQueue()
{
    if (!g_liveActorQueue)
    {
        const AgentProviderConfig config = LoadAgentProviderConfigFromEnvironment();
        g_liveActorQueue =
            std::make_unique<AsyncActorProviderQueue>(CreateLiveActorIntentProvider(config));
    }
    return g_liveActorQueue.get();
}

void EnsureDebugWorld()
{
    if (!g_initialized)
        ResetDebugWorld();
}

WorldState MakeDebugBaseline()
{
    WorldState baseline;
    const Handle region = baseline.CreateRegion("debug_region", 100);
    const Handle species = baseline.CreateSpecies("blind_dog");
    baseline.SetPopulation(region, species, 50);
    return baseline;
}

std::string InjectIntent(const std::string& payload, bool& ok)
{
    char tool[64]{};
    char regionName[64]{};
    char speciesName[64]{};
    int delta = 0;
    if (4 != std::sscanf(payload.c_str(), "%63s %63s %63s %d", tool, regionName, speciesName, &delta))
    {
        ok = false;
        return "usage: ai.inject adjust_population <region> <species> <delta>";
    }

    if (std::string(tool) != "adjust_population")
    {
        ok = false;
        return std::string("unknown intent: ") + tool;
    }

    EnsureDebugWorld();
    const Handle region = g_debugWorld.FindRegionByName(regionName);
    if (!region.IsValid())
    {
        ok = false;
        return std::string("unknown region: ") + regionName;
    }

    const Handle species = g_debugWorld.FindSpeciesByName(speciesName);
    if (!species.IsValid())
    {
        ok = false;
        return std::string("unknown species: ") + speciesName;
    }

    const Result result = g_debugWorld.ApplyAdjustPopulation(g_nextSeq++, region, species, delta, 0);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    return "accepted applied_delta=" + std::to_string(result.appliedDelta);
}

std::string FormatToolLog()
{
    EnsureDebugWorld();
    const std::vector<ToolRecord>& log = g_debugWorld.ToolLog();
    if (log.empty())
        return "empty";

    std::ostringstream out;
    for (size_t i = 0; i < log.size(); ++i)
    {
        const ToolRecord& record = log[i];
        if (i != 0)
            out << " | ";
        out << "seq=" << record.seq << " day=" << record.gameDay << " tool=" << record.tool
            << " accepted=" << (record.accepted ? 1 : 0) << " requested=" << record.requestedDelta
            << " applied=" << record.appliedDelta;
        if (!record.reason.empty())
            out << " reason=" << record.reason;
    }
    return out.str();
}

std::string WakeZoneAgent(IAgentProvider* provider, const char* label, bool& ok)
{
    EnsureDebugWorld();
    if (provider)
        g_nullAgent.SetProvider(provider);
    const Result result = g_nullAgent.Wake(g_debugWorld, 0);
    if (provider)
        g_nullAgent.SetProvider(nullptr);
    ok = result.ok;
    if (!result.ok)
        return result.reason;
    g_nextSeq = uint32_t(g_debugWorld.ToolLog().size() + 1);

    std::ostringstream out;
    out << label << " applied_delta=" << result.appliedDelta << " wakes=" << g_nullAgent.WakeCount();
    if (provider)
    {
        const AgentProviderResult& providerResult = g_nullAgent.LastProviderResult();
        out << " provider=" << providerResult.provider << " model=" << providerResult.model;
        if (providerResult.coast)
            out << " coast=" << providerResult.coastReason;
        if (!providerResult.error.empty())
            out << " error=" << providerResult.error;
    }
    return out.str();
}

std::string WakeNullAgent(bool& ok)
{
    return WakeZoneAgent(nullptr, "xrsim wake", ok);
}

std::string WakeLiveAgent(bool& ok)
{
    const AgentProviderConfig config = LoadAgentProviderConfigFromEnvironment();
    std::unique_ptr<IAgentProvider> provider = CreateLiveAgentProvider(config);
    if (!provider)
    {
        ok = false;
        return "live provider unavailable";
    }
    return WakeZoneAgent(provider.get(), "xrsim live wake", ok);
}

ActorAgentRecord* FindDebugActor(const std::string& name)
{
    EnsureDebugWorld();
    if (name == "squad")
        return &g_debugSquad;
    if (name == "mutant_pack")
        return &g_debugMutantPack;
    return nullptr;
}

std::string WakeDebugActor(const std::string& payload, bool& ok)
{
    std::istringstream input(payload);
    std::string actorName;
    std::string mode;
    std::string trailing;
    if (!(input >> actorName) || (input >> mode && input >> trailing))
    {
        ok = false;
        return "usage: agent.actor.wake <squad|mutant_pack> [live]";
    }
    if (!mode.empty() && mode != "live")
    {
        ok = false;
        return "usage: agent.actor.wake <squad|mutant_pack> [live]";
    }

    ActorAgentRecord* actor = FindDebugActor(actorName);
    if (!actor)
    {
        ok = false;
        return "unknown actor: " + actorName;
    }

    const std::string situation = actorName == "mutant_pack" ? "heard_gunfire" : "player_visible medium_range";
    if (mode == "live")
    {
        const ActorWakeContext context = g_actorRuntime.PrepareWake(*actor, g_debugWorld, situation, 0);
        const ActorWakeSubmitResult submitted = EnsureLiveActorQueue()->Submit(context);
        ok = submitted.ok;
        if (!submitted.ok)
            return submitted.reason;

        PendingActorWake pending;
        pending.kind = PendingActorWakeKind::DebugActor;
        pending.actorName = actorName;
        g_pendingActorWakes[submitted.requestId] = pending;
        return "actor wake queued request=" + std::to_string(submitted.requestId);
    }

    const ActuatorResult result = g_actorRuntime.Wake(g_debugWorld, *actor, situation, 0);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    return std::string("actor wake scope=") + ActorScopeName(actor->scope) + " id=" + std::to_string(actor->agentId) +
        " goal=" + actor->lastIntent.goal + " commands=" + std::to_string(result.commands.size()) +
        " applied_delta=" + std::to_string(result.appliedDelta);
}

bool ParsePackId(const std::string& payload, uint32_t& packId)
{
    std::istringstream in(payload);
    std::string trailing;
    return bool(in >> packId) && !(in >> trailing);
}

bool ParsePackWakePayload(const std::string& payload, uint32_t& packId, bool& live)
{
    std::istringstream in(payload);
    std::string mode;
    std::string trailing;
    if (!(in >> packId))
        return false;
    if (!(in >> mode))
    {
        live = false;
        return true;
    }
    if (mode != "live" || in >> trailing)
        return false;
    live = true;
    return true;
}

std::string RegisterPackFromPayload(const std::string& payload, bool& ok)
{
    std::istringstream in(payload);
    PackRegistration registration;
    if (!(in >> registration.section))
    {
        ok = false;
        return "usage: agent.pack.register <section> <id> [id...]";
    }

    uint32_t rawId = 0;
    while (in >> rawId)
    {
        if (rawId > UINT16_MAX)
        {
            ok = false;
            return "object id out of range: " + std::to_string(rawId);
        }
        registration.objectIds.push_back(uint16_t(rawId));
    }

    if (registration.objectIds.empty())
    {
        ok = false;
        return "missing pack members";
    }

    EnsureDebugWorld();

    registration.spawnReason = "session_spawn";
    const PackRegisterResult result = RegisterSessionMutantPack(registration);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    std::ostringstream out;
    out << "PACK#" << result.packId << " " << FormatPackList();
    return out.str();
}

std::string ObserveRegisteredPack(const std::string& payload, bool& ok)
{
    uint32_t packId = 0;
    if (!ParsePackId(payload, packId))
    {
        ok = false;
        return "usage: agent.pack.observe <pack_id>";
    }

    EnsureDebugWorld();
    std::string observation = ObservePack(packId, g_debugWorld);
    if (observation.rfind("unknown pack:", 0) == 0)
    {
        ok = false;
        return observation;
    }
    ok = true;
    return observation;
}

std::string WakeRegisteredPack(const std::string& payload, bool& ok)
{
    uint32_t packId = 0;
    bool live = false;
    if (!ParsePackWakePayload(payload, packId, live))
    {
        ok = false;
        return "usage: agent.pack.wake <pack_id> [live]";
    }

    EnsureDebugWorld();
    if (live)
    {
        const PackWakePrepareResult prepared = PreparePackWake(packId, g_debugWorld, 0);
        if (!prepared.ok)
        {
            ok = false;
            return prepared.reason;
        }

        const ActorWakeSubmitResult submitted = EnsureLiveActorQueue()->Submit(prepared.context);
        ok = submitted.ok;
        if (!submitted.ok)
            return submitted.reason;

        PendingActorWake pending;
        pending.kind = PendingActorWakeKind::SessionPack;
        pending.packId = packId;
        g_pendingActorWakes[submitted.requestId] = pending;
        return "pack wake queued request=" + std::to_string(submitted.requestId);
    }

    const ActuatorResult result = WakePack(packId, g_debugWorld, g_actorRuntime, 0);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    const ActorProviderResult& provider = g_actorRuntime.LastProviderResult();
    std::ostringstream out;
    out << "pack wake id=" << packId << " goal=" << provider.plan.goal << " commands=" << result.commands.size()
        << " applied_delta=" << result.appliedDelta;
    return out.str();
}

std::string PollLiveActorWake(const std::string& payload, bool& ok)
{
    std::istringstream in(payload);
    uint64_t requestId = 0;
    std::string trailing;
    if (!(in >> requestId) || in >> trailing)
    {
        ok = false;
        return "usage: agent.actor.poll <request_id>";
    }

    const auto pending = g_pendingActorWakes.find(requestId);
    if (!g_liveActorQueue || pending == g_pendingActorWakes.end())
    {
        ok = false;
        return "unknown request: " + std::to_string(requestId);
    }

    ActorWakePollResult polled = g_liveActorQueue->Poll(requestId);
    if (!polled.ok)
    {
        g_pendingActorWakes.erase(pending);
        ok = false;
        return polled.reason;
    }
    if (!polled.ready)
    {
        ok = true;
        return "pending request=" + std::to_string(requestId);
    }

    const PendingActorWake binding = pending->second;
    g_pendingActorWakes.erase(pending);

    ActuatorResult applied;
    ActorAgentRecord* actor = nullptr;
    if (binding.kind == PendingActorWakeKind::DebugActor)
    {
        actor = FindDebugActor(binding.actorName);
        if (!actor)
        {
            ok = false;
            return "unknown actor: " + binding.actorName;
        }
        applied = g_actorRuntime.ApplyProviderResult(
            g_debugWorld, *actor, polled.providerResult, binding.gameDay);
    }
    else
    {
        applied = ApplyPackWakeResult(
            binding.packId, g_debugWorld, g_actorRuntime, polled.providerResult, binding.gameDay);
    }

    ok = applied.ok;
    if (!applied.ok)
        return applied.reason;

    std::ostringstream out;
    if (binding.kind == PendingActorWakeKind::DebugActor)
    {
        out << "actor wake scope=" << ActorScopeName(actor->scope) << " id=" << actor->agentId;
    }
    else
    {
        out << "pack wake id=" << binding.packId;
    }
    if (!polled.providerResult.plan.goal.empty())
        out << " goal=" << polled.providerResult.plan.goal;
    out << " commands=" << applied.commands.size();
    out << " applied_delta=" << applied.appliedDelta;
    out << " provider=" << polled.providerResult.provider;
    out << " model=" << polled.providerResult.model;
    if (applied.coast)
    {
        const std::string reason = polled.providerResult.coastReason.empty() ? applied.reason
                                                                            : polled.providerResult.coastReason;
        out << " coast=" << reason;
    }
    return out.str();
}
} // namespace

std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok)
{
    if (verb.rfind("ai.freeplay.", 0) == 0)
        return HandleFreeplayVerb(verb, payload, ok);

    if (verb == "ai.reset")
    {
        ResetDebugWorld();
        ResetFreeplay();
        ok = true;
        return "xrsim reset";
    }

    if (verb == "ai.status")
    {
        ok = true;
        return std::string("xrsim initialized=") + (g_initialized ? "1" : "0") +
            " log=" + std::to_string(g_debugWorld.ToolLog().size());
    }

    if (verb == "ai.observe")
    {
        EnsureDebugWorld();
        ok = true;
        return g_debugWorld.Digest();
    }

    if (verb == "ai.inject")
        return InjectIntent(payload, ok);

    if (verb == "ai.wake")
        return WakeNullAgent(ok);

    if (verb == "ai.snapshot")
    {
        EnsureDebugWorld();
        ok = true;
        return g_debugWorld.SaveSnapshot();
    }

    if (verb == "ai.restore")
    {
        Result result = g_debugWorld.LoadSnapshot(payload);
        ok = result.ok;
        if (!result.ok)
            return result.reason;
        g_initialized = true;
        g_nextSeq = uint32_t(g_debugWorld.ToolLog().size() + 1);
        return "xrsim restored log=" + std::to_string(g_debugWorld.ToolLog().size());
    }

    if (verb == "ai.log")
    {
        ok = true;
        return FormatToolLog();
    }

    if (verb == "ai.replay")
    {
        EnsureDebugWorld();
        WorldState replay = MakeDebugBaseline();
        const Result result = replay.ReplayToolLogFrom(g_debugWorld);
        ok = result.ok;
        if (!result.ok)
            return result.reason;
        return "xrsim replay digest=" + replay.Digest();
    }

    if (verb == "agent.list")
    {
        EnsureDebugWorld();
        ok = true;
        return "id=1 scope=ZONE provider=null wakes=" + std::to_string(g_nullAgent.WakeCount()) + " state=ready";
    }

    if (verb == "agent.provider")
    {
        EnsureDebugWorld();
        ok = true;
        if (payload == "live")
            return DescribeAgentProviderConfig(LoadAgentProviderConfigFromEnvironment()) +
                " transport=" + DescribeAnthropicHttpTransport();
        return "id=1 provider=null model=deterministic-null";
    }

    if (verb == "agent.prompt")
    {
        EnsureDebugWorld();
        AgentWakeContext context;
        context.agentId = 1;
        context.gameDay = 0;
        context.observation = g_debugWorld.Digest();
        ok = true;
        return BuildAgentWakePrompt(context);
    }

    if (verb == "agent.wake")
    {
        if (payload == "live")
            return WakeLiveAgent(ok);
        if (!payload.empty() && payload != "1")
        {
            ok = false;
            return "unknown agent id: " + payload;
        }
        return WakeNullAgent(ok);
    }

    if (verb == "agent.actor.list")
    {
        EnsureDebugWorld();
        ok = true;
        return "SQUAD#101 debug_stalker_squad | MUTANT_PACK#201 debug_blind_dog_pack";
    }

    if (verb == "agent.actor.observe")
    {
        ActorAgentRecord* actor = FindDebugActor(payload);
        if (!actor)
        {
            ok = false;
            return "unknown actor: " + payload;
        }
        const std::string situation = payload == "mutant_pack" ? "heard_gunfire" : "player_visible medium_range";
        ok = true;
        return BuildActorObservation(*actor, g_debugWorld, situation);
    }

    if (verb == "agent.actor.wake")
        return WakeDebugActor(payload, ok);

    if (verb == "agent.actor.poll")
        return PollLiveActorWake(payload, ok);

    if (verb == "agent.actor.commands")
    {
        EnsureDebugWorld();
        ok = true;
        return FormatActuatorCommands(g_actorRuntime.LastCommands());
    }

    if (verb == "agent.pack.register")
        return RegisterPackFromPayload(payload, ok);

    if (verb == "agent.pack.list")
    {
        ok = true;
        return FormatPackList();
    }

    if (verb == "agent.pack.observe")
        return ObserveRegisteredPack(payload, ok);

    if (verb == "agent.pack.wake")
        return WakeRegisteredPack(payload, ok);

    if (verb == "agent.pack.commands")
    {
        EnsureDebugWorld();
        ok = true;
        return FormatActuatorCommands(g_actorRuntime.LastCommands());
    }

    if (verb == "agent.tree")
    {
        EnsureDebugWorld();
        ok = true;
        return "ZONE#1 provider=null wakes=" + std::to_string(g_nullAgent.WakeCount()) +
            " log=" + std::to_string(g_debugWorld.ToolLog().size());
    }

    ok = false;
    return "unknown ai verb: " + verb;
}
} // namespace xrSim
