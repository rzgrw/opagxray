#pragma once

#include "xrSim/xrSimActorRuntime.h"

#include <memory>
#include <string>

namespace xrSim
{
// ai.freeplay.* verbs: one runtime that ties the owner registry, the decision validator with its
// ledger, the wake scheduler and the async provider queue together.
//
// Not thread-safe; callers must never overlap. Today they run on two threads, one after the other:
// * the main thread in FrameMove (the CoC adapter's actor_on_update: mode, sync, tick, observe), then the
//   agent bridge's OnFrame right after it (CRenderDevice::ProcessFrame);
// * with mt_alife on (the default), the ALife update, which CALifeUpdateManager::shedule_Update pushes to
//   Device.seqParallel: a task that ProcessFrame starts after OnFrame, runs while the main thread renders,
//   and waits for before the frame ends. The adapter's squad_on_update, squad_on_unregister and
//   squad_on_npc_death callbacks and its get_squad_target wrapper (blocked events) can call xrsim_verb there.
// So the main thread must not call these verbs during DoRender (or from any other thread or task).
std::string HandleFreeplayVerb(const std::string& verb, const std::string& payload, bool& ok);
void ResetFreeplay();
void SetFreeplayProviderForTests(std::unique_ptr<IActorIntentProvider> provider);   // replaces the live provider
} // namespace xrSim
