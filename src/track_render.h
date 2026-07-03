#pragma once

#include <cstdint>
#include <vector>

#include <music_router/ring.h>

#include "audio_backend.h"
#include "config.h"
#include "echo_ring.h"
#include "event_route.h"
#include "graph.h"

namespace mr
{

class AudioGraph;

class Renderer
{
public:
    Renderer(AudioGraph& graph, SpscRing ring, std::uint32_t max_block, bool drop_realtime,
             ParamEchoRing* echo);

    void render(float* const* out, std::uint32_t channels, std::uint32_t frames,
                const RenderClock& clock);

private:
    void render_track(const Track& tr, float* const* out, std::uint32_t channels, std::uint32_t frames,
                      std::uint32_t block_start, float master_gain);

    AudioGraph& graph_;
    SpscRing ring_;
    ParamEchoRing* echo_;
    std::uint32_t max_block_;
    bool drop_realtime_;
    std::uint32_t last_time_ = 0;
    std::uint64_t last_stream_frame_ = 0;
    std::uint32_t last_latency_frames_ = 0;
    bool stream_started_ = false;

    std::vector<float> buf_a_storage_;
    std::vector<float> buf_b_storage_;
    float* buf_a_[kMaxChannels];
    float* buf_b_[kMaxChannels];
    std::vector<NodeBucket> buckets_;
};

}
