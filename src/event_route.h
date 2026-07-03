#pragma once

#include <cstdint>

#include <clap/events.h>
#include <music_router/ring.h>

#include "config.h"
#include "graph.h"

namespace mr
{

class PluginNode;

union ClapEventAny
{
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_note_expression_t expr;
    clap_event_param_value_t param_value;
    clap_event_param_mod_t param_mod;
    clap_event_midi_t midi;
    clap_event_midi2_t midi2;
};

struct NodeBucket
{
    ClapEventAny events[kMaxEventsPerBlock];
    std::uint32_t count;
};

clap_input_events_t make_input_events(NodeBucket* bucket);
const clap_output_events_t* null_output_events();

void drain_and_demux(SpscRing& ring, const GraphState& g, NodeBucket* buckets,
                     std::uint32_t block_start, std::uint32_t frames, bool drop_realtime,
                     std::uint32_t& last_time);

}
