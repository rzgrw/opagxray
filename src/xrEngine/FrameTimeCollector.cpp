#include "FrameTimeCollector.h"

#include <chrono>
#include <limits>

namespace xray::frametime
{
namespace
{
uint64_t SteadyClock(void*)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

FrameTimeRecord* AllocateRecords(size_t count, void*)
{
    return new (std::nothrow) FrameTimeRecord[count];
}

void FreeRecords(FrameTimeRecord* records, void*)
{
    delete[] records;
}

void SaturatingIncrement(uint64_t& value)
{
    if (value != std::numeric_limits<uint64_t>::max())
        ++value;
}
}

CFrameTimeCollector::CFrameTimeCollector()
    : CFrameTimeCollector(&SteadyClock, &AllocateRecords, &FreeRecords, nullptr)
{
}

CFrameTimeCollector::CFrameTimeCollector(
    ClockAdapter clock, AllocationAdapter allocate, FreeAdapter free, void* context)
    : m_clock(clock ? clock : &SteadyClock)
    , m_allocate(allocate ? allocate : &AllocateRecords)
    , m_free(free ? free : &FreeRecords)
    , m_context(context)
{
}

CFrameTimeCollector::~CFrameTimeCollector()
{
    if (m_records)
        m_free(m_records, m_context);
}

FrameTimeResult CFrameTimeCollector::Start(size_t capacity, uint32_t warmup_loops)
{
    if (m_state == FrameTimeState::Stopped)
        return FrameTimeResult::ResetRequired;
    if (m_state != FrameTimeState::Disabled)
        return FrameTimeResult::AlreadyActive;
    if (capacity == 0 || capacity > MaxCapacity)
        return FrameTimeResult::InvalidCapacity;
    if (warmup_loops > MaxWarmupLoops)
        return FrameTimeResult::InvalidWarmup;

    FrameTimeRecord* records = m_allocate(capacity, m_context);
    if (!records)
        return FrameTimeResult::AllocationFailed;

    m_records = records;
    m_capacity = capacity;
    m_warmup_loops = warmup_loops;
    m_size = 0;
    m_warmup_observed = 0;
    m_dropped_loops = 0;
    m_invalid_loops = 0;
    m_next_loop_seq = 0;
    m_previous_start_ns = 0;
    m_last_phase_event_ns = 0;
    m_capture_start_ns = 0;
    m_capture_end_ns = 0;
    m_have_previous = false;
    m_record_current = false;
    m_current_valid = false;
    m_truncated = false;
    m_phase_depth = 0;
    m_state = FrameTimeState::Armed;
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::Stop()
{
    if (m_state != FrameTimeState::Armed && m_state != FrameTimeState::Capturing &&
        m_state != FrameTimeState::Saturated)
    {
        return FrameTimeResult::InvalidState;
    }
    m_state = FrameTimeState::StopPending;
    m_record_current = false;
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::Reset()
{
    if (m_state == FrameTimeState::Disabled)
        return FrameTimeResult::Ok;
    if (m_state != FrameTimeState::Stopped)
        return FrameTimeResult::InvalidState;

    if (m_records)
        m_free(m_records, m_context);
    m_records = nullptr;
    m_capacity = 0;
    m_size = 0;
    m_warmup_loops = 0;
    m_warmup_observed = 0;
    m_dropped_loops = 0;
    m_invalid_loops = 0;
    m_next_loop_seq = 0;
    m_previous_start_ns = 0;
    m_last_phase_event_ns = 0;
    m_capture_start_ns = 0;
    m_capture_end_ns = 0;
    m_have_previous = false;
    m_record_current = false;
    m_current_valid = false;
    m_truncated = false;
    m_current = {};
    m_active_phases = {};
    m_phase_depth = 0;
    m_state = FrameTimeState::Disabled;
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::BeginLoop()
{
    if (m_state == FrameTimeState::Disabled || m_state == FrameTimeState::Stopped)
        return FrameTimeResult::Ok;
    return BeginLoopAt(Now());
}

void CFrameTimeCollector::DiscardCurrentLoop()
{
    if (m_have_previous && (m_state == FrameTimeState::Capturing || m_state == FrameTimeState::Saturated))
        m_current_valid = false;
}

FrameTimeResult CFrameTimeCollector::BeginLoopAt(uint64_t start_ns)
{
    if (m_state == FrameTimeState::Disabled || m_state == FrameTimeState::Stopped)
        return FrameTimeResult::Ok;

    if (m_state == FrameTimeState::StopPending)
    {
        const bool reversed = m_have_previous && start_ns < m_previous_start_ns;
        if (reversed)
            SaturatingIncrement(m_invalid_loops);
        m_record_current = false;
        m_phase_depth = 0;
        m_state = FrameTimeState::Stopped;
        return reversed ? FrameTimeResult::TimeReversal : FrameTimeResult::Ok;
    }

    if (!m_have_previous)
    {
        m_have_previous = true;
        m_previous_start_ns = start_ns;
        m_state = FrameTimeState::Capturing;
        return StartCurrentLoop(start_ns);
    }

    FrameTimeResult result = FrameTimeResult::Ok;
    if (start_ns < m_previous_start_ns)
    {
        m_current_valid = false;
        result = FrameTimeResult::TimeReversal;
    }

    FinalizeCurrentLoop(start_ns);
    m_previous_start_ns = start_ns;
    const FrameTimeResult startResult = StartCurrentLoop(start_ns);
    if (result == FrameTimeResult::TimeReversal && startResult == FrameTimeResult::Ok)
        m_current_valid = false;
    return startResult == FrameTimeResult::Ok ? result : startResult;
}

FrameTimeResult CFrameTimeCollector::StartCurrentLoop(uint64_t start_ns)
{
    m_current = {};
    m_current.start_ns = start_ns;
    m_last_phase_event_ns = start_ns;
    m_current_valid = true;
    m_phase_depth = 0;
    if (m_next_loop_seq == std::numeric_limits<uint64_t>::max())
    {
        m_record_current = false;
        m_current_valid = false;
        m_truncated = true;
        m_state = FrameTimeState::Stopped;
        SaturatingIncrement(m_invalid_loops);
        return FrameTimeResult::LoopSequenceOverflow;
    }

    m_current.loop_seq = m_next_loop_seq++;
    m_record_current = m_state == FrameTimeState::Capturing &&
        m_warmup_observed == m_warmup_loops && m_size < m_capacity;
    return FrameTimeResult::Ok;
}

void CFrameTimeCollector::FinalizeCurrentLoop(uint64_t boundary_ns)
{
    if (m_phase_depth != 0 || m_last_phase_event_ns > boundary_ns ||
        m_current.engine_frame_end < m_current.engine_frame_begin)
    {
        m_current_valid = false;
    }

    if (!m_current_valid)
    {
        SaturatingIncrement(m_invalid_loops);
        return;
    }

    if (m_warmup_observed < m_warmup_loops)
    {
        ++m_warmup_observed;
        return;
    }

    if (m_state == FrameTimeState::Saturated)
    {
        SaturatingIncrement(m_dropped_loops);
        return;
    }

    if (!m_record_current)
        return;

    m_current.wall_ns = boundary_ns - m_previous_start_ns;
    if (m_size == 0)
        m_capture_start_ns = m_current.start_ns;
    m_records[m_size++] = m_current;
    m_capture_end_ns = boundary_ns;
    if (m_size == m_capacity)
    {
        m_truncated = true;
        m_state = FrameTimeState::Saturated;
    }
}

FrameTimeResult CFrameTimeCollector::BeginPhase(FrameTimePhase phase)
{
    if (!Recording())
        return FrameTimeResult::Ok;
    return BeginPhaseAt(phase, Now());
}

FrameTimeResult CFrameTimeCollector::EndPhase(FrameTimePhase phase)
{
    if (!Recording())
        return FrameTimeResult::Ok;
    return EndPhaseAt(phase, Now());
}

FrameTimeResult CFrameTimeCollector::BeginPhaseAt(FrameTimePhase phase, uint64_t start_ns)
{
    if (!Recording())
        return FrameTimeResult::Ok;
    if (phase >= FrameTimePhase::Count)
        return Invalidate(FrameTimeResult::InvalidPhase);
    if (start_ns < m_last_phase_event_ns)
        return Invalidate(FrameTimeResult::TimeReversal);
    if (m_phase_depth == MaxPhaseDepth)
        return Invalidate(FrameTimeResult::PhaseOverflow);

    m_active_phases[m_phase_depth++] = {phase, start_ns};
    m_last_phase_event_ns = start_ns;
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::EndPhaseAt(FrameTimePhase phase, uint64_t end_ns)
{
    if (!Recording())
        return FrameTimeResult::Ok;
    if (phase >= FrameTimePhase::Count)
        return Invalidate(FrameTimeResult::InvalidPhase);
    if (m_phase_depth == 0)
        return Invalidate(FrameTimeResult::PhaseUnderflow);

    const ActivePhase active = m_active_phases[m_phase_depth - 1];
    if (active.phase != phase)
        return Invalidate(FrameTimeResult::PhaseMismatch);
    if (end_ns < active.start_ns || end_ns < m_last_phase_event_ns)
        return Invalidate(FrameTimeResult::TimeReversal);

    const uint64_t duration = end_ns - active.start_ns;
    auto& timing = m_current.phases[static_cast<size_t>(phase)];
    if (duration > std::numeric_limits<uint64_t>::max() - timing.duration_ns)
        return Invalidate(FrameTimeResult::DurationOverflow);
    if (timing.calls == std::numeric_limits<uint32_t>::max())
        return Invalidate(FrameTimeResult::CallOverflow);

    timing.duration_ns += duration;
    ++timing.calls;
    --m_phase_depth;
    m_last_phase_event_ns = end_ns;
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::Mark(FrameTimeLoopFlags flags)
{
    if (Recording())
        m_current.flags |= static_cast<uint32_t>(flags);
    return FrameTimeResult::Ok;
}

FrameTimeResult CFrameTimeCollector::SetEngineFrameRange(uint32_t begin, uint32_t end)
{
    if (!Recording())
        return FrameTimeResult::Ok;
    if (end < begin)
        return Invalidate(FrameTimeResult::InvalidFrameRange);
    m_current.engine_frame_begin = begin;
    m_current.engine_frame_end = end;
    return FrameTimeResult::Ok;
}

FrameTimeStatus CFrameTimeCollector::GetStatus() const
{
    FrameTimeStatus status;
    status.state = m_state;
    status.capacity = m_capacity;
    status.warmup_loops = m_warmup_loops;
    status.warmup_observed = m_warmup_observed;
    status.stored_loops = m_size;
    status.dropped_loops = m_dropped_loops;
    status.invalid_loops = m_invalid_loops;
    status.has_samples = m_size != 0;
    status.truncated = m_truncated;
    if (status.has_samples)
    {
        status.first_loop_seq = m_records[0].loop_seq;
        status.last_loop_seq = m_records[m_size - 1].loop_seq;
    }
    status.capture_start_ns = m_capture_start_ns;
    status.capture_end_ns = m_capture_end_ns;
    return status;
}

FrameTimeSamples CFrameTimeCollector::Samples() const
{
    return {m_records, m_size};
}

uint64_t CFrameTimeCollector::Now() const
{
    return m_clock(m_context);
}

bool CFrameTimeCollector::Recording() const
{
    return m_state == FrameTimeState::Capturing && m_record_current;
}

FrameTimeResult CFrameTimeCollector::Invalidate(FrameTimeResult result)
{
    m_current_valid = false;
    return result;
}
}
