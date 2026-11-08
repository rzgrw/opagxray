#include "stdafx.h"
#ifdef XRAY_OVERHEAD_PROBE
#include "OverheadExperiment.h"
#endif
#include "AgentBridge.h"

CAgentBridge* g_agent_bridge = nullptr;

#include "XR_IOConsole.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimPackSpawn.h"
#include "xrScriptEngine/script_engine.hpp"
#include <lua.hpp>

#include <SDL.h>
#include <sstream>
#include <vector>
#ifdef XR_PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#endif

// ---- transport shim ----
// The protocol, queues and verbs are shared; only these few socket calls differ.
// POSIX keeps the original Unix-socket calls verbatim; Windows uses Winsock TCP.
namespace
{
#ifdef XR_PLATFORM_WINDOWS
using bridge_socket = SOCKET;
constexpr bridge_socket invalid_socket = INVALID_SOCKET;
static_assert(sizeof(bridge_socket) == sizeof(CAgentBridge::socket_handle), "socket handle size");

int socket_poll_readable(bridge_socket s, int timeoutMs)
{
    WSAPOLLFD p{ s, POLLRDNORM, 0 };
    return ::WSAPoll(&p, 1, timeoutMs);
}
bridge_socket socket_accept(bridge_socket listener)
{
    const bridge_socket s = ::accept(listener, nullptr, nullptr);
    if (s != invalid_socket)
    {
        BOOL noDelay = TRUE; // request/response protocol: do not let Nagle + delayed ACK add latency
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    }
    return s;
}
bool socket_write(bridge_socket s, const std::string& out)
{
    return ::send(s, out.c_str(), int(out.size()), 0) != SOCKET_ERROR;
}
int socket_read(bridge_socket s, char* buf, int size) { return ::recv(s, buf, size, 0); }
void socket_close(bridge_socket s) { ::closesocket(s); }
#else
using bridge_socket = int;
constexpr bridge_socket invalid_socket = -1;

int socket_poll_readable(bridge_socket s, int timeoutMs)
{
    pollfd p{ s, POLLIN, 0 };
    return ::poll(&p, 1, timeoutMs);
}
bridge_socket socket_accept(bridge_socket listener) { return ::accept(listener, nullptr, nullptr); }
bool socket_write(bridge_socket s, const std::string& out) { return ::write(s, out.c_str(), out.size()) >= 0; }
ssize_t socket_read(bridge_socket s, char* buf, size_t size) { return ::read(s, buf, size); }
void socket_close(bridge_socket s) { ::close(s); }
#endif

// "-name" as a whole token: "-agent_bridge" must not match "-agent_bridge_port".
pcstr find_flag(pcstr params, pcstr name)
{
    const size_t len = xr_strlen(name);
    for (pcstr p = strstr(params, name); p; p = strstr(p + 1, name))
    {
        const bool startOk = p == params || p[-1] == ' ' || p[-1] == '\t';
        const char end = p[len];
        if (startOk && (end == '\0' || end == ' ' || end == '\t'))
            return p;
    }
    return nullptr;
}
} // namespace

static bridge_socket s_clientFd = invalid_socket; // owned by socket thread only

bool AgentBackgroundMode()
{
    static const bool background = find_flag(Core.Params, "-agent_background") != nullptr;
    return background;
}

// Background instances must keep simulating while never focused (see device.cpp
// OnWindowActivate); bridge instances need the same for injected input.
static void apply_always_active_policy()
{
    if (AgentBackgroundMode())
        psDeviceFlags.set(rsAlwaysActive, true);
}

#ifdef XR_PLATFORM_WINDOWS

