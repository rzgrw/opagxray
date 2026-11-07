#ifdef NDEBUG
#error Assertions must stay enabled for this test executable
#endif
#include "xrEngine/OverheadProbe.h"
#include <cassert>
#include <cstdint>
#include <limits>
using xray::overhead::Probe;
static uint64_t calls = 0;
static uint64_t Tick() { return ++calls * 100; }
int main()
{
    Probe p(&Tick);
    for (int i=0;i<10;++i) assert(!p.Boundary());
    assert(calls==0 && p.Count()==0);
    assert(!p.Arm(0,0));
    assert(!p.Arm(std::numeric_limits<uint64_t>::max(),1));
    assert(!p.Arm(0,Probe::MaxIntervals)); // leaves collector one spare slot
    assert(p.Arm(2,3));
    assert(!p.Arm(2,3));
    assert(calls==0 && p.Count()==0);
    for(int i=0;i<5;++i) assert(!p.Boundary());
    assert(p.Boundary());
    assert(!p.Armed() && p.Count()==6 && p.Last()==600);
    for(int i=0;i<10;++i) assert(!p.Boundary());
    assert(calls==6 && p.Count()==6);
    assert(!p.Arm(0,1)); // one capture per process, immutable completed data
    for(size_t i=1;i<p.Count();++i) assert(p.Stamps()[i]-p.Stamps()[i-1]==100);
    Probe cancelled(&Tick); assert(cancelled.Arm(0,2));
    cancelled.CancelBeforeFirstBoundary();
    const auto beforeCancel = calls;
    assert(!cancelled.Boundary() && !cancelled.Armed() && cancelled.Count()==0 && calls==beforeCancel);
    assert(!cancelled.Arm(0,2));
    Probe one(&Tick); assert(one.Arm(0,1)); assert(!one.Boundary()); assert(one.Boundary());
}
