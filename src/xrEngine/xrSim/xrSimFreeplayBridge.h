#pragma once

#include "xrSim/xrSimActorRuntime.h"

#include <memory>
#include <string>

namespace xrSim
{
// ai.freeplay.* verbs: one runtime that ties the owner registry, the decision validator with its
// ledger, the wake scheduler and the async provider queue together. Main thread only.
std::string HandleFreeplayVerb(const std::string& verb, const std::string& payload, bool& ok);
void ResetFreeplay();
void SetFreeplayProviderForTests(std::unique_ptr<IActorIntentProvider> provider);   // replaces the live provider
} // namespace xrSim
