#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "config.h"
#include "graph.h"
#include "plugin_node.h"

namespace mr
{

struct Swap
{
    const GraphState* new_graph = nullptr;
    const GraphState* old_graph = nullptr;
    PluginNode* start_list[kMaxNodes];
    std::uint32_t start_count = 0;
    PluginNode* stop_list[kMaxNodes];
    std::uint32_t stop_count = 0;
};

class AudioGraph
{
public:
    AudioGraph();
    ~AudioGraph();

    AudioGraph(const AudioGraph&) = delete;
    AudioGraph& operator=(const AudioGraph&) = delete;

    void set_offline(bool offline) noexcept { offline_ = offline; }

    std::uint32_t create_track();
    bool destroy_track(std::uint32_t track_id);
    bool set_track_instrument(std::uint32_t track_id, std::unique_ptr<PluginNode> node);
    bool insert_effect(std::uint32_t track_id, std::int32_t slot_index, std::unique_ptr<PluginNode> node);
    bool remove_effect(std::uint32_t track_id, std::int32_t slot_index);
    bool set_track_gain(std::uint32_t track_id, float gain);
    PluginNode* find_node(std::uint32_t track_id, std::int32_t dest_slot);
    bool locate(PluginNode* node, std::uint32_t& track_id, std::int32_t& dest_slot) const;

    void reclaim();
    void teardown();

    const GraphState* audio_apply();
    const GraphState* current() const noexcept { return active_.load(std::memory_order_acquire); }
    void pump_main_thread();

private:
    struct TrackModel
    {
        std::uint32_t id;
        PluginNode* instrument = nullptr;
        std::vector<PluginNode*> chain;
        float gain = 1.0f;
    };

    TrackModel* find_model(std::uint32_t track_id);
    const GraphState* materialize() const;
    void commit_edit();
    void queue_swap(Swap* s);
    void settle();
    void destroy_node(PluginNode* node);

    std::vector<TrackModel> model_;
    std::vector<std::unique_ptr<PluginNode>> node_registry_;
    float master_gain_ = 1.0f;
    std::uint32_t next_track_id_ = 0;

    std::atomic<const GraphState*> active_{nullptr};
    const GraphState* last_snapshot_ = nullptr;

    std::atomic<Swap*> pending_swap_{nullptr};
    std::atomic<Swap*> to_control_{nullptr};
    Swap* retire_ = nullptr;
    std::atomic<std::uint64_t> swap_gen_{0};

    bool offline_ = true;
};

}
