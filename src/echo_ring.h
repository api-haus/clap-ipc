#pragma once

#include <atomic>
#include <cstdint>

namespace mr
{

struct ParamEcho
{
    std::uint32_t track_id;
    std::int32_t dest_slot;
    std::uint32_t param_id;
    std::uint32_t _pad;
    double value;
};

class ParamEchoRing
{
public:
    bool push(const ParamEcho& e) noexcept
    {
        const std::uint32_t t = tail_.load(std::memory_order_relaxed);
        const std::uint32_t h = head_.load(std::memory_order_acquire);
        if (t - h == kCapacity)
        {
            return false;
        }
        slots_[t & (kCapacity - 1)] = e;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(ParamEcho& out) noexcept
    {
        const std::uint32_t h = head_.load(std::memory_order_relaxed);
        const std::uint32_t t = tail_.load(std::memory_order_acquire);
        if (h == t)
        {
            return false;
        }
        out = slots_[h & (kCapacity - 1)];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::uint32_t kCapacity = 1024;
    ParamEcho slots_[kCapacity];
    std::atomic<std::uint32_t> head_{0};
    std::atomic<std::uint32_t> tail_{0};
};

}
