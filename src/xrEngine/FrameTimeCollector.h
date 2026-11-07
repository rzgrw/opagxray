#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

// Standalone tests have no engine DLL API; runtime modules share these symbols.
#if defined(_WIN32) && defined(ENGINE_API)
#   define XR_FRAME_TIME_API ENGINE_API
#else
#   define XR_FRAME_TIME_API
#endif

namespace xray::frametime
{
enum class FrameTimeState : uint8_t
{
    Disabled,
    Armed,
    Capturing,
    Saturated,
    StopPending,
    Stopped,
};

enum class FrameTimePhase : uint8_t
{
    Main,
    Render,
    Present,
    WorkerWait,
    Limiter,
    InactiveSleep,
    NotReadySleep,
    DeviceSleep,
    LoadDraw,
    FenceWait,
    OcclusionWait,
    Count,
};

enum class FrameTimeLoopFlags : uint32_t
{
    None = 0,
    Loading = 1u << 0,
    Menu = 1u << 1,
    Paused = 1u << 2,
    Precache = 1u << 3,
    Inactive = 1u << 4,
    DeviceUnavailable = 1u << 5,
    WindowVisibilityUnknown = 1u << 6,
};

enum class FrameTimeResult : uint8_t
{
    Ok,
    InvalidCapacity,
    InvalidWarmup,
    AllocationFailed,
    AlreadyActive,
    ResetRequired,
    InvalidState,
    InvalidPhase,
    PhaseUnderflow,
    PhaseMismatch,
    PhaseOverflow,
    TimeReversal,
    DurationOverflow,
    CallOverflow,
    InvalidFrameRange,
    LoopSequenceOverflow,
};

struct FrameTimePhaseTiming
{
    uint64_t duration_ns = 0;
    uint32_t calls = 0;
};

struct FrameTimeRecord
{
    uint64_t loop_seq = 0;
    uint64_t start_ns = 0;
    uint64_t wall_ns = 0;
    uint32_t engine_frame_begin = 0;
    uint32_t engine_frame_end = 0;
    uint32_t flags = 0;
    std::array<FrameTimePhaseTiming, static_cast<size_t>(FrameTimePhase::Count)> phases{};

    const FrameTimePhaseTiming& Phase(FrameTimePhase phase) const
    {
        return phases[static_cast<size_t>(phase)];
    }
};

struct FrameTimeSamples
{
    const FrameTimeRecord* data = nullptr;
    size_t size = 0;
};

struct FrameTimeStatus
{
    FrameTimeState state = FrameTimeState::Disabled;
    size_t capacity = 0;
    uint32_t warmup_loops = 0;
    uint32_t warmup_observed = 0;
    uint64_t stored_loops = 0;
    uint64_t dropped_loops = 0;
    uint64_t invalid_loops = 0;
    uint64_t first_loop_seq = 0;
    uint64_t last_loop_seq = 0;
    uint64_t capture_start_ns = 0;
    uint64_t capture_end_ns = 0;
    bool has_samples = false;
    bool truncated = false;
};

class XR_FRAME_TIME_API CFrameTimeCollector
{
public:
    static constexpr size_t DefaultCapacity = 32768;
    static constexpr size_t MaxCapacity = 131072;
    static constexpr uint32_t MaxWarmupLoops = 1000000;
    static constexpr size_t MaxPhaseDepth = 32;

    using ClockAdapter = uint64_t (*)(void* context);
    using AllocationAdapter = FrameTimeRecord* (*)(size_t count, void* context);
    using FreeAdapter = void (*)(FrameTimeRecord* records, void* context);

    CFrameTimeCollector();
    CFrameTimeCollector(ClockAdapter clock, AllocationAdapter allocate, FreeAdapter free, void* context);
    ~CFrameTimeCollector();

    CFrameTimeCollector(const CFrameTimeCollector&) = delete;
    CFrameTimeCollector& operator=(const CFrameTimeCollector&) = delete;

    FrameTimeResult Start(size_t capacity = DefaultCapacity, uint32_t warmup_loops = 0);
    FrameTimeResult Stop();
    FrameTimeResult Reset();
    // Reject a loop contaminated by a forbidden measurement-control request.
    // Account it at the next boundary; preserve the capture and retained rows.
    void DiscardCurrentLoop();

    FrameTimeResult BeginLoop();
    FrameTimeResult BeginLoopAt(uint64_t start_ns);
    FrameTimeResult BeginPhase(FrameTimePhase phase);
    FrameTimeResult EndPhase(FrameTimePhase phase);
    FrameTimeResult BeginPhaseAt(FrameTimePhase phase, uint64_t start_ns);
    FrameTimeResult EndPhaseAt(FrameTimePhase phase, uint64_t end_ns);
    FrameTimeResult Mark(FrameTimeLoopFlags flags);
    FrameTimeResult SetEngineFrameRange(uint32_t begin, uint32_t end);

    FrameTimeStatus GetStatus() const;
    FrameTimeSamples Samples() const;

private:
    struct ActivePhase
    {
        FrameTimePhase phase = FrameTimePhase::Main;
        uint64_t start_ns = 0;
    };

    FrameTimeResult StartCurrentLoop(uint64_t start_ns);
    void FinalizeCurrentLoop(uint64_t boundary_ns);
    uint64_t Now() const;
    bool Recording() const;
    FrameTimeResult Invalidate(FrameTimeResult result);

    FrameTimeState m_state = FrameTimeState::Disabled;
    size_t m_capacity = 0;
    size_t m_size = 0;
    uint32_t m_warmup_loops = 0;
    uint32_t m_warmup_observed = 0;
    uint64_t m_dropped_loops = 0;
    uint64_t m_invalid_loops = 0;
    uint64_t m_next_loop_seq = 0;
    uint64_t m_previous_start_ns = 0;
    uint64_t m_last_phase_event_ns = 0;
    uint64_t m_capture_start_ns = 0;
    uint64_t m_capture_end_ns = 0;
    bool m_have_previous = false;
    bool m_record_current = false;
    bool m_current_valid = false;
    bool m_truncated = false;
    FrameTimeRecord* m_records = nullptr;
    FrameTimeRecord m_current{};
    std::array<ActivePhase, MaxPhaseDepth> m_active_phases{};
    size_t m_phase_depth = 0;

    ClockAdapter m_clock = nullptr;
    AllocationAdapter m_allocate = nullptr;
    FreeAdapter m_free = nullptr;
    void* m_context = nullptr;
};
}
