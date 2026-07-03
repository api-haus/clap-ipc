#pragma once

#include <cstdint>

#include "config.h"

namespace mr
{

class PluginNode;

struct Track
{
    std::uint32_t id;
    PluginNode* instrument;
    PluginNode* chain[kMaxEffectsPerTrack];
    std::uint32_t chain_len;
    float gain;
};

struct GraphState
{
    Track tracks[kMaxTracks];
    std::uint32_t track_count;
    float master_gain;
};

std::uint32_t gather_nodes(const GraphState& g, PluginNode** out, std::uint32_t cap);
bool node_set_contains(PluginNode* const* set, std::uint32_t count, PluginNode* n);

const Track* find_track(const GraphState& g, std::uint32_t track_id);
PluginNode* resolve_node(const GraphState& g, std::uint32_t track_id, std::int32_t dest_slot);

}
