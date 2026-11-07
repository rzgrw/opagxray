#include "OverheadProbe.h"
#include <chrono>
#include <new>
namespace xray::overhead
{
uint64_t Probe::Now()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool Probe::Arm(uint64_t warmup, uint64_t measured)
{
    if (m_stamps || measured == 0 || measured >= MaxIntervals || warmup >= MaxIntervals ||
        warmup > MaxIntervals - measured - 1) return false;
    const size_t capacity = static_cast<size_t>(warmup + measured + 1);
    auto* stamps = new (std::nothrow) uint64_t[capacity];
    if (!stamps) return false;
    m_stamps.reset(stamps);
    m_capacity = capacity;
    m_armed = true;
    return true;
}
}
