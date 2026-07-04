#pragma once

#include <cstdint>
#include <vector>

#include <clap/events.h>
#include <music_router/ring.h>

#include "config.h"
#include "graph.h"

namespace mr
{

class PluginNode;

class EventReorderQueue
{
public:
    explicit EventReorderQueue(std::uint32_t capacity);

    bool push(const MrEvent& ev) noexcept;
    const MrEvent* peek_min() const noexcept;
    void pop_min() noexcept;
    void clear() noexcept { size_ = 0; }
    bool empty() const noexcept { return size_ == 0; }

private:
    struct Slot
    {
        MrEvent ev;
        std::uint64_t seq;
    };

    static bool less(const Slot& a, const Slot& b) noexcept;

    std::vector<Slot> heap_;
    std::uint32_t cap_;
    std::uint32_t size_ = 0;
    std::uint64_t next_seq_ = 0;
};

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

void drain_and_demux(SpscRing& ring, EventReorderQueue& reorder, const GraphState& g,
                     NodeBucket* buckets, std::uint32_t block_start, std::uint32_t frames,
                     bool drop_realtime);

}
