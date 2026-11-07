#pragma once
#include "FrameTimeCollector.h"
#include <cstdio>
#include <atomic>
#include <thread>

namespace xray::frametime
{
struct FrameTimeReply
{
    bool ok = false;
    char text[2048]{};
};

struct XR_FRAME_TIME_API FrameTimeMetadata
{
    enum Field { SourceBuild, Renderer, ContentManifest, Overlay, RenderResolution,
        DrawableResolution, Vsync, FrameLimit, SDLVersion, AllocatorBuild, Count };
    char values[Count][256]{};
    FrameTimeMetadata();
    bool Set(Field field, const char* value);
};

XR_FRAME_TIME_API bool WriteFrameTimeCapture(FILE* file, const CFrameTimeCollector& collector, const FrameTimeMetadata& metadata);

// Primary-thread owner. A lock-free disabled branch precedes the immutable owner
// check, so worker scopes cannot touch state and disabled scopes cannot initialize
// native TLS storage. There is no socket-thread API.
class XR_FRAME_TIME_API CFrameTimeSession
{
public:
    explicit CFrameTimeSession(CFrameTimeCollector::ClockAdapter clock = nullptr);
    ~CFrameTimeSession();
    void BeginLoop(uint32_t engineFrame);
    void EndLoop(uint32_t engineFrame);
#ifdef XRAY_OVERHEAD_PROBE
    bool ExperimentalCloseAt(uint64_t closingStamp);
#endif
    void Mark(FrameTimeLoopFlags flags) { if (Running()) m_collector.Mark(flags); }
    bool Running() const { return m_running.load(std::memory_order_relaxed); }
    const CFrameTimeCollector& Collector() const { return m_collector; }
    FrameTimeReply Control(const char* verb, const char* payload, const FrameTimeMetadata* metadata = nullptr);
    using Writer = bool (*)(FILE*, const CFrameTimeCollector&, const FrameTimeMetadata&);
    FrameTimeReply Export(const char* appdata, Writer writer = &WriteFrameTimeCapture);
    bool NeedsShutdownWarning() const { return m_haveCapture && !m_exported; }
    // Call only in already-existing main-thread presentation/settings paths.
    // Marks provenance unknown if the observed setting differs from start.
    void ObserveDimensions(uint32_t width, uint32_t height, bool drawable);
    void ObserveFrameLimit(uint32_t limit);
    void ObserveVsync(int interval);

private:
    friend class FrameTimeScope;
    static_assert(std::atomic<bool>::is_always_lock_free);
    const std::thread::id m_owner;
    bool m_recordScopes = false;
    void AdvanceGeneration();
    void RejectMeasuredControl();
    FrameTimeReply Status() const;
    CFrameTimeCollector m_collector;
    FrameTimeMetadata m_metadata;
    uint32_t m_dimensions[4]{};
    uint32_t m_limit = 0;
    int m_vsync = 0;
    uint32_t m_unknownMetadata = 0;
    uint64_t m_generation = 0;
    uint32_t m_frameBegin = 0;
    std::atomic<bool> m_running{false};
    bool m_haveCapture = false;
    bool m_exported = false;
    bool m_generationExhausted = false;
};

class FrameTimeScope
{
public:
    FrameTimeScope(CFrameTimeSession& session, FrameTimePhase phase) : m_phase(phase)
    {
        if (!session.Running())
            return;
        if (std::this_thread::get_id() != session.m_owner || !session.m_recordScopes)
            return;
        if (session.m_collector.BeginPhase(phase) == FrameTimeResult::Ok)
        {
            m_session = &session;
            m_generation = session.m_generation;
        }
    }
    ~FrameTimeScope()
    {
        if (m_session && std::this_thread::get_id() == m_session->m_owner &&
            m_session->m_recordScopes && m_generation == m_session->m_generation)
            m_session->m_collector.EndPhase(m_phase);
    }
    FrameTimeScope(const FrameTimeScope&) = delete;
    FrameTimeScope& operator=(const FrameTimeScope&) = delete;
private:
    CFrameTimeSession* m_session = nullptr;
    uint64_t m_generation = 0;
    FrameTimePhase m_phase;
};
}
