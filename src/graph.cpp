#include "graph.h"

namespace mr
{

std::uint32_t gather_nodes(const GraphState& g, PluginNode** out, std::uint32_t cap)
{
    std::uint32_t n = 0;
    for (std::uint32_t t = 0; t < g.track_count; ++t)
    {
        const Track& tr = g.tracks[t];
        if (tr.instrument != nullptr && n < cap)
        {
            out[n++] = tr.instrument;
        }
        for (std::uint32_t s = 0; s < tr.chain_len && n < cap; ++s)
        {
            out[n++] = tr.chain[s];
        }
    }
    return n;
}

bool node_set_contains(PluginNode* const* set, std::uint32_t count, PluginNode* n)
{
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (set[i] == n)
        {
            return true;
        }
    }
    return false;
}

const Track* find_track(const GraphState& g, std::uint32_t track_id)
{
    for (std::uint32_t t = 0; t < g.track_count; ++t)
    {
        if (g.tracks[t].id == track_id)
        {
            return &g.tracks[t];
        }
    }
    return nullptr;
}

PluginNode* resolve_node(const GraphState& g, std::uint32_t track_id, std::int32_t dest_slot)
{
    const Track* tr = find_track(g, track_id);
    if (tr == nullptr)
    {
        return nullptr;
    }
    if (dest_slot < 0)
    {
        return tr->instrument;
    }
    if (static_cast<std::uint32_t>(dest_slot) < tr->chain_len)
    {
        return tr->chain[dest_slot];
    }
    return nullptr;
}

}
