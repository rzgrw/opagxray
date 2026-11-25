#include "xrSim/xrSimClaudeCliProvider.h"

#include "xrSim/xrSimActorIntent.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace xrSim
{
namespace
{
const char* kIntentHeader = "xrsim_actor_intent_v1";
const char* kSystemPrompt = "You answer with the requested schema lines only. No prose, no markdown, no code fences.";
constexpr size_t kMaxErrorLine = 200;
std::atomic<CliWarningSink> g_warningSink{nullptr};

void Warn(const std::string& line)
{
    if (const CliWarningSink sink = g_warningSink.load(std::memory_order_acquire))
        sink(line.c_str());
}

std::string Trimmed(const std::string& line)
{
    size_t begin = 0, end = line.size();
    while (begin < end && (line[begin] == ' ' || line[begin] == '\t'))
        ++begin;
    while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
        --end;
    return line.substr(begin, end - begin);
}

// The first stderr line, at most kMaxErrorLine bytes: all the game ever keeps of the child's stderr.
std::string FirstLine(const std::string& text)
{
    const std::string line = Trimmed(text.substr(0, text.find('\n')));
    return line.size() > kMaxErrorLine ? line.substr(0, kMaxErrorLine) : line;
}

ActorProviderResult CoastCli(const ClaudeCliConfig& config, const char* reason, const std::string& error = {})
{
    ActorProviderResult result;
    result.ok = true;
    result.coast = true;
    result.provider = "claude_cli";
    result.model = config.model;
    result.error = error;
    result.coastReason = reason;
    result.plan.coast = true;
    return result;
}

ActorProviderResult FailCli(const ClaudeCliConfig& config, const std::string& error)
{
    ActorProviderResult result;
    result.provider = "claude_cli";
    result.model = config.model;
    result.error = error;
    return result;
}

#if !defined(_WIN32)
constexpr size_t kMaxOutput = 1u << 20;   // stdout kept; anything beyond is read and dropped
constexpr size_t kMaxStderr = 4096;       // enough for the first line
constexpr int kTermGraceMs = 500;         // SIGTERM to SIGKILL
constexpr uint32_t kReapLimitMs = 3000;   // SIGKILL to giving up on the reap

using Clock = std::chrono::steady_clock;

bool IsExecutableFile(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && access(path.c_str(), X_OK) == 0;
}

// An absolute path, or a bare name searched on PATH (empty entries, i.e. the current directory, skipped).
bool ResolveExecutable(const std::string& name, std::string& resolved)
{
    if (name.empty())
        return false;
    if (name.find('/') != std::string::npos)
    {
        if (name[0] != '/' || !IsExecutableFile(name))
            return false;
        resolved = name;
        return true;
    }
    const char* path = std::getenv("PATH");
    const std::string dirs = path != nullptr && *path != '\0' ? path : "/usr/bin:/bin";
    size_t begin = 0;
    while (begin <= dirs.size())
    {
        size_t end = dirs.find(':', begin);
        if (end == std::string::npos)
            end = dirs.size();
        const std::string dir = dirs.substr(begin, end - begin);
        if (!dir.empty())
        {
            const std::string candidate = dir + "/" + name;
            if (IsExecutableFile(candidate))
            {
                resolved = candidate;
                return true;
            }
        }
        begin = end + 1;
    }
    return false;
}

void CloseFd(int& fd)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}

bool SetFlags(int fd, bool nonBlocking)
{
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
        return false;
    if (!nonBlocking)
        return true;
    const int flags = fcntl(fd, F_GETFL);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int RemainingMs(Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : left > 1000 ? 1000 : static_cast<int>(left);
}

// The child's environment: the game's, without the variables CliRemovedEnvironmentNames lists.
std::vector<std::string> ChildEnvironment()
{
    std::vector<std::string> parent;
    for (char** item = environ; item != nullptr && *item != nullptr; ++item)
        parent.emplace_back(*item);
    return CliChildEnvironment(parent);
}

std::string TempDirectory()
{
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = tmp != nullptr && *tmp == '/' ? tmp : "/tmp";
    struct stat info;
    if (stat(dir.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        dir = "/tmp";
    return dir;
}

class ProcessCliRunner final : public ICliRunner
{
public:
    ProcessCliRunner()
    {
        if (pipe(m_wake) != 0 || !SetFlags(m_wake[0], true) || !SetFlags(m_wake[1], true))
        {
            CloseFd(m_wake[0]);
            CloseFd(m_wake[1]);
        }
    }

    ~ProcessCliRunner() override
    {
        CloseFd(m_wake[0]);
        CloseFd(m_wake[1]);
    }

    CliRunResult Run(const std::vector<std::string>& argv, const std::string& stdinText, uint32_t timeoutMs) override;

    void Cancel() override
    {
        std::lock_guard guard{m_mutex};
        m_cancelled.store(true, std::memory_order_release);
        if (m_wake[1] >= 0)
        {
            const char byte = 1;
            (void)!write(m_wake[1], &byte, 1);
        }
    }

private:
    bool Cancelled() const { return m_cancelled.load(std::memory_order_acquire); }
    pid_t Spawn(const std::string& path, const std::vector<std::string>& argv, int in, int out, int err, std::string& error);
    bool WaitExited(pid_t pid, Clock::time_point until, bool stopOnCancel);
    int KillAndReap(pid_t pid, bool force);

    std::mutex m_mutex;   // orders Cancel against the spawn
    std::atomic_bool m_cancelled{false};
    int m_wake[2] = {-1, -1};
};

pid_t ProcessCliRunner::Spawn(
    const std::string& path, const std::vector<std::string>& argv, int in, int out, int err, std::string& error)
{
    std::vector<char*> args;
    for (const std::string& arg : argv)
        args.push_back(const_cast<char*>(arg.c_str()));
    args[0] = const_cast<char*>(path.c_str());
    args.push_back(nullptr);
    const std::vector<std::string> envStrings = ChildEnvironment();
    std::vector<char*> envp;
    for (const std::string& entry : envStrings)
        envp.push_back(const_cast<char*>(entry.c_str()));
    envp.push_back(nullptr);
    const std::string cwd = TempDirectory();

#if defined(__APPLE__)
    // posix_spawn copies no address space (the game's is large). CLOEXEC_DEFAULT closes every descriptor
    // the file actions do not name; the child gets its own process group so the whole group can be killed.
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_adddup2(&actions, in, 0);
    posix_spawn_file_actions_adddup2(&actions, out, 1);
    posix_spawn_file_actions_adddup2(&actions, err, 2);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // the replacement needs macOS 26; the target is 15
#endif
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    sigset_t defaults, mask;
    sigfillset(&defaults);
    sigemptyset(&mask);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setsigmask(&attr, &mask);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, static_cast<short>(POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETPGROUP |
                                        POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK));
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, path.c_str(), &actions, &attr, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0)
    {
        error = std::string("spawn_failed:") + std::strerror(rc);
        return -1;
    }
    return pid;
#else
    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 65536)
        maxFd = 65536;
    const pid_t pid = fork();
    if (pid < 0)
    {
        error = std::string("spawn_failed:") + std::strerror(errno);
        return -1;
    }
    if (pid == 0)
    {
        // Async-signal-safe calls only until execve.
        setpgid(0, 0);
        if (dup2(in, 0) < 0 || dup2(out, 1) < 0 || dup2(err, 2) < 0 || chdir(cwd.c_str()) != 0)
            _exit(127);
        for (int fd = 3; fd < maxFd; ++fd)
            close(fd);
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        signal(SIGPIPE, SIG_DFL);
        execve(path.c_str(), args.data(), envp.data());
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
#endif
}

// True once the child has exited (left as a zombie, not reaped, so its pid and process group stay ours).
bool ProcessCliRunner::WaitExited(pid_t pid, Clock::time_point until, bool stopOnCancel)
{
    for (;;)
    {
        siginfo_t info;
        std::memset(&info, 0, sizeof(info));
        const int rc = waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT);
        if (rc == 0 && info.si_pid == pid)
            return true;
        if (rc != 0 && errno != EINTR)
            return true;   // not our child any more: nothing left to wait for
        if (Clock::now() >= until || (stopOnCancel && Cancelled()))
            return false;
        poll(nullptr, 0, 10);
    }
}

// Ends the child's process group and reaps the child. Returns the exit code (128 + signal when killed).
int ProcessCliRunner::KillAndReap(pid_t pid, bool force)
{
    if (force)
    {
        kill(-pid, SIGTERM);
        WaitExited(pid, Clock::now() + std::chrono::milliseconds(kTermGraceMs), false);
    }
    // The leader is a zombie or still running: either way the group id cannot be reused yet. SIGKILL ends
    // any process left in the group (a stubborn child, or helpers a finished child left behind).
    kill(-pid, SIGKILL);
    return ReapKilledChild(pid, kReapLimitMs);
}

CliRunResult ProcessCliRunner::Run(const std::vector<std::string>& argv, const std::string& stdinText, uint32_t timeoutMs)
{
    CliRunResult result;
    if (argv.empty())
    {
        result.error = "no_executable";
        return result;
    }
    if (Cancelled())
    {
        result.error = "cancelled";
        return result;
    }
    std::string path;
    if (!ResolveExecutable(argv[0], path))
    {
        result.error = "not_found";
        return result;
    }

    int in[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1};
    auto closeAll = [&] {
        for (int* fds : {in, out, err})
        {
            CloseFd(fds[0]);
            CloseFd(fds[1]);
        }
    };
    if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0 || !SetFlags(in[0], false) || !SetFlags(out[1], false) ||
        !SetFlags(err[1], false) || !SetFlags(in[1], true) || !SetFlags(out[0], true) || !SetFlags(err[0], true))
    {
        result.error = std::string("pipe_failed:") + std::strerror(errno);
        closeAll();
        return result;
    }
#if defined(__APPLE__)
    fcntl(in[1], F_SETNOSIGPIPE, 1);   // a child that exits before reading its stdin must not kill the game
#else
    sigset_t pipeMask, oldMask;
    sigemptyset(&pipeMask);
    sigaddset(&pipeMask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipeMask, &oldMask);
#endif

    pid_t pid = -1;
    {
        std::lock_guard guard{m_mutex};
        if (Cancelled())
            result.error = "cancelled";
        else
            pid = Spawn(path, argv, in[0], out[1], err[1], result.error);
    }
    CloseFd(in[0]);
    CloseFd(out[1]);
    CloseFd(err[1]);
    if (pid < 0)
    {
        closeAll();
#if !defined(__APPLE__)
        pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
#endif
        return result;
    }
    result.started = true;

    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string stderrText;
    size_t written = 0;
    if (stdinText.empty())
        CloseFd(in[1]);
    bool cancelled = false;
    char buffer[65536];
    while (out[0] >= 0 || err[0] >= 0)
    {
        if (Cancelled())
        {
            cancelled = true;
            break;
        }
        const int waitMs = RemainingMs(deadline);
        if (waitMs == 0)
        {
            result.timedOut = true;
            break;
        }
        pollfd fds[4];
        int count = 0;
        int inAt = -1, outAt = -1, errAt = -1;
        if (in[1] >= 0)
        {
            inAt = count;
            fds[count++] = pollfd{in[1], POLLOUT, 0};
        }
        if (out[0] >= 0)
        {
            outAt = count;
            fds[count++] = pollfd{out[0], POLLIN, 0};
        }
        if (err[0] >= 0)
        {
            errAt = count;
            fds[count++] = pollfd{err[0], POLLIN, 0};
        }
        if (m_wake[0] >= 0)
            fds[count++] = pollfd{m_wake[0], POLLIN, 0};
        if (poll(fds, static_cast<nfds_t>(count), waitMs) < 0)
        {
            if (errno == EINTR)
                continue;
            result.error = std::string("poll_failed:") + std::strerror(errno);
            cancelled = true;
            break;
        }
        if (inAt >= 0 && fds[inAt].revents != 0)
        {
            const size_t chunk = stdinText.size() - written < sizeof(buffer) ? stdinText.size() - written : sizeof(buffer);
            const ssize_t n = write(in[1], stdinText.data() + written, chunk);
            if (n > 0)
                written += static_cast<size_t>(n);
            if ((n < 0 && errno != EAGAIN && errno != EINTR) || written == stdinText.size())
                CloseFd(in[1]);   // all written, or the child stopped reading
        }
        for (const int at : {outAt, errAt})
        {
            if (at < 0 || fds[at].revents == 0)
                continue;
            int& fd = at == outAt ? out[0] : err[0];
            std::string& sink = at == outAt ? result.output : stderrText;
            const size_t limit = at == outAt ? kMaxOutput : kMaxStderr;
            const ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n > 0)
            {
                if (sink.size() < limit)
                    sink.append(buffer, static_cast<size_t>(n) < limit - sink.size() ? static_cast<size_t>(n) : limit - sink.size());
            }
            else if (n == 0 || (errno != EAGAIN && errno != EINTR))
                CloseFd(fd);
        }
        if (m_wake[0] >= 0)
        {
            while (read(m_wake[0], buffer, sizeof(buffer)) > 0)
            {
            }
        }
    }
    CloseFd(in[1]);

    // Output closed: the child is ending. Wait for it until the deadline, then end its group and reap it.
    if (!cancelled && !result.timedOut && !WaitExited(pid, deadline, true))
        result.timedOut = !Cancelled();
    cancelled = cancelled || Cancelled();
    const int code = KillAndReap(pid, cancelled || result.timedOut);
    closeAll();
