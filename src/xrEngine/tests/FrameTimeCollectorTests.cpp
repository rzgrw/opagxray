#include "xrEngine/FrameTimeCollector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
using xray::frametime::CFrameTimeCollector;
using xray::frametime::FrameTimeLoopFlags;
using xray::frametime::FrameTimePhase;
using xray::frametime::FrameTimeResult;
using xray::frametime::FrameTimeState;

int failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

struct Harness
{
    uint64_t now = 0;
    size_t clockCalls = 0;
    size_t allocations = 0;
    size_t frees = 0;
    bool failAllocation = false;
};

uint64_t Clock(void* context)
{
    auto& harness = *static_cast<Harness*>(context);
    ++harness.clockCalls;
    return harness.now;
}

xray::frametime::FrameTimeRecord* Allocate(size_t count, void* context)
{
    auto& harness = *static_cast<Harness*>(context);
    ++harness.allocations;
    if (harness.failAllocation)
        return nullptr;
    return new (std::nothrow) xray::frametime::FrameTimeRecord[count];
}

void Free(xray::frametime::FrameTimeRecord* records, void* context)
{
    auto& harness = *static_cast<Harness*>(context);
    ++harness.frees;
    delete[] records;
}

struct Fixture
{
    Harness harness;
    CFrameTimeCollector collector{ &Clock, &Allocate, &Free, &harness };
};

void TestDisabledAndBounds()
{
    Fixture fixture;
    CHECK(fixture.collector.BeginLoop() == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhase(FrameTimePhase::Main) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhase(FrameTimePhase::Main) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Mark(FrameTimeLoopFlags::Loading) == FrameTimeResult::Ok);
    CHECK(fixture.harness.clockCalls == 0);
    CHECK(fixture.harness.allocations == 0);
    CHECK(fixture.collector.Samples().size == 0);

    CHECK(fixture.collector.Start(0) == FrameTimeResult::InvalidCapacity);
    CHECK(fixture.collector.Start(CFrameTimeCollector::MaxCapacity + 1) == FrameTimeResult::InvalidCapacity);
    CHECK(fixture.collector.Start(1, CFrameTimeCollector::MaxWarmupLoops + 1) == FrameTimeResult::InvalidWarmup);
    CHECK(fixture.harness.allocations == 0);

    fixture.harness.failAllocation = true;
    CHECK(fixture.collector.Start(1) == FrameTimeResult::AllocationFailed);
    CHECK(fixture.collector.GetStatus().state == FrameTimeState::Disabled);
    CHECK(fixture.harness.frees == 0);
}

void TestBoundariesWarmupAndStopRules()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(8, 2) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Start(8) == FrameTimeResult::AlreadyActive);
    CHECK(fixture.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Samples().size == 0);
    CHECK(fixture.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().warmup_observed == 1);
    CHECK(fixture.collector.BeginLoopAt(300) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().warmup_observed == 2);
    CHECK(fixture.collector.Samples().size == 0);
    CHECK(fixture.collector.BeginLoopAt(450) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Samples().size == 1);
    CHECK(fixture.collector.Samples().data[0].start_ns == 300);
    CHECK(fixture.collector.Samples().data[0].wall_ns == 150);
    CHECK(fixture.collector.Stop() == FrameTimeResult::Ok);
    CHECK(fixture.collector.Stop() == FrameTimeResult::InvalidState);
    CHECK(fixture.collector.BeginLoopAt(600) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().state == FrameTimeState::Stopped);
    CHECK(fixture.collector.Stop() == FrameTimeResult::InvalidState);
    CHECK(fixture.collector.Start(8) == FrameTimeResult::ResetRequired);

    const auto before = fixture.collector.Samples().data[0];
    CHECK(fixture.collector.BeginLoopAt(700) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 700) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Mark(FrameTimeLoopFlags::Menu) == FrameTimeResult::Ok);
    CHECK(std::memcmp(&before, &fixture.collector.Samples().data[0], sizeof(before)) == 0);
    CHECK(fixture.collector.Samples().size == 1);
    CHECK(fixture.collector.Reset() == FrameTimeResult::Ok);
    CHECK(fixture.collector.Samples().size == 0);
    CHECK(fixture.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Reset() == FrameTimeResult::InvalidState);
}

void TestCapacityAndDroppedLoops()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(0) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(10) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(20) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().state == FrameTimeState::Saturated);
    CHECK(fixture.collector.GetStatus().stored_loops == 2);
    CHECK(fixture.collector.GetStatus().dropped_loops == 0);
    CHECK(fixture.collector.GetStatus().truncated);
    CHECK(fixture.collector.BeginLoopAt(30) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().dropped_loops == 1);
    CHECK(fixture.collector.GetStatus().truncated);
    CHECK(fixture.collector.Stop() == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(40) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().stored_loops == 2);
    CHECK(fixture.collector.GetStatus().dropped_loops == 1);
    CHECK(fixture.collector.Samples().data[0].wall_ns == 10);
    CHECK(fixture.collector.Samples().data[1].start_ns == 10);
}

