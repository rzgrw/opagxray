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

// The variables removed from the CLI child's environment: API keys and endpoint (so the CLI uses the player's own
// login) and the variables that tie a process to a running Claude Code session. tools/freeplay_play.py removes the
// same list from the game's environment; tools/tests/test_freeplay_play.py checks the two are identical.
const std::vector<std::string>& CliRemovedEnvironmentNames();
std::vector<std::string> CliChildEnvironment(const std::vector<std::string>& parent);   // "NAME=value" entries, order kept

// A warning the runner cannot return as a value (it happens after the result is settled): one line, handed to the
// sink the engine installs (its log). Without a sink the line is dropped. nullptr removes the sink.
using CliWarningSink = void (*)(const char* line);
void SetCliWarningSink(CliWarningSink sink);

#if !defined(_WIN32)
// Reaps a child that was sent SIGKILL, polling for at most limitMs. A child that has not exited by then (one stuck in
// uninterruptible sleep) is left unreaped with one warning, so the worker thread, and the join at quit, never block
// on it. Returns the exit code (128 + signal when killed), or -1 when given up or not this process's child.
int ReapKilledChild(int pid, uint32_t limitMs);
#endif

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
