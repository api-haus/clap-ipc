#include "graph_swap.h"

#include <cassert>
#include <thread>

#include <music_router/wire.h>

#include "invariants.h"

namespace mr
{

AudioGraph::AudioGraph()
{
    const GraphState* empty = materialize();
    active_.store(empty, std::memory_order_release);
    last_snapshot_ = empty;
}

AudioGraph::~AudioGraph()
{
    if (active_.load(std::memory_order_acquire) != nullptr)
    {
        teardown();
    }
}

const GraphState* AudioGraph::materialize() const
{
    auto* g = new GraphState();
    g->master_gain = master_gain_;
    g->track_count = 0;
    for (const TrackModel& tm : model_)
    {
        if (g->track_count >= kMaxTracks)
        {
            break;
        }
        Track& tr = g->tracks[g->track_count++];
        tr.id = tm.id;
        tr.instrument = tm.instrument;
        tr.chain_len = 0;
        for (PluginNode* n : tm.chain)
        {
            if (tr.chain_len < kMaxEffectsPerTrack)
            {
                tr.chain[tr.chain_len++] = n;
            }
        }
        tr.gain = tm.gain;
    }
    return g;
}

void AudioGraph::commit_edit()
{
    MR_ASSERT_CONTROL_THREAD();
    reclaim();

    const GraphState* newg = materialize();
    auto* s = new Swap();
    s->new_graph = newg;

    PluginNode* newset[kMaxNodes];
    PluginNode* oldset[kMaxNodes];
    std::uint32_t nn = gather_nodes(*newg, newset, kMaxNodes);
    std::uint32_t on = gather_nodes(*last_snapshot_, oldset, kMaxNodes);

    for (std::uint32_t i = 0; i < nn; ++i)
    {
        if (!node_set_contains(oldset, on, newset[i]))
        {
            s->start_list[s->start_count++] = newset[i];
        }
    }
    for (std::uint32_t i = 0; i < on; ++i)
    {
        if (!node_set_contains(newset, nn, oldset[i]))
        {
            s->stop_list[s->stop_count++] = oldset[i];
        }
    }

    queue_swap(s);
    last_snapshot_ = newg;
    settle();
}

void AudioGraph::queue_swap(Swap* s)
{
    MR_ASSERT_CONTROL_THREAD();
    while (pending_swap_.load(std::memory_order_acquire) != nullptr)
    {
        std::this_thread::yield();
    }
    pending_swap_.store(s, std::memory_order_release);
}

void AudioGraph::settle()
{
    MR_ASSERT_CONTROL_THREAD();
    if (offline_)
    {
        {
            ScopedThreadRole audio(ThreadRole::Audio);
            audio_apply();
            audio_apply();
        }
        reclaim();
    }
}

const GraphState* AudioGraph::audio_apply()
{
    MR_ASSERT_AUDIO_THREAD();

    if (retire_ != nullptr && to_control_.load(std::memory_order_acquire) == nullptr)
    {
        for (std::uint32_t i = 0; i < retire_->stop_count; ++i)
        {
            retire_->stop_list[i]->stop_processing();
        }
        to_control_.store(retire_, std::memory_order_release);
        retire_ = nullptr;
    }

    if (retire_ == nullptr)
    {
        Swap* s = pending_swap_.exchange(nullptr, std::memory_order_acquire);
        if (s != nullptr)
        {
            for (std::uint32_t i = 0; i < s->start_count; ++i)
            {
                s->start_list[i]->start_processing();
            }
            s->old_graph = active_.load(std::memory_order_relaxed);
            active_.store(s->new_graph, std::memory_order_release);
            swap_gen_.fetch_add(1, std::memory_order_release);
            retire_ = s;
        }
    }

    return active_.load(std::memory_order_relaxed);
}

void AudioGraph::reclaim()
{
    MR_ASSERT_CONTROL_THREAD();
    Swap* r = to_control_.exchange(nullptr, std::memory_order_acquire);
    if (r != nullptr)
    {
        for (std::uint32_t i = 0; i < r->stop_count; ++i)
        {
            destroy_node(r->stop_list[i]);
        }
        delete r->old_graph;
        delete r;
    }
}

void AudioGraph::destroy_node(PluginNode* node)
{
    MR_ASSERT_CONTROL_THREAD();
    for (auto it = node_registry_.begin(); it != node_registry_.end(); ++it)
    {
        if (it->get() == node)
        {
            node_registry_.erase(it);
            return;
        }
    }
}

AudioGraph::TrackModel* AudioGraph::find_model(std::uint32_t track_id)
{
    for (TrackModel& tm : model_)
    {
        if (tm.id == track_id)
        {
            return &tm;
        }
    }
    return nullptr;
}

std::uint32_t AudioGraph::create_track()
{
    MR_ASSERT_CONTROL_THREAD();
    if (model_.size() >= kMaxTracks)
    {
        return UINT32_MAX;
    }
    std::uint32_t id = next_track_id_++;
    model_.push_back(TrackModel{id, nullptr, {}, 1.0f});
    commit_edit();
    return id;
}

bool AudioGraph::destroy_track(std::uint32_t track_id)
{
    MR_ASSERT_CONTROL_THREAD();
    for (auto it = model_.begin(); it != model_.end(); ++it)
    {
        if (it->id == track_id)
        {
            model_.erase(it);
            commit_edit();
            return true;
        }
    }
    return false;
}

bool AudioGraph::set_track_instrument(std::uint32_t track_id, std::unique_ptr<PluginNode> node)
{
    MR_ASSERT_CONTROL_THREAD();
    TrackModel* tm = find_model(track_id);
    if (tm == nullptr)
    {
        return false;
    }
    PluginNode* raw = node.get();
    node_registry_.push_back(std::move(node));
    tm->instrument = raw;
    commit_edit();
    return true;
}

bool AudioGraph::insert_effect(std::uint32_t track_id, std::int32_t slot_index,
                                 std::unique_ptr<PluginNode> node)
{
    MR_ASSERT_CONTROL_THREAD();
    TrackModel* tm = find_model(track_id);
    if (tm == nullptr || tm->chain.size() >= kMaxEffectsPerTrack)
    {
        return false;
    }
    PluginNode* raw = node.get();
    node_registry_.push_back(std::move(node));
    std::size_t pos = (slot_index < 0 || static_cast<std::size_t>(slot_index) > tm->chain.size())
                          ? tm->chain.size()
                          : static_cast<std::size_t>(slot_index);
    tm->chain.insert(tm->chain.begin() + pos, raw);
    commit_edit();
    return true;
}

bool AudioGraph::remove_effect(std::uint32_t track_id, std::int32_t slot_index)
{
    MR_ASSERT_CONTROL_THREAD();
    TrackModel* tm = find_model(track_id);
    if (tm == nullptr || slot_index < 0 ||
        static_cast<std::size_t>(slot_index) >= tm->chain.size())
    {
        return false;
    }
    tm->chain.erase(tm->chain.begin() + slot_index);
    commit_edit();
    return true;
}

bool AudioGraph::set_track_gain(std::uint32_t track_id, float gain)
{
    MR_ASSERT_CONTROL_THREAD();
    TrackModel* tm = find_model(track_id);
    if (tm == nullptr)
    {
        return false;
    }
    tm->gain = gain;
    commit_edit();
    return true;
}

PluginNode* AudioGraph::find_node(std::uint32_t track_id, std::int32_t dest_slot)
{
    MR_ASSERT_CONTROL_THREAD();
    TrackModel* tm = find_model(track_id);
    if (tm == nullptr)
    {
        return nullptr;
    }
    if (dest_slot < 0)
    {
        return tm->instrument;
    }
    if (static_cast<std::size_t>(dest_slot) < tm->chain.size())
    {
        return tm->chain[dest_slot];
    }
    return nullptr;
}

bool AudioGraph::locate(PluginNode* node, std::uint32_t& track_id, std::int32_t& dest_slot) const
{
    MR_ASSERT_CONTROL_THREAD();
    if (node == nullptr)
    {
        return false;
    }
    for (const TrackModel& tm : model_)
    {
        if (tm.instrument == node)
        {
            track_id = tm.id;
            dest_slot = MR_DEST_INSTRUMENT;
            return true;
        }
        for (std::size_t s = 0; s < tm.chain.size(); ++s)
        {
            if (tm.chain[s] == node)
            {
                track_id = tm.id;
                dest_slot = static_cast<std::int32_t>(s);
                return true;
            }
        }
    }
    return false;
}

void AudioGraph::pump_main_thread()
{
    MR_ASSERT_CONTROL_THREAD();
    for (std::unique_ptr<PluginNode>& node : node_registry_)
    {
        node->pump_main_thread();
    }
}

void AudioGraph::teardown()
{
    MR_ASSERT_CONTROL_THREAD();
    {
        ScopedThreadRole audio(ThreadRole::Audio);
        for (int i = 0; i < 4; ++i)
        {
            audio_apply();
        }
        for (std::unique_ptr<PluginNode>& node : node_registry_)
        {
            if (node->state() == NodeState::Processing)
            {
                node->stop_processing();
            }
        }
    }
    reclaim();
    node_registry_.clear();

    const GraphState* g = active_.load(std::memory_order_acquire);
    if (g != last_snapshot_)
    {
        delete last_snapshot_;
    }
    delete g;
    active_.store(nullptr, std::memory_order_release);
    last_snapshot_ = nullptr;
}

}