void TestClassificationPhasesAndFrameIds()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(4) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(1000) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Mark(FrameTimeLoopFlags::Loading) == FrameTimeResult::Ok);
    CHECK(fixture.collector.SetEngineFrameRange(7, 9) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 1010) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Present, 1020) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Present, 1030) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main, 1060) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::LoadDraw, 1060) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::LoadDraw, 1060) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::LoadDraw, 1070) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::LoadDraw, 1080) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(1100) == FrameTimeResult::Ok);

    const auto& sample = fixture.collector.Samples().data[0];
    CHECK(sample.loop_seq == 0);
    CHECK(sample.engine_frame_begin == 7);
    CHECK(sample.engine_frame_end == 9);
    CHECK(sample.flags == static_cast<uint32_t>(FrameTimeLoopFlags::Loading));
    CHECK(sample.Phase(FrameTimePhase::Main).calls == 1);
    CHECK(sample.Phase(FrameTimePhase::Main).duration_ns == 50);
    CHECK(sample.Phase(FrameTimePhase::Present).duration_ns == 10);
    CHECK(sample.Phase(FrameTimePhase::LoadDraw).calls == 2);
    CHECK(sample.Phase(FrameTimePhase::LoadDraw).duration_ns == 10);
    CHECK(sample.Phase(FrameTimePhase::Render).calls == 0);
    CHECK(sample.wall_ns == 100);
    CHECK(sample.Phase(FrameTimePhase::Main).duration_ns +
        sample.Phase(FrameTimePhase::Present).duration_ns < sample.wall_ns);

    CHECK(fixture.collector.Mark(FrameTimeLoopFlags::Menu) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(1200) == FrameTimeResult::Ok);
    CHECK(fixture.collector.Samples().data[1].flags == static_cast<uint32_t>(FrameTimeLoopFlags::Menu));
    CHECK(fixture.collector.Samples().data[1].Phase(FrameTimePhase::Main).calls == 0);
}

void TestInvalidInputs()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(16) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::PhaseUnderflow);
    CHECK(fixture.collector.BeginPhaseAt(static_cast<FrameTimePhase>(255), 110) == FrameTimeResult::InvalidPhase);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Present, 120) == FrameTimeResult::PhaseMismatch);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main, 105) == FrameTimeResult::TimeReversal);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main, 120) == FrameTimeResult::Ok);
    CHECK(fixture.collector.SetEngineFrameRange(9, 7) == FrameTimeResult::InvalidFrameRange);
    CHECK(fixture.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().invalid_loops == 1);
    CHECK(fixture.collector.Samples().size == 0);
}

void TestTimeReversal()
{
    Fixture loop;
    CHECK(loop.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(loop.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(loop.collector.BeginLoopAt(50) == FrameTimeResult::TimeReversal);
    CHECK(loop.collector.GetStatus().invalid_loops == 1);
    CHECK(loop.collector.Samples().size == 0);

    Fixture phase;
    CHECK(phase.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(phase.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(phase.collector.BeginPhaseAt(FrameTimePhase::Main, 90) == FrameTimeResult::TimeReversal);
    CHECK(phase.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(phase.collector.GetStatus().invalid_loops == 1);
    CHECK(phase.collector.Samples().size == 0);
}

void TestDurationOverflow()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 200) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 200) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main,
        std::numeric_limits<uint64_t>::max()) == FrameTimeResult::Ok);
    CHECK(fixture.collector.EndPhaseAt(FrameTimePhase::Main,
        std::numeric_limits<uint64_t>::max()) == FrameTimeResult::DurationOverflow);
    CHECK(fixture.collector.BeginLoopAt(std::numeric_limits<uint64_t>::max()) == FrameTimeResult::Ok);
    CHECK(fixture.collector.GetStatus().invalid_loops == 1);
    CHECK(fixture.collector.Samples().size == 0);
}

void TestPhaseDepth()
{
    Fixture fixture;
    CHECK(fixture.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    for (size_t i = 0; i < CFrameTimeCollector::MaxPhaseDepth; ++i)
        CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main, 100 + i) == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginPhaseAt(FrameTimePhase::Main,
        100 + CFrameTimeCollector::MaxPhaseDepth) == FrameTimeResult::PhaseOverflow);
}

