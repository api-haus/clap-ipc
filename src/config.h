#pragma once

#include <cstdint>

namespace mr
{

inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxTracks = 16;
inline constexpr std::uint32_t kMaxEffectsPerTrack = 8;
inline constexpr std::uint32_t kMaxNodes = kMaxTracks * (1 + kMaxEffectsPerTrack);
inline constexpr std::uint32_t kMaxEventsPerBlock = 256;

inline constexpr std::uint32_t kDefaultSampleRate = 48000;
inline constexpr std::uint32_t kDefaultBlock = 512;
inline constexpr std::uint32_t kMinDeviceBlock = 512;
inline constexpr std::uint32_t kDefaultCapacity = 8192;
inline constexpr float kDefaultRtPriorityFraction = 0.8f;

enum class BackendKind
{
    Capture,
    Device
};

struct HostConfig
{
    BackendKind backend = BackendKind::Capture;
    std::uint32_t sample_rate = kDefaultSampleRate;
    std::uint32_t block = kDefaultBlock;
    std::uint32_t capacity = kDefaultCapacity;
    std::uint32_t lookahead_frames = kDefaultBlock * 2;
    const char* control_socket_path = nullptr;
    const char* device = nullptr;
    bool drop_realtime = false;
    bool enable_gui = false;
};

}
