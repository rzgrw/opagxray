#pragma once
/*
 * Agent bridge: local control channel for autonomous engine testing.
 * Active only with -agent_bridge. One client at a time.
 *   POSIX:   Unix domain socket, -agent_bridge [socket_path]
 *            (default $app_data_root$/agent_bridge.sock).
 *   Windows: TCP bound to 127.0.0.1 only, -agent_bridge [-agent_bridge_port <n>]
 *            (default port 0 = OS-chosen); the bound port is written to
 *            $app_data_root$/agent_bridge.port so clients can find each instance.
 * Protocol v1: "<id> <verb> [payload]\n" -> "<id> ok [payload]\n" | "<id> err <msg>\n"
 * Spec: docs/superpowers/specs/2026-07-04-agent-bridge-design.md
 */
#include "xrCore/xrCore.h"

#include <atomic>
#include <cstdint>
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

#ifdef XR_PLATFORM_WINDOWS
    using socket_handle = std::uintptr_t; // SOCKET
    CAgentBridge(u16 port, const char* portFilePath);
#else
    using socket_handle = int;
    CAgentBridge(const char* socketPath);
#endif
    ~CAgentBridge();

private:
    void SocketThreadProc();
    void HandleRequest(const std::string& line); // main thread
    void Respond(const std::string& id, bool ok, const std::string& payload);

    // verb handlers (main thread)
    std::string VerbPerf(const std::string& verb, const std::string& payload, bool& ok);
    std::string VerbCmd(const std::string& payload, bool& ok);
    std::string VerbLua(const std::string& payload, bool& ok);
    std::string VerbKey(const std::string& payload, bool& ok);
    std::string VerbMouse(const std::string& payload, bool& ok);
    std::string VerbState(bool& ok);
    std::string VerbShot(const std::string& payload, bool& ok);
    std::string VerbAgentPackSpawn(const std::string& payload, bool& ok);

    string_path m_socketPath; // POSIX: socket file; Windows: agent_bridge.port file
#ifdef XR_PLATFORM_WINDOWS
    socket_handle m_listenFd = ~socket_handle(0); // INVALID_SOCKET
    bool m_ready = false;      // failures are values: Initialize() drops a bridge that did not come up
    bool m_wsaStarted = false;
    bool m_portFileWritten = false;
#else
    socket_handle m_listenFd = -1;
#endif
    std::atomic_bool m_shutdown{ false };
    std::thread m_thread;

    std::mutex m_lock; // guards both queues
    std::deque<std::string> m_requests;   // socket thread -> main thread
    std::deque<std::string> m_responses;  // main thread -> socket thread

    // deferred key/mouse "tap" releases, flushed next OnFrame
    std::deque<u32> m_pendingKeyUps; // SDL keycodes
    std::deque<u8> m_pendingBtnUps;  // SDL button ids
};

extern ENGINE_API CAgentBridge* g_agent_bridge;

// -agent_background: unattended instance that stays out of the user's way —
// window shown without activation, off-screen and windowed, no OS input grab or
// cursor warps, no pause on focus loss, no audio device, no error dialogs.
ENGINE_API bool AgentBackgroundMode();
