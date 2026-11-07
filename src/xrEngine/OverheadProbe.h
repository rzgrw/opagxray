#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace xray::overhead
{
// Experimental common core: identical bytes in every arm. No engine dependency.
class Probe
{
public:
    using Clock = uint64_t (*)();
    static constexpr uint64_t MaxIntervals = 131072;
    explicit Probe(Clock clock = &Now) : m_clock(clock) {}
    bool Arm(uint64_t warmup, uint64_t measured);
    // True once, at the closing boundary. No allocation, I/O or clock disarmed.
    bool Boundary()
    {
        if (!m_armed) return false;
        m_stamps[m_count++] = m_clock();
        if (m_count != m_capacity) return false;
        m_armed = false;
        return true;
    }
    void CancelBeforeFirstBoundary() { if (m_count == 0) m_armed = false; }
    bool Armed() const { return m_armed; }
    size_t Count() const { return m_count; }
    uint64_t Last() const { return m_count ? m_stamps[m_count-1] : 0; }
    const uint64_t* Stamps() const { return m_stamps.get(); }
private:
    static uint64_t Now();
    Clock m_clock;
    std::unique_ptr<uint64_t[]> m_stamps;
    size_t m_capacity = 0, m_count = 0;
    bool m_armed = false;
};
}
