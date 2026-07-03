#pragma once

#include <cstdint>
#include <deque>

#include <music_router/params.h>

#include "echo_ring.h"

namespace mr
{

class AudioGraph;
class PluginNode;

class NotificationCenter
{
public:
    void bind(AudioGraph* graph) noexcept { graph_ = graph; }

    ParamEchoRing* echo_ring() noexcept { return &echo_; }

    void on_rescan(PluginNode* node, std::uint32_t clap_rescan_flags);
    void on_gui_closed(PluginNode* node);
    void note_state_loaded(std::uint32_t track_id, std::int32_t dest_slot);
    void push_param_value(std::uint32_t track_id, std::int32_t dest_slot, std::uint32_t param_id,
                          double value);

    bool poll(MrNotification& out);

private:
    void enqueue_rescan(std::uint32_t track_id, std::int32_t dest_slot, std::uint32_t flags);

    AudioGraph* graph_ = nullptr;
    std::deque<MrNotification> queue_;
    ParamEchoRing echo_;
};

}
