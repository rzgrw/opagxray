#include <cstdint>
#include <limits>

#define private public
#include "xrEngine/FrameTimeCollector.h"
#undef private

#include <cstdio>

namespace
{
using namespace xray::frametime;

int failures = 0;

void Check(bool condition, const char* message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

void TestSequenceExhaustion()
{
    CFrameTimeCollector collector;
    Check(collector.Start(4) == FrameTimeResult::Ok, "start");
    Check(collector.BeginLoopAt(0) == FrameTimeResult::Ok, "baseline");
    collector.m_next_loop_seq = std::numeric_limits<uint64_t>::max();
    Check(collector.BeginLoopAt(1) == FrameTimeResult::LoopSequenceOverflow, "sequence overflow returned");
    Check(collector.GetStatus().state == FrameTimeState::Stopped, "sequence overflow stops capture");
    Check(collector.GetStatus().invalid_loops == 1, "sequence overflow invalidates capture");
    Check(collector.GetStatus().truncated, "sequence overflow truncates capture");
    Check(collector.Samples().size == 1, "only pre-exhaustion record retained");
    Check(collector.Samples().data[0].loop_seq == 0, "existing loop ID unchanged");
    Check(collector.BeginLoopAt(2) == FrameTimeResult::Ok, "stopped boundary is inert");
    Check(collector.Samples().size == 1, "no duplicate or wrapped record accepted");
}

void TestDiagnosticCounterExhaustion()
{
    CFrameTimeCollector dropped;
    Check(dropped.Start(1) == FrameTimeResult::Ok, "drop start");
    dropped.BeginLoopAt(0);
    dropped.BeginLoopAt(1);
    dropped.m_dropped_loops = std::numeric_limits<uint64_t>::max();
    dropped.BeginLoopAt(2);
    Check(dropped.GetStatus().dropped_loops == std::numeric_limits<uint64_t>::max(), "drop count saturates");
    Check(dropped.GetStatus().truncated, "drop saturation retains truncation");

    CFrameTimeCollector invalid;
    Check(invalid.Start(2) == FrameTimeResult::Ok, "invalid start");
    invalid.BeginLoopAt(0);
    invalid.m_invalid_loops = std::numeric_limits<uint64_t>::max();
    Check(invalid.EndPhaseAt(FrameTimePhase::Main, 1) == FrameTimeResult::PhaseUnderflow, "invalid loop injected");
    invalid.BeginLoopAt(2);
    Check(invalid.GetStatus().invalid_loops == std::numeric_limits<uint64_t>::max(), "invalid count saturates");
    Check(invalid.Samples().size == 0, "invalid sample not accepted");
}
}

int main()
{
    TestSequenceExhaustion();
    TestDiagnosticCounterExhaustion();
    if (failures != 0)
        return 1;
    std::puts("PASS: nonwrapping loop sequence and diagnostic counters");
    return 0;
}
