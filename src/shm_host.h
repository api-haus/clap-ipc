#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <music_router/ring.h>

namespace mr
{

class HostSegment
{
public:
    static std::unique_ptr<HostSegment> create(std::uint32_t capacity, std::uint32_t sample_rate,
                                               std::uint32_t max_block, std::uint32_t lookahead);
    ~HostSegment();

    HostSegment(const HostSegment&) = delete;
    HostSegment& operator=(const HostSegment&) = delete;

    const char* name() const noexcept { return name_.c_str(); }
    SpscRing ring() const { return SpscRing::attach(base_); }

    std::uint32_t capacity() const noexcept { return capacity_; }
    std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    std::uint32_t max_block() const noexcept { return max_block_; }
    std::uint32_t lookahead() const noexcept { return lookahead_; }

    void set_output_latency_frames(std::uint32_t frames) noexcept
    {
        ring().publish_output_latency(frames);
    }
    std::uint32_t output_latency_frames() const noexcept
    {
        return ring().output_latency_frames();
    }

private:
    HostSegment() = default;

    std::string name_;
    void* base_ = nullptr;
    std::size_t bytes_ = 0;
    std::intptr_t handle_ = -1;
    std::uint32_t capacity_ = 0;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t max_block_ = 0;
    std::uint32_t lookahead_ = 0;
};

}
