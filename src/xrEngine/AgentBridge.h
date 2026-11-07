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

    CAgentBridge(const char* socketPath);
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

    string_path m_socketPath;
    int m_listenFd = -1;
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
