#ifdef NDEBUG
#error Assertions must stay enabled for this test executable
#endif
#include "xrEngine/FrameTimeSession.h"
#include <cassert>
using namespace xray::frametime;
static uint64_t now=100;
static uint64_t Tick(void*) { return now; }
int main()
{
    CFrameTimeSession s(&Tick);
    assert(s.Control("perf.start","4 2").ok); // 3 measured, one spare
    for (int i=0;i<5;++i) { now=100+i*100; s.BeginLoop(i); s.EndLoop(i); }
    assert(s.Collector().Samples().size==2);
    assert(s.ExperimentalCloseAt(600));
    const auto st=s.Collector().GetStatus();
    assert(!s.Running() && st.state==FrameTimeState::Stopped);
    assert(st.stored_loops==3 && st.capacity==4 && !st.truncated);
    assert(st.capture_start_ns==300 && st.capture_end_ns==600);
    assert(st.invalid_loops==0 && st.dropped_loops==0);
    assert(!s.ExperimentalCloseAt(700));
    s.BeginLoop(7);
    assert(s.Collector().GetStatus().capture_end_ns==600);
}