void TestStopResetAndZeroDuration()
{
    Fixture armed;
    CHECK(armed.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(armed.collector.Stop() == FrameTimeResult::Ok);
    CHECK(armed.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(armed.collector.GetStatus().state == FrameTimeState::Stopped);
    CHECK(armed.collector.GetStatus().stored_loops == 0);
    CHECK(armed.collector.GetStatus().dropped_loops == 0);

    Fixture warmup;
    CHECK(warmup.collector.Start(2, 2) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(0) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(10) == FrameTimeResult::Ok);
    CHECK(warmup.collector.Stop() == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(20) == FrameTimeResult::Ok);
    CHECK(warmup.collector.GetStatus().warmup_observed == 1);
    CHECK(warmup.collector.GetStatus().stored_loops == 0);
    CHECK(warmup.collector.GetStatus().dropped_loops == 0);

    Fixture stale;
    CHECK(stale.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(stale.collector.BeginLoopAt(0) == FrameTimeResult::Ok);
    CHECK(stale.collector.BeginPhaseAt(FrameTimePhase::Main, 5) == FrameTimeResult::Ok);
    CHECK(stale.collector.Stop() == FrameTimeResult::Ok);
    CHECK(stale.collector.BeginLoopAt(10) == FrameTimeResult::Ok);
    CHECK(stale.collector.Reset() == FrameTimeResult::Ok);
    CHECK(stale.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(stale.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(stale.collector.EndPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::PhaseUnderflow);
    CHECK(stale.collector.BeginLoopAt(120) == FrameTimeResult::Ok);
    CHECK(stale.collector.GetStatus().invalid_loops == 1);

    Fixture repeated;
    CHECK(repeated.collector.Start(4) == FrameTimeResult::Ok);
    CHECK(repeated.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(repeated.collector.BeginPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::Ok);
    CHECK(repeated.collector.BeginPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::Ok);
    CHECK(repeated.collector.EndPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::Ok);
    CHECK(repeated.collector.EndPhaseAt(FrameTimePhase::Main, 110) == FrameTimeResult::Ok);
    CHECK(repeated.collector.BeginPhaseAt(FrameTimePhase::Main, 120) == FrameTimeResult::Ok);
    CHECK(repeated.collector.EndPhaseAt(FrameTimePhase::Main, 120) == FrameTimeResult::Ok);
    CHECK(repeated.collector.BeginLoopAt(130) == FrameTimeResult::Ok);
    CHECK(repeated.collector.Samples().data[0].Phase(FrameTimePhase::Main).calls == 3);
    CHECK(repeated.collector.Samples().data[0].Phase(FrameTimePhase::Main).duration_ns == 0);
    const auto finalized = repeated.collector.Samples().data[0];
    CHECK(repeated.collector.BeginPhaseAt(FrameTimePhase::Present, 130) == FrameTimeResult::Ok);
    CHECK(repeated.collector.EndPhaseAt(FrameTimePhase::Present, 135) == FrameTimeResult::Ok);
    CHECK(std::memcmp(&finalized, &repeated.collector.Samples().data[0], sizeof(finalized)) == 0);
}

void TestAllocationLifetime()
{
    Harness harness;
    {
        CFrameTimeCollector collector{ &Clock, &Allocate, &Free, &harness };
        CHECK(collector.Start(1) == FrameTimeResult::Ok);
        CHECK(collector.BeginLoopAt(0) == FrameTimeResult::Ok);
        CHECK(collector.Stop() == FrameTimeResult::Ok);
        CHECK(collector.BeginLoopAt(10) == FrameTimeResult::Ok);
        CHECK(collector.Reset() == FrameTimeResult::Ok);
        CHECK(harness.frees == 1);
    }
    CHECK(harness.allocations == 1);
    CHECK(harness.frees == 1);
}

void TestRetainedCaptureBoundsAndReset()
{
    Fixture exact;
    CHECK(exact.collector.Start(1) == FrameTimeResult::Ok);
    CHECK(exact.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(exact.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(exact.collector.Stop() == FrameTimeResult::Ok);
    CHECK(exact.collector.GetStatus().truncated);
    CHECK(exact.collector.BeginLoopAt(300) == FrameTimeResult::Ok);
    CHECK(exact.collector.GetStatus().state == FrameTimeState::Stopped);
    CHECK(exact.collector.GetStatus().truncated);
    CHECK(exact.collector.GetStatus().capture_start_ns == 100);
    CHECK(exact.collector.GetStatus().capture_end_ns == 200);

    Fixture warmup;
    CHECK(warmup.collector.Start(8, 1) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(300) == FrameTimeResult::Ok);
    CHECK(warmup.collector.Stop() == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(400) == FrameTimeResult::Ok);
    CHECK(warmup.collector.GetStatus().capture_start_ns == 200);
    CHECK(warmup.collector.GetStatus().capture_end_ns == 300);

    Fixture empty;
    CHECK(empty.collector.Start(8, 1) == FrameTimeResult::Ok);
    CHECK(empty.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(empty.collector.Stop() == FrameTimeResult::Ok);
    CHECK(empty.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(!empty.collector.GetStatus().has_samples);
    CHECK(empty.collector.GetStatus().capture_start_ns == 0);
    CHECK(empty.collector.GetStatus().capture_end_ns == 0);

    Fixture reset;
    CHECK(reset.collector.Start(1, 1) == FrameTimeResult::Ok);
    CHECK(reset.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(reset.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(reset.collector.BeginLoopAt(300) == FrameTimeResult::Ok);
    CHECK(reset.collector.BeginLoopAt(400) == FrameTimeResult::Ok);
    CHECK(reset.collector.Stop() == FrameTimeResult::Ok);
    CHECK(reset.collector.BeginLoopAt(500) == FrameTimeResult::Ok);
    CHECK(reset.collector.Reset() == FrameTimeResult::Ok);
    const auto status = reset.collector.GetStatus();
    CHECK(status.state == FrameTimeState::Disabled);
    CHECK(status.capacity == 0 && status.stored_loops == 0);
    CHECK(status.dropped_loops == 0 && status.invalid_loops == 0);
    CHECK(status.warmup_loops == 0 && status.warmup_observed == 0);
    CHECK(status.capture_start_ns == 0 && status.capture_end_ns == 0);
    CHECK(!status.has_samples && !status.truncated);
    CHECK(reset.collector.Reset() == FrameTimeResult::Ok);
}

void TestWarmupAndStopReversal()
{
    Fixture warmup;
    CHECK(warmup.collector.Start(8, 1) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(50) == FrameTimeResult::TimeReversal);
    CHECK(warmup.collector.GetStatus().invalid_loops == 1);
    CHECK(warmup.collector.BeginLoopAt(150) == FrameTimeResult::Ok);
    CHECK(warmup.collector.GetStatus().invalid_loops == 2);
    CHECK(warmup.collector.Stop() == FrameTimeResult::Ok);
    CHECK(warmup.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(warmup.collector.GetStatus().stored_loops == 0);
    CHECK(warmup.collector.GetStatus().capture_start_ns == 0);
    CHECK(warmup.collector.GetStatus().capture_end_ns == 0);

    Fixture stopped;
    CHECK(stopped.collector.Start(2) == FrameTimeResult::Ok);
    CHECK(stopped.collector.BeginLoopAt(100) == FrameTimeResult::Ok);
    CHECK(stopped.collector.BeginLoopAt(200) == FrameTimeResult::Ok);
    CHECK(stopped.collector.Stop() == FrameTimeResult::Ok);
    CHECK(stopped.collector.BeginLoopAt(50) == FrameTimeResult::TimeReversal);
    CHECK(stopped.collector.GetStatus().state == FrameTimeState::Stopped);
    CHECK(stopped.collector.GetStatus().invalid_loops == 1);
    CHECK(stopped.collector.GetStatus().capture_start_ns == 100);
    CHECK(stopped.collector.GetStatus().capture_end_ns == 200);
}

void TestProductionClockAdapter()
{
    Fixture fixture;
    fixture.harness.now = 100;
    CHECK(fixture.collector.Start() == FrameTimeResult::Ok);
    CHECK(fixture.collector.BeginLoop() == FrameTimeResult::Ok);
    fixture.harness.now = 110;
    CHECK(fixture.collector.BeginPhase(FrameTimePhase::Main) == FrameTimeResult::Ok);
    fixture.harness.now = 120;
    CHECK(fixture.collector.EndPhase(FrameTimePhase::Main) == FrameTimeResult::Ok);
    fixture.harness.now = 150;
    CHECK(fixture.collector.BeginLoop() == FrameTimeResult::Ok);
    CHECK(fixture.harness.clockCalls == 4);
    CHECK(fixture.collector.Samples().data[0].wall_ns == 50);
    CHECK(fixture.collector.Samples().data[0].Phase(FrameTimePhase::Main).duration_ns == 10);
}
}

int main()
{
    TestDisabledAndBounds();
    TestBoundariesWarmupAndStopRules();
    TestCapacityAndDroppedLoops();
    TestClassificationPhasesAndFrameIds();
    TestInvalidInputs();
    TestTimeReversal();
    TestDurationOverflow();
    TestPhaseDepth();
    TestStopResetAndZeroDuration();
    TestAllocationLifetime();
    TestRetainedCaptureBoundsAndReset();
    TestWarmupAndStopReversal();
    TestProductionClockAdapter();
    if (failures != 0)
    {
        std::fprintf(stderr, "FAIL: %d collector checks\n", failures);
        return 1;
    }
    std::puts("PASS: standalone frame-time collector state, bounds, phases, clocks and accounting");
    return 0;
}