#if !defined(__APPLE__)
    // A SIGPIPE raised by a write above stays pending while blocked: take it before unblocking.
    const timespec zero{0, 0};
    while (sigtimedwait(&pipeMask, nullptr, &zero) > 0)
    {
    }
    pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
#endif

    if (cancelled)
    {
        result.exitCode = -1;
        result.error = result.error.empty() ? "cancelled" : result.error;
        return result;
    }
    result.exitCode = result.timedOut ? -1 : code;
    result.error = FirstLine(stderrText);
    return result;
}
#endif
} // namespace

const std::vector<std::string>& CliRemovedEnvironmentNames()
{
    // Keep identical to KEY_VARIABLES + SESSION_VARIABLES in tools/freeplay_play.py (checked by its tests).
    // BEGIN removed-environment
    static const std::vector<std::string> names = {
        "ANTHROPIC_API_KEY",
        "XRAY_AGENT_API_KEY",
        "ANTHROPIC_AUTH_TOKEN",
        "ANTHROPIC_BASE_URL",
        "CLAUDECODE",
        "CLAUDE_PID",
        "CLAUDE_EFFORT",
        "CLAUDE_AGENT_SDK_VERSION",
        "CLAUDE_CODE_SESSION_ID",
        "CLAUDE_CODE_MESSAGING_SOCKET",
        "CLAUDE_CODE_MESSAGING_TOKEN",
        "CLAUDE_CODE_CHILD_SESSION",
        "CLAUDE_CODE_EMIT_SESSION_STATE_EVENTS",
        "CLAUDE_CODE_ENTRYPOINT",
        "CLAUDE_CODE_SESSION_ATTENDED",
        "CLAUDE_CODE_ARTIFACT",
        "CLAUDE_CODE_EXECPATH",
    };
    // END removed-environment
    return names;
}

