# Agent Bridge Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `9162c534e`, `2a155131f`, `70e9183cf`. Historical Downloads paths are superseded by the private-runtime setup; unchecked boxes do not imply missing code. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Unix-socket control channel (`-agent_bridge`) letting a coding agent drive the running game — console commands, Lua eval, synthetic input, state queries, screenshots — for autonomous verification.

**Architecture:** `CAgentBridge` in xrEngine: a socket thread reads newline-delimited requests into a mutex-guarded queue; `CRenderDevice::ProcessFrame` drains the queue on the main thread each frame and queues responses back to the socket thread. Spec: `docs/superpowers/specs/2026-07-04-agent-bridge-design.md`.

**Tech Stack:** POSIX Unix domain sockets, std::thread, SDL2 event injection, Lua C API via `GEnv.ScriptEngine->lua()`, Python 3 stdlib client.

## Global Constraints

- macOS/POSIX only: all bridge code inside `#ifndef XR_PLATFORM_WINDOWS` (files still compile on Windows to empty stubs).
- No engine behavior change when `-agent_bridge` is absent (zero threads, zero allocations).
- Teardown must be bounded (this codebase's hardening rule): shutdown flag + `close()` of the listen fd wakes the thread; `poll` timeout is 100 ms, so join completes in ≤ ~200 ms.
- There is no unit-test framework in this repo. Verification = compile + scripted runtime checks with `nc -U` / `agentctl.py` against a menu-only engine launch (fast, no game data interaction needed until the final acceptance task).
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- Run game commands from the CoC dir: `cd "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl" && DYLD_LIBRARY_PATH=/Users/rz/OpenXVibeRay/bin/arm64/Release /Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da <flags>`
- Build: `cmake --build /Users/rz/OpenXVibeRay/build --target xrEngine xr_3da -j10`

## Verb reference (from the spec)

`hello`, `cmd <console line>`, `lua <chunk>`, `key <sdl_key_name> down|up|tap`, `mouse move <dx> <dy>` / `mouse btn <left|right|middle> down|up|tap`, `state`, `shot <name>`, `bye`. Responses: `<id> ok [payload]` / `<id> err <message>`, `\n` in payloads escaped as `\\n`.

---

### Task 1: CAgentBridge core — socket thread, queue, frame drain, `hello`/`bye`

**Files:**
- Create: `src/xrEngine/AgentBridge.h`
- Create: `src/xrEngine/AgentBridge.cpp`
- Modify: `src/xrEngine/CMakeLists.txt` (add the two files to the existing `target_sources` list, near `Device_*.cpp` entries)
- Modify: `src/xrEngine/x_ray.cpp` (create/destroy)
- Modify: `src/xrEngine/device.cpp` (`CRenderDevice::ProcessFrame` drain hook)

**Interfaces:**
- Produces: global `CAgentBridge* g_agent_bridge` (null when disabled); `CAgentBridge::Initialize()` (reads `-agent_bridge` from `Core.Params`), `CAgentBridge::Destroy()`, `void CAgentBridge::OnFrame()` (main-thread drain; later tasks add verb handlers inside its switch).
- Consumes: `Core.Params` (xrCore), `FS.update_path` for `$app_data_root$`.

- [ ] **Step 1: Write AgentBridge.h**

```cpp
#pragma once
/*
 * Agent bridge: Unix-socket control channel for autonomous engine testing.
 * Active only with -agent_bridge [socket_path]. One client at a time.
 * Protocol v1: "<id> <verb> [payload]\n" -> "<id> ok [payload]\n" | "<id> err <msg>\n"
 * Spec: docs/superpowers/specs/2026-07-04-agent-bridge-design.md
 */
#include "xrCore/xrCore.h"

#ifndef XR_PLATFORM_WINDOWS
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <deque>

class ENGINE_API CAgentBridge
{
public:
    static void Initialize(); // creates g_agent_bridge if -agent_bridge present
    static void Destroy();

    void OnFrame(); // main thread: drain requests, execute verbs, queue responses

private:
    CAgentBridge(const char* socketPath);
    ~CAgentBridge();

    void SocketThreadProc();
    void HandleRequest(const std::string& line); // main thread
    void Respond(const std::string& id, bool ok, const std::string& payload);

    // verb handlers (main thread) — filled in by later tasks
    std::string VerbCmd(const std::string& payload, bool& ok);
    std::string VerbLua(const std::string& payload, bool& ok);
    std::string VerbKey(const std::string& payload, bool& ok);
    std::string VerbMouse(const std::string& payload, bool& ok);
    std::string VerbState(bool& ok);
    std::string VerbShot(const std::string& payload, bool& ok);

    string_path m_socketPath;
    int m_listenFd = -1;
    std::atomic_bool m_shutdown{ false };
    std::thread m_thread;

    std::mutex m_lock; // guards both queues
    std::deque<std::string> m_requests;   // socket thread -> main thread
    std::deque<std::string> m_responses;  // main thread -> socket thread

    // deferred key/mouse "tap" releases, flushed next OnFrame (Task 4)
    std::deque<u32> m_pendingKeyUps;   // SDL keycodes
    std::deque<u8> m_pendingBtnUps;    // SDL button ids
};

extern ENGINE_API CAgentBridge* g_agent_bridge;
#else
class ENGINE_API CAgentBridge
{
public:
    static void Initialize() {}
    static void Destroy() {}
    void OnFrame() {}
};
extern ENGINE_API CAgentBridge* g_agent_bridge;
#endif
```

- [ ] **Step 2: Write AgentBridge.cpp — lifecycle, socket thread, protocol framing, `hello`/`bye`**

```cpp
#include "stdafx.h"
#include "AgentBridge.h"

CAgentBridge* g_agent_bridge = nullptr;

#ifndef XR_PLATFORM_WINDOWS
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

static int s_clientFd = -1; // owned by socket thread only

void CAgentBridge::Initialize()
{
    pcstr opt = strstr(Core.Params, "-agent_bridge");
    if (!opt || g_agent_bridge)
        return;

    string_path path{};
    // optional explicit path after the flag
    if (1 != sscanf(opt + xr_strlen("-agent_bridge"), " %s", path) || path[0] == '-')
        FS.update_path(path, "$app_data_root$", "agent_bridge.sock");

    g_agent_bridge = xr_new<CAgentBridge>(path);
}

void CAgentBridge::Destroy() { xr_delete(g_agent_bridge); }

CAgentBridge::CAgentBridge(const char* socketPath)
{
    xr_strcpy(m_socketPath, socketPath);
    ::unlink(m_socketPath); // stale socket from a crashed run

    m_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    R_ASSERT2(m_listenFd >= 0, "agent_bridge: socket() failed");

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    xr_strcpy(addr.sun_path, m_socketPath);
    R_ASSERT2(0 == ::bind(m_listenFd, (sockaddr*)&addr, sizeof(addr)), m_socketPath);
    R_ASSERT2(0 == ::listen(m_listenFd, 1), "agent_bridge: listen() failed");

    m_thread = std::thread([this] { SocketThreadProc(); });
    Msg("* agent_bridge: listening on %s", m_socketPath);
}

CAgentBridge::~CAgentBridge()
{
    m_shutdown.store(true, std::memory_order_release);
    if (m_listenFd >= 0)
        ::close(m_listenFd); // wakes poll with POLLNVAL/error
    if (m_thread.joinable())
        m_thread.join(); // bounded: poll timeout is 100ms
    ::unlink(m_socketPath);
    Msg("* agent_bridge: closed");
}

void CAgentBridge::SocketThreadProc()
{
    std::string inbuf;
    while (!m_shutdown.load(std::memory_order_acquire))
    {
        if (s_clientFd < 0) // accept phase
        {
            pollfd p{ m_listenFd, POLLIN, 0 };
            if (::poll(&p, 1, 100) <= 0)
                continue;
            s_clientFd = ::accept(m_listenFd, nullptr, nullptr);
            inbuf.clear();
            continue;
        }

        // flush queued responses (single writer: this thread)
        for (;;)
        {
            std::string out;
            {
                std::lock_guard guard{ m_lock };
                if (m_responses.empty())
                    break;
                out = std::move(m_responses.front());
                m_responses.pop_front();
            }
            if (::write(s_clientFd, out.c_str(), out.size()) < 0)
            {
                ::close(s_clientFd);
                s_clientFd = -1;
                break;
            }
        }
        if (s_clientFd < 0)
            continue;

        pollfd p{ s_clientFd, POLLIN, 0 };
        const int rc = ::poll(&p, 1, 20); // short: responses need timely flushing
        if (rc <= 0)
            continue;

        char chunk[4096];
        const ssize_t n = ::read(s_clientFd, chunk, sizeof(chunk));
        if (n <= 0) // disconnect: drop pending work, re-accept
        {
            ::close(s_clientFd);
            s_clientFd = -1;
            std::lock_guard guard{ m_lock };
            m_requests.clear();
            m_responses.clear();
            continue;
        }
        inbuf.append(chunk, size_t(n));

        size_t nl;
        while ((nl = inbuf.find('\n')) != std::string::npos)
        {
            std::string line = inbuf.substr(0, nl);
            inbuf.erase(0, nl + 1);
            if (line.empty())
                continue;
            std::lock_guard guard{ m_lock };
            m_requests.push_back(std::move(line));
        }
    }
    if (s_clientFd >= 0)
    {
        ::close(s_clientFd);
        s_clientFd = -1;
    }
}

static std::string escape_payload(std::string s)
{
    size_t pos = 0;
    while ((pos = s.find('\n', pos)) != std::string::npos)
    {
        s.replace(pos, 1, "\\n");
        pos += 2;
    }
    return s;
}

void CAgentBridge::Respond(const std::string& id, bool ok, const std::string& payload)
{
    std::string line = id + (ok ? " ok" : " err");
    if (!payload.empty())
        line += " " + escape_payload(payload);
    line += "\n";
    std::lock_guard guard{ m_lock };
    m_responses.push_back(std::move(line));
}

void CAgentBridge::OnFrame()
{
    // Task 4 adds deferred tap-release flushing here.
    for (;;)
    {
        std::string line;
        {
            std::lock_guard guard{ m_lock };
            if (m_requests.empty())
                return;
            line = std::move(m_requests.front());
            m_requests.pop_front();
        }
        HandleRequest(line);
    }
}

void CAgentBridge::HandleRequest(const std::string& line)
{
    const size_t sp1 = line.find(' ');
    const std::string id = line.substr(0, sp1);
    if (sp1 == std::string::npos)
    {
        Respond(id, false, "missing verb");
        return;
    }
    const size_t sp2 = line.find(' ', sp1 + 1);
    const std::string verb = line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
    const std::string payload = sp2 == std::string::npos ? "" : line.substr(sp2 + 1);

    bool ok = true;
    std::string result;
    if (verb == "hello")
        result = "agent_bridge v1 " + std::string(Core.ApplicationName);
    else if (verb == "bye")
    {
        Respond(id, true, "");
        // socket thread notices the client closing; nothing to do here
        return;
    }
    else if (verb == "cmd")   result = VerbCmd(payload, ok);
    else if (verb == "lua")   result = VerbLua(payload, ok);
    else if (verb == "key")   result = VerbKey(payload, ok);
    else if (verb == "mouse") result = VerbMouse(payload, ok);
    else if (verb == "state") result = VerbState(ok);
    else if (verb == "shot")  result = VerbShot(payload, ok);
    else { ok = false; result = "unknown verb: " + verb; }

    Respond(id, ok, result);
}

// ---- verb stubs: implemented in Tasks 2-5 ----
std::string CAgentBridge::VerbCmd(const std::string&, bool& ok)   { ok = false; return "not implemented"; }
std::string CAgentBridge::VerbLua(const std::string&, bool& ok)   { ok = false; return "not implemented"; }
std::string CAgentBridge::VerbKey(const std::string&, bool& ok)   { ok = false; return "not implemented"; }
std::string CAgentBridge::VerbMouse(const std::string&, bool& ok) { ok = false; return "not implemented"; }
std::string CAgentBridge::VerbState(bool& ok)                     { ok = false; return "not implemented"; }
std::string CAgentBridge::VerbShot(const std::string&, bool& ok)  { ok = false; return "not implemented"; }

#else // XR_PLATFORM_WINDOWS
CAgentBridge* g_agent_bridge = nullptr;
#endif
```

- [ ] **Step 3: Wire creation/destruction in x_ray.cpp**

In `CApplication::CApplication`, immediately AFTER the console is created and executed (find `Console->Execute("stat_memory")` or the `createConsole()`/config block around line 163) add:

```cpp
    CAgentBridge::Initialize(); // no-op without -agent_bridge
```

In `CApplication::~CApplication`, BEFORE `destroyConsole()` (bridge verbs use Console) add:

```cpp
    CAgentBridge::Destroy();
```

Add `#include "AgentBridge.h"` to x_ray.cpp includes.

- [ ] **Step 4: Wire per-frame drain in device.cpp**

In `CRenderDevice::ProcessFrame()` (device.cpp — the function calling `FrameMove`/`DoRender`), at its very start add:

```cpp
    if (g_agent_bridge)
        g_agent_bridge->OnFrame();
```

Add `#include "AgentBridge.h"` to device.cpp includes.

- [ ] **Step 5: Add files to src/xrEngine/CMakeLists.txt**

Find the `target_sources` block listing `Device_create.cpp` etc.; add alphabetically:

```
    AgentBridge.cpp
    AgentBridge.h
```

- [ ] **Step 6: Build**

Run: `cmake --build /Users/rz/OpenXVibeRay/build --target xr_3da -j10 2>&1 | grep -E 'error|Built target' | tail -3`
Expected: `Built target xr_3da`, no errors.

- [ ] **Step 7: Runtime check — hello/bye/unknown**

```bash
cd "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl"
(DYLD_LIBRARY_PATH=/Users/rz/OpenXVibeRay/bin/arm64/Release /Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge &>/dev/null &)
sleep 25   # engine boot
SOCK="appdata/agent_bridge.sock"
printf '1 hello\n2 nope\n3 bye\n' | nc -U "$SOCK"
```

Expected output:
```
1 ok agent_bridge v1 <name>
2 err unknown verb: nope
3 ok
```

Then `pkill -TERM -f xr_3da` and verify the socket file is gone: `test -S "$SOCK" || echo removed`.

- [ ] **Step 8: Commit**

```bash
git add src/xrEngine/AgentBridge.h src/xrEngine/AgentBridge.cpp src/xrEngine/CMakeLists.txt \
        src/xrEngine/x_ray.cpp src/xrEngine/device.cpp
git commit -m "feat(bridge): agent bridge core — socket thread, frame drain, hello/bye

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: `cmd` verb — console execution with output capture

**Files:**
- Modify: `src/xrEngine/AgentBridge.cpp` (replace `VerbCmd` stub)

**Interfaces:**
- Consumes: `Console->Execute(pcstr)` (global `CConsole* Console`), `SetLogCB(const LogCallback&)` from `xrCore/log.h` (returns previous callback; `LogCallback{Func, void* ctx}` where `Func = void(*)(void* ctx, pcstr line)` — check the exact typedef in log.h and match it).
- Produces: `cmd <line>` → `ok <captured log output>` (log lines emitted during Execute, joined with `\n`, escaped by Respond).

- [ ] **Step 1: Implement VerbCmd**

```cpp
#include "XR_IOConsole.h" // CConsole, global Console — add to AgentBridge.cpp includes

namespace
{
    std::string* s_capture = nullptr;
    void capture_log(void* /*ctx*/, pcstr line)
    {
        if (s_capture && line)
        {
            if (!s_capture->empty())
                *s_capture += '\n';
            *s_capture += line;
        }
    }
} // namespace

std::string CAgentBridge::VerbCmd(const std::string& payload, bool& ok)
{
    if (payload.empty()) { ok = false; return "empty command"; }
    if (!Console)        { ok = false; return "console not ready"; }

    std::string captured;
    s_capture = &captured;
    const LogCallback prev = SetLogCB(LogCallback(capture_log, nullptr));
    Console->Execute(payload.c_str());
    SetLogCB(prev);
    s_capture = nullptr;
    return captured;
}
```

(If `LogCallback::Func` takes only `pcstr` — no ctx — adapt `capture_log` to that signature; read `src/xrCore/log.h:29-42` first.)

- [ ] **Step 2: Build** — same command as Task 1 Step 6, expect clean.

- [ ] **Step 3: Runtime check**

Launch as in Task 1 Step 7, then:

```bash
printf '1 cmd vid_mode\n' | nc -U "$SOCK"
```

Expected: `1 ok` followed by the console's response about the current vid_mode (e.g. contains `vid_mode`). Also test an invalid command: `printf '2 cmd not_a_command\n' | nc -U "$SOCK"` → `2 ok Unknown command` (console reports unknown via log — that's correct behavior: `cmd` succeeded, the console line is the payload).

- [ ] **Step 4: Commit**

```bash
git add src/xrEngine/AgentBridge.cpp
git commit -m "feat(bridge): cmd verb — console execution with log capture

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: `lua` verb — eval with results

**Files:**
- Modify: `src/xrEngine/AgentBridge.cpp` (replace `VerbLua` stub)

**Interfaces:**
- Consumes: `GEnv.ScriptEngine` (`CScriptEngine*`, from `Include/xrAPI/xrAPI.h` — xrEngine already links xrScriptEngine), `GEnv.ScriptEngine->lua()` → `lua_State*`. Lua C API headers: `#include <lua.hpp>` (LuaJIT — check how `src/xrScriptEngine/script_engine.hpp` includes lua and mirror it).
- Produces: `lua <chunk>` → `ok <tostring of results, tab-separated>`; tables dumped one level as `{k=v, ...}`.

- [ ] **Step 1: Implement VerbLua**

```cpp
#include "xrScriptEngine/script_engine.hpp" // GEnv.ScriptEngine — add to includes
#include <lua.hpp>

namespace
{
    // shallow stringification of the value at stack index i
    std::string lua_value_to_string(lua_State* L, int i)
    {
        if (lua_istable(L, i))
        {
            std::string out = "{";
            lua_pushnil(L);
            bool first = true;
            while (lua_next(L, i < 0 ? i - 1 : i))
            {
                if (!first) out += ", ";
                first = false;
                // key (string keys verbatim; others via tostring on a COPY —
                // lua_tostring on the live key would corrupt lua_next iteration)
                if (lua_type(L, -2) == LUA_TSTRING)
                    out += lua_tostring(L, -2);
                else
                {
                    lua_pushvalue(L, -2);
                    pcstr ks = lua_tostring(L, -1);
                    out += ks ? ks : luaL_typename(L, -1);
                    lua_pop(L, 1);
                }
                out += "=";
                // value (no recursion)
                pcstr vs = lua_tostring(L, -1);
                out += vs ? vs : luaL_typename(L, -1);
                lua_pop(L, 1);
            }
            out += "}";
            return out;
        }
        pcstr s = lua_tostring(L, i);
        return s ? s : luaL_typename(L, i);
    }
} // namespace

std::string CAgentBridge::VerbLua(const std::string& payload, bool& ok)
{
    if (!GEnv.ScriptEngine) { ok = false; return "script engine not ready"; }
    lua_State* L = GEnv.ScriptEngine->lua();
    const int base = lua_gettop(L);

    // try as expression first ("return <payload>"), fall back to statement chunk
    std::string expr = "return " + payload;
    int rc = luaL_loadstring(L, expr.c_str());
    if (rc != 0)
    {
        lua_pop(L, 1);
        rc = luaL_loadstring(L, payload.c_str());
    }
    if (rc != 0)
    {
        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
        lua_settop(L, base);
        ok = false;
        return err;
    }

    rc = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (rc != 0)
    {
        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "runtime error";
        lua_settop(L, base);
        ok = false;
        return err;
    }

    std::string out;
    const int nresults = lua_gettop(L) - base;
    for (int i = 1; i <= nresults; ++i)
    {
        if (i > 1) out += "\t";
        out += lua_value_to_string(L, base + i);
    }
    lua_settop(L, base);
    return out;
}
```

**Note for implementer:** the table-key else-branch above is deliberately conservative; simplify to `lua_typename` labels if `luaL_tolstring` isn't available in this LuaJIT. Keep the stack balanced (`lua_settop(L, base)` on every exit path) — an unbalanced stack corrupts the game's script engine.

- [ ] **Step 2: Build** — expect clean. If `lua.hpp` isn't found, check how `script_engine.hpp` includes Lua (likely `<lua.hpp>` via LuaJIT include dir already on xrEngine's include path through the xrScriptEngine link; otherwise add `target_include_directories` entry matching xrScriptEngine's).

- [ ] **Step 3: Runtime check**

```bash
printf '1 lua 2+2\n2 lua device():time_global()\n3 lua {a=1,b="x"}\n4 lua error("boom")\n' | nc -U "$SOCK"
```

Expected: `1 ok 4`, `2 ok <number>`, `3 ok {a=1, b=x}`, `4 err ...boom`.

- [ ] **Step 4: Commit**

```bash
git add src/xrEngine/AgentBridge.cpp
git commit -m "feat(bridge): lua verb — eval with stringified results

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: `key` and `mouse` verbs — synthetic SDL input

**Files:**
- Modify: `src/xrEngine/AgentBridge.cpp` (replace `VerbKey`/`VerbMouse` stubs, extend `OnFrame`)

**Interfaces:**
- Consumes: `SDL_PushEvent`, `SDL_GetKeyFromName`, `SDL_GetScancodeFromKey`, `Device.m_sdlWnd` (window id via `SDL_GetWindowID`). The engine drains events via `SDL_PeepEvents(SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYMAPCHANGED)` (`xr_input.cpp:253`) — pushed events land in the same queue.
- Produces: `key w down|up|tap`, `mouse move 10 0`, `mouse btn left tap`.

- [ ] **Step 1: Implement key/mouse event builders and tap deferral**

```cpp
namespace
{
    void push_key_event(SDL_Keycode kc, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.timestamp = SDL_GetTicks();
        e.key.windowID = SDL_GetWindowID(Device.m_sdlWnd);
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.sym = kc;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(kc);
        SDL_PushEvent(&e);
    }

    void push_btn_event(u8 button, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.timestamp = SDL_GetTicks();
        e.button.windowID = SDL_GetWindowID(Device.m_sdlWnd);
        e.button.button = button;
        e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
        SDL_PushEvent(&e);
    }
} // namespace

std::string CAgentBridge::VerbKey(const std::string& payload, bool& ok)
{
    char name[64]{}, action[16]{};
    if (2 != sscanf(payload.c_str(), "%63s %15s", name, action))
    { ok = false; return "usage: key <sdl_key_name> down|up|tap"; }

    const SDL_Keycode kc = SDL_GetKeyFromName(name);
    if (kc == SDLK_UNKNOWN) { ok = false; return std::string("unknown key: ") + name; }

    if (0 == xr_strcmp(action, "down"))      push_key_event(kc, true);
    else if (0 == xr_strcmp(action, "up"))   push_key_event(kc, false);
    else if (0 == xr_strcmp(action, "tap"))  { push_key_event(kc, true); m_pendingKeyUps.push_back(u32(kc)); }
    else { ok = false; return std::string("unknown action: ") + action; }
    return "";
}

std::string CAgentBridge::VerbMouse(const std::string& payload, bool& ok)
{
    char sub[16]{};
    if (1 != sscanf(payload.c_str(), "%15s", sub)) { ok = false; return "usage: mouse move|btn ..."; }

    if (0 == xr_strcmp(sub, "move"))
    {
        int dx = 0, dy = 0;
        if (2 != sscanf(payload.c_str(), "%*s %d %d", &dx, &dy))
        { ok = false; return "usage: mouse move <dx> <dy>"; }
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.timestamp = SDL_GetTicks();
        e.motion.windowID = SDL_GetWindowID(Device.m_sdlWnd);
        e.motion.xrel = dx;
        e.motion.yrel = dy;
        SDL_PushEvent(&e);
        return "";
    }
    if (0 == xr_strcmp(sub, "btn"))
    {
        char which[16]{}, action[16]{};
        if (2 != sscanf(payload.c_str(), "%*s %15s %15s", which, action))
        { ok = false; return "usage: mouse btn <left|right|middle> down|up|tap"; }
        u8 b = SDL_BUTTON_LEFT;
        if (0 == xr_strcmp(which, "right")) b = SDL_BUTTON_RIGHT;
        else if (0 == xr_strcmp(which, "middle")) b = SDL_BUTTON_MIDDLE;
        else if (0 != xr_strcmp(which, "left")) { ok = false; return "unknown button"; }

        if (0 == xr_strcmp(action, "down"))     push_btn_event(b, true);
        else if (0 == xr_strcmp(action, "up"))  push_btn_event(b, false);
        else if (0 == xr_strcmp(action, "tap")) { push_btn_event(b, true); m_pendingBtnUps.push_back(b); }
        else { ok = false; return "unknown action"; }
        return "";
    }
    ok = false;
    return "usage: mouse move|btn ...";
}
```

- [ ] **Step 2: Flush deferred tap releases at the top of OnFrame**

Replace the `// Task 4 adds...` comment in `OnFrame()` with:

```cpp
    // taps: the down was pushed last frame; release now so the game saw one full frame held
    while (!m_pendingKeyUps.empty())
    {
        push_key_event(SDL_Keycode(m_pendingKeyUps.front()), false);
        m_pendingKeyUps.pop_front();
    }
    while (!m_pendingBtnUps.empty())
    {
        push_btn_event(m_pendingBtnUps.front(), false);
        m_pendingBtnUps.pop_front();
    }
```

Add `#include <SDL.h>` and `#include "device.h"` to AgentBridge.cpp includes if not already pulled in via stdafx.

- [ ] **Step 3: Build** — expect clean.

- [ ] **Step 4: Runtime check** (menu is fine — Escape opens/closes menus, arrows navigate):

```bash
printf '1 key escape tap\n' | nc -U "$SOCK"; sleep 1
printf '2 lua tostring(main_menu ~= nil)\n' | nc -U "$SOCK"
```

Expected: `1 ok`, and the game visibly reacts (menu state toggles; exact assertion comes with `state` in Task 5). No crash, no error in the game log.

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/AgentBridge.cpp
git commit -m "feat(bridge): key/mouse verbs — synthetic SDL input injection

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: `state` and `shot` verbs

**Files:**
- Modify: `src/xrEngine/AgentBridge.cpp` (replace `VerbState`/`VerbShot` stubs)

**Interfaces:**
- Consumes: `Device.dwWidth/dwHeight/fTimeGlobal`, `Device.GetStats()` (check `IPerformanceAlert`/stats interface in device.h for the fps field — if awkward, compute fps as `1.f / Device.fTimeDelta`), `g_pGameLevel` (`IGame_Level*`, null in menu), the Lua path from Task 3 for actor data, `GEnv.Render->Screenshot(IRender::SM_NORMAL, name)`.
- Produces: `state` → `ok scene=<menu|game> level=<name> fps=<n> pos=<x,y,z> hp=<0..1> time=<ms>`; `shot <name>` → `ok <name>` (screenshot lands in `$screenshots$`).

- [ ] **Step 1: Implement VerbState**

```cpp
std::string CAgentBridge::VerbState(bool& ok)
{
    std::string out;
    string256 buf;
    const float fps = Device.fTimeDelta > EPS ? 1.f / Device.fTimeDelta : 0.f;
    xr_sprintf(buf, "scene=%s fps=%.0f frame=%u", g_pGameLevel ? "game" : "menu", fps, Device.dwFrame);
    out = buf;

    if (g_pGameLevel && GEnv.ScriptEngine)
    {
        bool luaOk = true;
        // one round-trip into Lua for the actor block; db.actor is nil while loading
        const std::string actor = VerbLua(
            "db and db.actor and string.format('level=%s pos=%.1f,%.1f,%.1f hp=%.2f time=%d', "
            "level.name(), db.actor:position().x, db.actor:position().y, db.actor:position().z, "
            "db.actor.health, game.time()) or 'actor=nil'", luaOk);
        out += " ";
        out += luaOk ? actor : "actor=err";
    }
    return out;
}
```

- [ ] **Step 2: Implement VerbShot**

```cpp
std::string CAgentBridge::VerbShot(const std::string& payload, bool& ok)
{
    if (payload.empty()) { ok = false; return "usage: shot <name>"; }
    if (!GEnv.Render)    { ok = false; return "renderer not ready"; }
    GEnv.Render->Screenshot(IRender::SM_NORMAL, payload.c_str());
    return payload;
}
```

(Verify the enum spelling at `src/xrEngine/Render.h:148` — `ScreenshotMode`, value `SM_NORMAL` — and adjust if the enumerator is namespaced as `IRender::SM_NORMAL`.)

- [ ] **Step 3: Build** — expect clean.

- [ ] **Step 4: Runtime check** (menu):

```bash
printf '1 state\n2 shot bridge_test\n' | nc -U "$SOCK"
sleep 2 && find "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl/appdata" -name '*bridge_test*' -newer /tmp -mmin -1
```

Expected: `1 ok scene=menu fps=<n> frame=<n>`, `2 ok bridge_test`, and a screenshot file exists (locate the `$screenshots$` dir from fsgame.ltx if the find misses).

- [ ] **Step 5: Commit**

```bash
git add src/xrEngine/AgentBridge.cpp
git commit -m "feat(bridge): state and shot verbs

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: `tools/agentctl.py` client

**Files:**
- Create: `tools/agentctl.py`

**Interfaces:**
- Consumes: the v1 protocol.
- Produces: CLI — `agentctl.py <socket> <verb> [payload...]` (single request) and `agentctl.py <socket> --script <file>` (one request per line, `# comments` and `sleep <s>` directives allowed). Exit code 0 iff every response was `ok`.

- [ ] **Step 1: Write the client**

```python
#!/usr/bin/env python3
"""Agent-bridge client for OpenXVibeRay. Protocol v1 (see
docs/superpowers/specs/2026-07-04-agent-bridge-design.md)."""
import socket
import sys
import time


class Bridge:
    def __init__(self, path: str, timeout: float = 10.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buf = b""
        self.next_id = 1

    def request(self, verb: str, payload: str = "") -> tuple[bool, str]:
        rid = str(self.next_id)
        self.next_id += 1
        line = f"{rid} {verb} {payload}".strip() + "\n"
        self.sock.sendall(line.encode())
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                resp = self.buf[:nl].decode()
                self.buf = self.buf[nl + 1:]
                parts = resp.split(" ", 2)
                if parts[0] != rid:
                    continue  # stale response from a previous client — skip
                ok = len(parts) > 1 and parts[1] == "ok"
                payload_out = parts[2] if len(parts) > 2 else ""
                return ok, payload_out.replace("\\n", "\n")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed the connection")
            self.buf += chunk


def run_script(bridge: Bridge, path: str) -> int:
    failures = 0
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("sleep "):
            time.sleep(float(line.split()[1]))
            continue
        verb, _, payload = line.partition(" ")
        ok, out = bridge.request(verb, payload)
        status = "ok " if ok else "ERR"
        print(f"[{status}] {line} -> {out}")
        if not ok:
            failures += 1
    return failures


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        print("usage: agentctl.py <socket> <verb> [payload...] | <socket> --script <file>")
        return 2
    bridge = Bridge(sys.argv[1])
    if sys.argv[2] == "--script":
        return 1 if run_script(bridge, sys.argv[3]) else 0
    ok, out = bridge.request(sys.argv[2], " ".join(sys.argv[3:]))
    print(out)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Syntax check without the game**

Run: `python3 -c "import ast; ast.parse(open('tools/agentctl.py').read())" && echo parse-ok`
Expected: `parse-ok`.

- [ ] **Step 3: Runtime check against a live menu instance**

```bash
python3 tools/agentctl.py "$SOCK" hello
python3 tools/agentctl.py "$SOCK" state
```

Expected: version line; state line; exit code 0 (`echo $?`).

- [ ] **Step 4: Commit**

```bash
chmod +x tools/agentctl.py
git add tools/agentctl.py
git commit -m "feat(bridge): agentctl.py client with script mode

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Acceptance — scripted in-game session

**Files:**
- Create: `tools/bridge_acceptance.txt` (the spec's acceptance loop as an agentctl script)
- Modify: `docs/macos-dev-setup.md` (short "Agent bridge" section: flag, socket path, agentctl usage, protocol pointer)

**Interfaces:**
- Consumes: everything from Tasks 1–6, CoC install with save `1` (l05_bar).

- [ ] **Step 1: Write tools/bridge_acceptance.txt**

```
# Agent bridge acceptance: load save, verify movement, screenshot, quit.
hello
state
cmd start server(1/single/alife/load) client(localhost)
sleep 45
state
lua db.actor:position().x .. "," .. db.actor:position().z
key w down
sleep 2
key w up
lua db.actor:position().x .. "," .. db.actor:position().z
shot bridge_acceptance
sleep 2
cmd quit
```

- [ ] **Step 2: Run it end-to-end**

```bash
cd "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl"
(DYLD_LIBRARY_PATH=/Users/rz/OpenXVibeRay/bin/arm64/Release /Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge &>/dev/null &)
sleep 25
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py appdata/agent_bridge.sock --script /Users/rz/OpenXVibeRay/tools/bridge_acceptance.txt
```

Expected: every line `[ok ]`; the two position lines DIFFER (actor moved); screenshot file exists; the process exits after `cmd quit` (verify `pgrep xr_3da` empty — clean-exit hardening means no husk). This validates: menu boot → save load through the bridge → real input moves the actor → observation — the full loop.

- [ ] **Step 3: Document in docs/macos-dev-setup.md**

Append a section:

```markdown
## Agent bridge (autonomous testing)

Launch with `-agent_bridge [socket_path]` (default: `<appdata>/agent_bridge.sock`).
Drive it with `tools/agentctl.py <socket> <verb> [payload]` or `--script <file>`.
Verbs: hello, cmd, lua, key, mouse, state, shot, bye — protocol in
`docs/superpowers/specs/2026-07-04-agent-bridge-design.md`.
Example: `python3 tools/agentctl.py appdata/agent_bridge.sock lua 'db.actor.health'`
```

- [ ] **Step 4: Commit**

```bash
git add tools/bridge_acceptance.txt docs/macos-dev-setup.md
git commit -m "feat(bridge): acceptance script and docs

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
