#include "track_render.h"

#include <cstring>

#include <clap/audio-buffer.h>
#include <clap/process.h>

#include "graph_swap.h"
#include "invariants.h"
#include "plugin_node.h"

namespace mr
{

namespace
{

clap_audio_buffer_t make_buffer(float** channels, std::uint32_t count, std::uint64_t constant_mask)
{
    clap_audio_buffer_t b;
    b.data32 = channels;
    b.data64 = nullptr;
    b.channel_count = count;
    b.latency = 0;
    b.constant_mask = constant_mask;
    return b;
}

void zero_channels(float** channels, std::uint32_t count, std::uint32_t frames)
{
    for (std::uint32_t ch = 0; ch < count; ++ch)
    {
        std::memset(channels[ch], 0, frames * sizeof(float));
    }
}

struct EchoContext
{
    ParamEchoRing* echo;
    std::uint32_t track_id;
    std::int32_t dest_slot;
};

bool CLAP_ABI echo_try_push(const clap_output_events_t* list, const clap_event_header_t* ev)
{
    auto* ctx = static_cast<EchoContext*>(list->ctx);
    if (ctx->echo != nullptr && ev->space_id == CLAP_CORE_EVENT_SPACE_ID
        && ev->type == CLAP_EVENT_PARAM_VALUE)
    {
        auto* pv = reinterpret_cast<const clap_event_param_value_t*>(ev);
        ParamEcho pe{ctx->track_id, ctx->dest_slot, pv->param_id, 0, pv->value};
        ctx->echo->push(pe);
    }
    return true;
}

clap_output_events_t make_echo_sink(EchoContext* ctx)
{
    clap_output_events_t out;
    out.ctx = ctx;
    out.try_push = echo_try_push;
    return out;
}

}

Renderer::Renderer(AudioGraph& graph, SpscRing ring, std::uint32_t max_block, bool drop_realtime,
                   ParamEchoRing* echo)
    : graph_{graph}, ring_{ring}, echo_{echo}, max_block_{max_block}, drop_realtime_{drop_realtime},
      reorder_(ring.capacity()),
      buf_a_storage_(static_cast<std::size_t>(kMaxChannels) * max_block, 0.0f),
      buf_b_storage_(static_cast<std::size_t>(kMaxChannels) * max_block, 0.0f),
      buckets_(kMaxNodes)
{
    for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch)
    {
        buf_a_[ch] = buf_a_storage_.data() + static_cast<std::size_t>(ch) * max_block;
        buf_b_[ch] = buf_b_storage_.data() + static_cast<std::size_t>(ch) * max_block;
    }
}

void Renderer::render(float* const* out, std::uint32_t channels, std::uint32_t frames,
                      const RenderClock& clock)
{
    MR_ASSERT_AUDIO_THREAD();

    const GraphState* g = graph_.audio_apply();
    ring_.publish_clock(clock.stream_frame, clock.mono_ns);
    if (clock.latency_frames != last_latency_frames_)
    {
        ring_.publish_output_latency(clock.latency_frames);
        last_latency_frames_ = clock.latency_frames;
    }

    if (!stream_started_ || clock.stream_frame < last_stream_frame_)
    {
        reorder_.clear();
    }
    last_stream_frame_ = clock.stream_frame;
    stream_started_ = true;

    const std::uint32_t block_start = static_cast<std::uint32_t>(clock.stream_frame);
    for (std::uint32_t ch = 0; ch < channels; ++ch)
    {
        std::memset(out[ch], 0, frames * sizeof(float));
    }

    drain_and_demux(ring_, reorder_, *g, buckets_.data(), block_start, frames, drop_realtime_);

    for (std::uint32_t t = 0; t < g->track_count; ++t)
    {
        render_track(g->tracks[t], out, channels, frames, block_start, g->master_gain);
    }
}

void Renderer::render_track(const Track& tr, float* const* out, std::uint32_t channels,
                            std::uint32_t frames, std::uint32_t block_start, float master_gain)
{
    PluginNode* inst = tr.instrument;
    if (inst == nullptr || inst->state() != NodeState::Processing)
    {
        return;
    }

    std::uint32_t cur_ch = inst->out_channels();
    if (cur_ch > kMaxChannels)
    {
        cur_ch = kMaxChannels;
    }

    zero_channels(buf_a_, cur_ch, frames);
    clap_audio_buffer_t inst_out = make_buffer(buf_a_, cur_ch, 0);
    clap_input_events_t inst_in = make_input_events(&buckets_[inst->bucket_index]);

    clap_process_t p;
    p.steady_time = static_cast<std::int64_t>(block_start);
    p.frames_count = frames;
    p.transport = nullptr;
    p.audio_inputs = nullptr;
    p.audio_inputs_count = 0;
    p.audio_outputs = &inst_out;
    p.audio_outputs_count = 1;
    p.in_events = &inst_in;
    EchoContext inst_echo{echo_, tr.id, -1};
    clap_output_events_t inst_sink = make_echo_sink(&inst_echo);
    p.out_events = &inst_sink;
    inst->process(&p);

    float** cur = buf_a_;
    float** other = buf_b_;
    std::uint64_t cur_mask = inst_out.constant_mask;

    for (std::uint32_t s = 0; s < tr.chain_len; ++s)
    {
        PluginNode* fx = tr.chain[s];
        if (fx == nullptr || fx->state() != NodeState::Processing)
        {
            continue;
        }
        std::uint32_t in_ch = fx->in_channels();
        std::uint32_t out_ch = fx->out_channels();
        if (in_ch > kMaxChannels) in_ch = kMaxChannels;
        if (out_ch > kMaxChannels) out_ch = kMaxChannels;
        if (in_ch > cur_ch) in_ch = cur_ch;

        zero_channels(other, out_ch, frames);
        clap_audio_buffer_t fx_in = make_buffer(cur, in_ch, cur_mask);
        clap_audio_buffer_t fx_out = make_buffer(other, out_ch, 0);

        clap_input_events_t fx_events = make_input_events(&buckets_[fx->bucket_index]);
        clap_process_t pf;
        pf.steady_time = static_cast<std::int64_t>(block_start);
        pf.frames_count = frames;
        pf.transport = nullptr;
        pf.audio_inputs = &fx_in;
        pf.audio_inputs_count = 1;
        pf.audio_outputs = &fx_out;
        pf.audio_outputs_count = 1;
        pf.in_events = &fx_events;
        EchoContext fx_echo{echo_, tr.id, static_cast<std::int32_t>(s)};
        clap_output_events_t fx_sink = make_echo_sink(&fx_echo);
        pf.out_events = &fx_sink;
        fx->process(&pf);

        float** tmp = cur;
        cur = other;
        other = tmp;
        cur_ch = out_ch;
        cur_mask = fx_out.constant_mask;
    }

    std::uint32_t mix_ch = cur_ch < channels ? cur_ch : channels;
    float gain = tr.gain * master_gain;
    for (std::uint32_t ch = 0; ch < mix_ch; ++ch)
    {
        if (cur_mask & (1ull << ch))
        {
            float v = cur[ch][0] * gain;
            for (std::uint32_t i = 0; i < frames; ++i)
            {
                out[ch][i] += v;
            }
        }
        else
        {
            for (std::uint32_t i = 0; i < frames; ++i)
            {
                out[ch][i] += cur[ch][i] * gain;
            }
        }
    }
}

}