std::vector<std::string> CliChildEnvironment(const std::vector<std::string>& parent)
{
    const std::vector<std::string>& removed = CliRemovedEnvironmentNames();
    std::vector<std::string> env;
    for (const std::string& entry : parent)
    {
        const std::string name = entry.substr(0, entry.find('='));
        if (entry.find('=') != std::string::npos && std::find(removed.begin(), removed.end(), name) != removed.end())
            continue;
        env.push_back(entry);
    }
    return env;
}

void SetCliWarningSink(CliWarningSink sink) { g_warningSink.store(sink, std::memory_order_release); }

#if !defined(_WIN32)
int ReapKilledChild(int pid, uint32_t limitMs)
{
    const Clock::time_point until = Clock::now() + std::chrono::milliseconds(limitMs);
    for (;;)
    {
        int status = 0;
        const pid_t rc = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
        if (rc == static_cast<pid_t>(pid))
        {
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            if (WIFSIGNALED(status))
                return 128 + WTERMSIG(status);
            return -1;
        }
        if (rc < 0 && errno != EINTR)
            return -1;   // not this process's child (already reaped)
        if (Clock::now() >= until)
            break;
        poll(nullptr, 0, 10);
    }
    Warn("claude_cli: child " + std::to_string(pid) + " did not exit within " + std::to_string(limitMs) +
        " ms after SIGKILL; left unreaped");
    return -1;
}

