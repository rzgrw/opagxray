#pragma once

#include "xrSim/xrSimActorRuntime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrSim
{
struct CliRunResult
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;   // stdout
    std::string error;    // short reason when !started, or the first line of stderr
};

class ICliRunner
{
public:
    virtual ~ICliRunner() = default;
    // argv[0] is the executable. No shell is involved. stdinText is written to the child's stdin and closed.
    virtual CliRunResult Run(const std::vector<std::string>& argv, const std::string& stdinText, uint32_t timeoutMs) = 0;
    // Kills a running child from another thread; Run then returns promptly. Sticky: once cancelled, the
    // runner starts no further child (the provider queue cancels only at shutdown).
    virtual void Cancel() = 0;
};

// POSIX: a child spawned from an argument vector with three pipes, in its own process group, in the system
// temp directory, inheriting no other descriptor. Windows: Run returns started=false, error "unsupported_platform".
std::unique_ptr<ICliRunner> CreateProcessCliRunner();
bool IsCliExecutableAvailable(const std::string& pathOrName);   // absolute path that is executable, or a bare name found on PATH

struct ClaudeCliConfig
{
    std::string executable = "claude";
    std::string model;
    uint32_t timeoutMs = 60000;
};

std::vector<std::string> BuildClaudeCliArguments(const ClaudeCliConfig& config);
std::string ExtractIntentBlock(const std::string& text);   // from the first line equal to "xrsim_actor_intent_v1" through the next line equal to "end", inclusive; "" if absent

// One Claude Code CLI call in headless mode per wake, on the provider queue's worker thread.
class ClaudeCliActorIntentProvider final : public IActorIntentProvider
{
public:
    ClaudeCliActorIntentProvider(const ClaudeCliConfig& config, std::unique_ptr<ICliRunner> runner);
    ActorProviderResult Wake(const ActorWakeContext& context) override;
    void Cancel() override;

private:
    ClaudeCliConfig m_config;
    std::unique_ptr<ICliRunner> m_runner;
};
} // namespace xrSim