namespace
{
// Atomic publish: write a temp file, then rename over the target, so a client
// polling for the file never reads a partial port number.
bool write_port_file(pcstr path, u16 port)
{
    string_path tmp;
    xr_sprintf(tmp, "%s.%lu.tmp", path, ::GetCurrentProcessId());
    const HANDLE h = ::CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    char text[16];
    const int len = xr_sprintf(text, "%u\n", unsigned(port));
    DWORD written = 0;
    const bool ok = len > 0 && ::WriteFile(h, text, DWORD(len), &written, nullptr) && written == DWORD(len) &&
        ::FlushFileBuffers(h);
    ::CloseHandle(h);
    if (ok && ::MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    ::DeleteFileA(tmp);
    return false;
}
} // namespace

void CAgentBridge::Initialize()
{
    apply_always_active_policy();
    pcstr opt = find_flag(Core.Params, "-agent_bridge");
    if (!opt || g_agent_bridge)
        return;

    unsigned port = 0; // 0: the OS picks a free port; agent_bridge.port tells clients which
    if (pcstr portOpt = find_flag(Core.Params, "-agent_bridge_port"))
    {
        char tail = 0;
        const int rc = sscanf(portOpt + xr_strlen("-agent_bridge_port"), " %u%c", &port, &tail);
        if (rc < 1 || (rc == 2 && tail != ' ' && tail != '\t') || port > 65535)
        {
            Msg("! agent_bridge: invalid -agent_bridge_port (expected 0..65535); bridge disabled");
            return;
        }
    }

    string_path portFile{};
    FS.update_path(portFile, "$app_data_root$", "agent_bridge.port");

    CAgentBridge* bridge = xr_new<CAgentBridge>(u16(port), portFile);
    if (!bridge->m_ready)
    {
        xr_delete(bridge);
        Msg("! agent_bridge: disabled (startup failed)");
        return;
    }
    // Agent-driven sessions run without OS window focus: keep the game
    // simulating and accepting input as if focused, or every command
    // lands in a paused world.
    psDeviceFlags.set(rsAlwaysActive, true);
    g_agent_bridge = bridge;
}

CAgentBridge::CAgentBridge(u16 port, const char* portFilePath)
{
    xr_strcpy(m_socketPath, portFilePath);
    ::DeleteFileA(m_socketPath); // a stale port file from a crashed run must not point clients at a dead port

    WSADATA wsa{};
    if (0 != ::WSAStartup(MAKEWORD(2, 2), &wsa))
    {
        Msg("! agent_bridge: WSAStartup failed");
        return;
    }
    m_wsaStarted = true;

    const SOCKET s = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET)
    {
        Msg("! agent_bridge: socket() failed (%d)", ::WSAGetLastError());
        return;
    }
    BOOL exclusive = TRUE; // no other process may bind the same port while we hold it
    ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, never a remote interface
    addr.sin_port = htons(port);
    int addrLen = sizeof(addr);
    if (0 != ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) || 0 != ::listen(s, 1) ||
        0 != ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &addrLen))
    {
        Msg("! agent_bridge: cannot listen on 127.0.0.1:%u (%d)", unsigned(port), ::WSAGetLastError());
        ::closesocket(s);
        return;
    }
    m_listenFd = socket_handle(s);
    const unsigned bound = ntohs(addr.sin_port);

    m_portFileWritten = write_port_file(m_socketPath, u16(bound));
    if (!m_portFileWritten)
        Msg("~ agent_bridge: could not write %s (error %lu); clients need the port from this log", m_socketPath,
            ::GetLastError());

    m_thread = std::thread([this] { SocketThreadProc(); });
    m_ready = true;
    Msg("* agent_bridge: listening on tcp:127.0.0.1:%u (port file %s)", bound, m_socketPath);
}

CAgentBridge::~CAgentBridge()
{
    m_shutdown.store(true, std::memory_order_release);
    if (m_thread.joinable())
        m_thread.join(); // bounded: poll timeouts are <=100ms
    if (m_listenFd != socket_handle(INVALID_SOCKET))
        ::closesocket(SOCKET(m_listenFd)); // after join: the thread never polls a closed handle
    if (m_portFileWritten)
        ::DeleteFileA(m_socketPath);
    if (m_wsaStarted)
        ::WSACleanup();
    if (m_ready)
        Msg("* agent_bridge: closed");
}

#else // POSIX: Unix domain socket

void CAgentBridge::Initialize()
{
    apply_always_active_policy();
    pcstr opt = find_flag(Core.Params, "-agent_bridge");
    if (!opt || g_agent_bridge)
        return;

    string_path path{};
    // optional explicit path after the flag
    if (1 != sscanf(opt + xr_strlen("-agent_bridge"), " %s", path) || path[0] == '-')
        FS.update_path(path, "$app_data_root$", "agent_bridge.sock");
    convert_path_separators(path); // FS paths use '\' — bind() needs the real POSIX path

    // Agent-driven sessions run without OS window focus: keep the game
    // simulating and accepting input as if focused, or every command
    // lands in a paused world.
    psDeviceFlags.set(rsAlwaysActive, true);

    g_agent_bridge = xr_new<CAgentBridge>(path);
}

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
        ::close(m_listenFd); // wakes the accept poll
    if (m_thread.joinable())
        m_thread.join(); // bounded: poll timeouts are <=100ms
    ::unlink(m_socketPath);
    Msg("* agent_bridge: closed");
}