std::unique_ptr<ICliRunner> CreateProcessCliRunner() { return std::make_unique<ProcessCliRunner>(); }

bool IsCliExecutableAvailable(const std::string& pathOrName)
{
    std::string resolved;
    return ResolveExecutable(pathOrName, resolved);
}
#else
namespace
{
class UnsupportedCliRunner final : public ICliRunner
{
public:
    CliRunResult Run(const std::vector<std::string>&, const std::string&, uint32_t) override
    {
        CliRunResult result;
        result.error = "unsupported_platform";
        return result;
    }
    void Cancel() override {}
};
} // namespace

std::unique_ptr<ICliRunner> CreateProcessCliRunner() { return std::make_unique<UnsupportedCliRunner>(); }
bool IsCliExecutableAvailable(const std::string&) { return false; }
#endif

std::vector<std::string> BuildClaudeCliArguments(const ClaudeCliConfig& config)
{
    return {config.executable, "-p", "--model", config.model, "--tools", "", "--no-session-persistence",
        "--strict-mcp-config", "--setting-sources", "", "--system-prompt", kSystemPrompt};
}

std::string ExtractIntentBlock(const std::string& text)
{
    std::string block;
    bool inBlock = false;
    size_t begin = 0;
    while (begin < text.size())
    {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = Trimmed(text.substr(begin, end - begin));
        begin = end + 1;
        if (!inBlock)
        {
            if (line != kIntentHeader)
                continue;
            inBlock = true;
        }
        block += line + "\n";
        if (line == "end")
            return block;
    }
    return std::string();
}

