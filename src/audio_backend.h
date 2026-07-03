#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "rt_policy.h"

namespace mr
{

struct RenderClock
{
    std::uint64_t stream_frame;
    std::uint64_t mono_ns;
    std::uint32_t latency_frames;
};

inline std::uint64_t steady_now_ns() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

using RenderFn = std::function<void(float* const* out, std::uint32_t channels, std::uint32_t frames,
                                    const RenderClock& clock)>;

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;
    virtual bool start(RenderFn render, std::uint32_t sample_rate, std::uint32_t max_block,
                       IRtPolicy& rt) = 0;
    virtual void stop() = 0;
    virtual bool is_offline() const = 0;
    virtual std::uint32_t output_latency_frames() const = 0;
};

class ICaptureBackend : public IAudioBackend
{
public:
    virtual std::uint32_t render_offline(std::uint32_t blocks, const char* wav_path) = 0;
};

std::unique_ptr<ICaptureBackend> make_capture_backend();
std::unique_ptr<IAudioBackend> make_device_backend(std::string device_selector = std::string{});

}