#endif // XR_PLATFORM_WINDOWS

void CAgentBridge::Destroy() { xr_delete(g_agent_bridge); }

void CAgentBridge::SocketThreadProc()
{
    const bridge_socket listener = bridge_socket(m_listenFd);
    std::string inbuf;
    while (!m_shutdown.load(std::memory_order_acquire))
    {
        if (s_clientFd == invalid_socket) // accept phase
        {
            if (socket_poll_readable(listener, 100) <= 0)
                continue;
            s_clientFd = socket_accept(listener);
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
            if (!socket_write(s_clientFd, out))
            {
                socket_close(s_clientFd);
                s_clientFd = invalid_socket;
                break;
            }
        }
        if (s_clientFd == invalid_socket)
            continue;

        const int rc = socket_poll_readable(s_clientFd, 20); // short: responses need timely flushing
        if (rc <= 0)
            continue;

        char chunk[4096];
        const auto n = socket_read(s_clientFd, chunk, sizeof(chunk));
        if (n <= 0) // disconnect: drop pending work, re-accept
        {
            socket_close(s_clientFd);
            s_clientFd = invalid_socket;
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
    if (s_clientFd != invalid_socket)
    {
        socket_close(s_clientFd);
        s_clientFd = invalid_socket;
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

namespace
{
    void push_key_event(SDL_Scancode sc, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.timestamp = SDL_GetTicks();
        e.key.windowID = SDL_GetWindowID(Device.m_sdlWnd);
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.scancode = sc;
        e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
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

void CAgentBridge::OnFrame()
{
    // taps: the down was pushed last frame; release now so the game saw one full frame held
    while (!m_pendingKeyUps.empty())
    {
        push_key_event(SDL_Scancode(m_pendingKeyUps.front()), false);
        m_pendingKeyUps.pop_front();
    }
    while (!m_pendingBtnUps.empty())
    {
        push_btn_event(m_pendingBtnUps.front(), false);
        m_pendingBtnUps.pop_front();
    }

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
        result = std::string("agent_bridge v1 ") + Core.ApplicationName;
    else if (verb == "bye")
    {
        Respond(id, true, "");
        return; // socket thread notices the client closing
    }
#ifdef XRAY_OVERHEAD_PROBE
    else if (verb.rfind("overhead.", 0) == 0) result = xray::overhead::Control(verb, payload, ok);
#endif
    else if (verb.rfind("perf.", 0) == 0) result = VerbPerf(verb, payload, ok);
    else if (verb == "cmd")   result = VerbCmd(payload, ok);
    else if (verb == "lua")   result = VerbLua(payload, ok);
    else if (verb == "key")   result = VerbKey(payload, ok);
    else if (verb == "mouse") result = VerbMouse(payload, ok);
    else if (verb == "state") result = VerbState(ok);
    else if (verb == "shot")  result = VerbShot(payload, ok);
    else if (verb == "agent.pack.spawn") result = VerbAgentPackSpawn(payload, ok);
    else if (verb.rfind("ai.", 0) == 0 || verb.rfind("agent.", 0) == 0)
        result = xrSim::HandleBridgeVerb(verb, payload, ok);
    else { ok = false; result = "unknown verb: " + verb; }

    Respond(id, ok, result);
#ifdef XRAY_OVERHEAD_PROBE
    if (ok && verb == "overhead.arm") xray::overhead::AwaitRunner();
#endif
}

// All perf controls stay in the existing main-thread OnFrame queue. Loading-only
// and not-ready loops do not drain it; callers start before loading and stop only
// once responsive. Stop replies stop_pending; one later explicit status is enough.
std::string CAgentBridge::VerbPerf(const std::string& verb, const std::string& payload, bool& ok)
{
    using namespace xray::frametime;
    auto& session = Device.FrameTime;
    FrameTimeMetadata metadata;
    const auto state = session.Collector().GetStatus().state;
    if (verb == "perf.start" && state == FrameTimeState::Disabled)
    {
#ifndef XR_FRAME_TIME_SOURCE_BUILD
#define XR_FRAME_TIME_SOURCE_BUILD "unknown"
#endif
#ifndef XR_FRAME_TIME_ALLOCATOR
#define XR_FRAME_TIME_ALLOCATOR "unknown"
#endif
        metadata.Set(FrameTimeMetadata::SourceBuild, XR_FRAME_TIME_SOURCE_BUILD);
        metadata.Set(FrameTimeMetadata::AllocatorBuild, XR_FRAME_TIME_ALLOCATOR);
        // No authenticated asset inventory is maintained by the engine. Do not
        // pretend the presence of a staging manifest verifies current assets.
        char value[128];
        xr_sprintf(value, "%ux%u", Device.dwWidth, Device.dwHeight);
        metadata.Set(FrameTimeMetadata::RenderResolution, value);
        if (SDL_GL_GetCurrentContext())
        {
            metadata.Set(FrameTimeMetadata::Renderer, "OpenGL"); // actual current API, not a mutable console token
            int width=0, height=0;
            SDL_GL_GetDrawableSize(Device.m_sdlWnd, &width, &height);
            if (width>0 && height>0)
            {
                xr_sprintf(value, "%dx%d", width, height);
                metadata.Set(FrameTimeMetadata::DrawableResolution, value);
            }
            const int interval=SDL_GL_GetSwapInterval();
            metadata.Set(FrameTimeMetadata::Vsync, interval<0?"adaptive":interval?"on":"off");
        }
        extern int ps_fps_limit, ps_fps_limit_in_menu, g_svDedicateServerUpdateReate;
        const int limit=GEnv.isDedicatedServer?g_svDedicateServerUpdateReate:
            (Device.Paused() || !g_pGameLevel)?ps_fps_limit_in_menu:ps_fps_limit;
        if (limit>=0)
        {
            xr_sprintf(value, "%u", unsigned(limit));
            metadata.Set(FrameTimeMetadata::FrameLimit, value);
        }
        SDL_version version{};SDL_GetVersion(&version);
        xr_sprintf(value, "%u.%u.%u", version.major,version.minor,version.patch);
        metadata.Set(FrameTimeMetadata::SDLVersion, value);
    }
    auto result=session.Control(verb.c_str(),payload.c_str(),&metadata);
    if (result.ok && verb=="perf.export")
    {
        string_path path;
        FS.update_path(path,"$app_data_root$","");
        convert_path_separators(path);
        result=session.Export(path);
    }
    ok=result.ok;
    return result.text;
}

// ---- cmd ----

namespace
{
    std::string* s_capture = nullptr;
    void capture_log(void* /*context*/, const char* line)
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

// ---- lua ----

namespace
{
    // shallow stringification of the value at absolute stack index i
    std::string lua_value_to_string(lua_State* L, int i)
    {
        if (lua_istable(L, i))
        {
            std::string out = "{";
            lua_pushnil(L);
            bool first = true;
            while (lua_next(L, i))
            {
                if (!first)
                    out += ", ";
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
                pcstr vs = lua_tostring(L, -1); // no recursion into nested tables
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
    const std::string expr = "return " + payload;
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
        if (i > 1)
            out += "\t";
        out += lua_value_to_string(L, base + i);
    }
    lua_settop(L, base);
    return out;
}

// ---- key / mouse ----

std::string CAgentBridge::VerbKey(const std::string& payload, bool& ok)
{
    char name[64]{}, action[16]{};
    if (2 != sscanf(payload.c_str(), "%63s %15s", name, action))
    {
        ok = false;
        return "usage: key <sdl_key_name> down|up|tap";
    }

    // Physical scancode first: game bindings are scancode-based, and deriving
    // scancodes from key symbols depends on the user's ACTIVE keyboard layout
    // (a Cyrillic layout has no physical key producing 'w' — injection went
    // nowhere whenever the user's layout was toggled to Russian).
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    if (sc == SDL_SCANCODE_UNKNOWN)
    {
        const SDL_Keycode kc = SDL_GetKeyFromName(name);
        if (kc != SDLK_UNKNOWN)
            sc = SDL_GetScancodeFromKey(kc);
    }
    if (sc == SDL_SCANCODE_UNKNOWN) { ok = false; return std::string("unknown key: ") + name; }

    if (0 == xr_strcmp(action, "down"))
        push_key_event(sc, true);
    else if (0 == xr_strcmp(action, "up"))
        push_key_event(sc, false);
    else if (0 == xr_strcmp(action, "tap"))
    {
        push_key_event(sc, true);
        m_pendingKeyUps.push_back(u32(sc));
    }
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
        {
            ok = false;
            return "usage: mouse move <dx> <dy>";
        }
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
        {
            ok = false;
            return "usage: mouse btn <left|right|middle> down|up|tap";
        }
        u8 b = SDL_BUTTON_LEFT;
        if (0 == xr_strcmp(which, "right"))
            b = SDL_BUTTON_RIGHT;
        else if (0 == xr_strcmp(which, "middle"))
            b = SDL_BUTTON_MIDDLE;
        else if (0 != xr_strcmp(which, "left"))
        {
            ok = false;
            return "unknown button";
        }

        if (0 == xr_strcmp(action, "down"))
            push_btn_event(b, true);
        else if (0 == xr_strcmp(action, "up"))
            push_btn_event(b, false);
        else if (0 == xr_strcmp(action, "tap"))
        {
            push_btn_event(b, true);
            m_pendingBtnUps.push_back(b);
        }
        else { ok = false; return "unknown action"; }
        return "";
    }
    ok = false;
    return "usage: mouse move|btn ...";
}

// ---- state / shot ----

std::string CAgentBridge::VerbState(bool& ok)
{
    string256 buf;
    const float fps = Device.fTimeDelta > EPS ? 1.f / Device.fTimeDelta : 0.f;
    xr_sprintf(buf, "scene=%s fps=%.0f frame=%u paused=%d loadscr=%d precache=%u",
        g_pGameLevel ? "game" : "menu", fps, Device.dwFrame,
        Device.Paused() ? 1 : 0, load_screen_renderer.IsActive() ? 1 : 0, Device.dwPrecacheFrame);
    std::string out = buf;

    if (g_pGameLevel && GEnv.ScriptEngine)
    {
        bool luaOk = true;
        // one round-trip into Lua for the actor block; db.actor is nil while loading
        const std::string actor = VerbLua(
            "db and db.actor and string.format('level=%s pos=%.1f,%.1f,%.1f hp=%.2f time=%d', "
            "level.name(), db.actor:position().x, db.actor:position().y, db.actor:position().z, "
            "db.actor.health, game.time()) or 'actor=nil'",
            luaOk);
        out += " ";
        out += luaOk ? actor : "actor=err";
    }
    return out;
}

std::string CAgentBridge::VerbShot(const std::string& payload, bool& ok)
{
    if (payload.empty()) { ok = false; return "usage: shot <name>"; }
    if (!GEnv.Render)    { ok = false; return "renderer not ready"; }
    GEnv.Render->Screenshot(IRender::SM_NORMAL, payload.c_str());
    return payload;
}

namespace
{
    std::string format_spawned_ids(const std::vector<u16>& ids)
    {
        std::ostringstream out;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i != 0)
                out << ",";
            out << ids[i];
        }
        return out.str();
    }
} // namespace

std::string CAgentBridge::VerbAgentPackSpawn(const std::string& payload, bool& ok)
{
    const xrSim::PackSpawnParseResult parsed = xrSim::ParsePackSpawnPayload(payload);
    if (!parsed.ok)
    {
        ok = false;
        return parsed.reason;
    }

    if (!g_pGameLevel || !g_pGameLevel->bReady)
    {
        ok = false;
        return "game level not ready";
    }

    std::vector<u16> ids;
    ids.reserve(parsed.request.count);
    for (u32 i = 0; i < parsed.request.count; ++i)
    {
        u16 id = u16(-1);
        xr_string reason;
        if (!g_pGameLevel->AgentBridgeSpawnObjectNearCurrentEntity(
                parsed.request.section.c_str(), i, parsed.request.count, float(parsed.request.radiusMeters), id, reason))
        {
            ok = false;
            std::ostringstream out;
            out << "spawn failed after ids=" << format_spawned_ids(ids) << " reason=" << reason.c_str();
            return out.str();
        }
        ids.push_back(id);
    }

    std::ostringstream registration;
    registration << parsed.request.section;
    for (const u16 id : ids)
        registration << " " << id;

    bool registerOk = false;
    const std::string registered = xrSim::HandleBridgeVerb("agent.pack.register", registration.str(), registerOk);
    ok = registerOk;
    if (!registerOk)
        return registered;

    return "spawned " + registered + " ids=" + format_spawned_ids(ids);
}