ClaudeCliActorIntentProvider::ClaudeCliActorIntentProvider(const ClaudeCliConfig& config, std::unique_ptr<ICliRunner> runner)
    : m_config(config), m_runner(std::move(runner))
{
}

ActorProviderResult ClaudeCliActorIntentProvider::Wake(const ActorWakeContext& context)
{
    if (!m_runner)
        return CoastCli(m_config, "cli_unavailable", "no_runner");
    const CliRunResult run = m_runner->Run(BuildClaudeCliArguments(m_config), context.prompt, m_config.timeoutMs);
    if (!run.started)
        return CoastCli(m_config, "cli_unavailable", run.error);
    if (run.timedOut)
        return CoastCli(m_config, "cli_timeout");
    if (run.exitCode != 0)
        return CoastCli(m_config, "cli_exit",
            "exit=" + std::to_string(run.exitCode) + (run.error.empty() ? std::string() : " " + run.error));

    const std::string block = ExtractIntentBlock(run.output);
    if (block.empty())
        return FailCli(m_config, "no_intent_block");
    const ActorIntentParseResult parsed = ParseActorIntentPlan(block);
    if (!parsed.ok)
        return FailCli(m_config, parsed.reason);

    ActorProviderResult result;
    result.ok = true;
    result.coast = parsed.plan.coast;
    result.provider = "claude_cli";
    result.model = m_config.model;
    result.plan = parsed.plan;
    return result;
}

void ClaudeCliActorIntentProvider::Cancel()
{
    if (m_runner)
        m_runner->Cancel();
}
} // namespace xrSim
