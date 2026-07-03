#include "event_route.h"

#include <cassert>

#include <clap/ext/note-ports.h>

#include "invariants.h"
#include "plugin_node.h"

namespace mr
{

namespace
{

std::uint32_t CLAP_ABI input_size(const clap_input_events_t* list)
{
    return static_cast<const NodeBucket*>(list->ctx)->count;
}

const clap_event_header_t* CLAP_ABI input_get(const clap_input_events_t* list, std::uint32_t index)
{
    return &static_cast<const NodeBucket*>(list->ctx)->events[index].header;
}

bool CLAP_ABI null_try_push(const clap_output_events_t*, const clap_event_header_t*)
{
    return true;
}

const clap_output_events_t g_null_output = {nullptr, null_try_push};

void fill_header(clap_event_header_t& h, std::uint32_t size, std::uint32_t time, std::uint16_t type,
                 std::uint32_t flags)
{
    h.size = size;
    h.time = time;
    h.space_id = CLAP_CORE_EVENT_SPACE_ID;
    h.type = type;
    h.flags = flags;
}

bool translate_event(const MrEvent& ev, PluginNode* target, std::uint32_t time, std::uint32_t flags,
                     ClapEventAny& out)
{
    switch (ev.kind)
    {
    case MR_EV_NOTE_ON:
    case MR_EV_NOTE_OFF:
    case MR_EV_NOTE_CHOKE:
        fill_header(out.note.header, sizeof(clap_event_note_t), time,
                    static_cast<std::uint16_t>(ev.kind), flags);
        out.note.note_id = ev.u.note.note_id;
        out.note.port_index = ev.u.note.port;
        out.note.channel = ev.u.note.channel;
        out.note.key = ev.u.note.key;
        out.note.velocity = ev.u.note.velocity;
        return true;

    case MR_EV_NOTE_EXPR:
    {
        std::uint32_t dialect = target ? target->note_dialect() : CLAP_NOTE_DIALECT_CLAP;
        if (dialect == 0 || dialect == CLAP_NOTE_DIALECT_CLAP)
        {
            fill_header(out.expr.header, sizeof(clap_event_note_expression_t), time,
                        CLAP_EVENT_NOTE_EXPRESSION, flags);
            out.expr.expression_id = static_cast<clap_note_expression>(ev.u.expr.expression_id);
            out.expr.note_id = ev.u.expr.note_id;
            out.expr.port_index = ev.u.expr.port;
            out.expr.channel = ev.u.expr.channel;
            out.expr.key = ev.u.expr.key;
            out.expr.value = ev.u.expr.value;
            return true;
        }
        if (dialect == CLAP_NOTE_DIALECT_MIDI2)
        {
            if (ev.u.expr.expression_id != MR_EXPR_TUNING)
            {
                return false;
            }
            double norm = ev.u.expr.value / 2.0;
            if (norm > 1.0) norm = 1.0;
            if (norm < -1.0) norm = -1.0;
            std::uint32_t data = static_cast<std::uint32_t>(0x80000000u + norm * 0x7FFFFFFF);
            fill_header(out.midi2.header, sizeof(clap_event_midi2_t), time, CLAP_EVENT_MIDI2, flags);
            out.midi2.port_index = ev.u.expr.port < 0 ? 0 : static_cast<std::uint16_t>(ev.u.expr.port);
            std::uint16_t channel = ev.u.expr.channel < 0 ? 0 : (ev.u.expr.channel & 0x0F);
            std::uint16_t key = ev.u.expr.key < 0 ? 0 : (ev.u.expr.key & 0x7F);
            out.midi2.data[0] = (0x4u << 28) | (0x6u << 20) | (channel << 16) | (key << 8);
            out.midi2.data[1] = data;
            out.midi2.data[2] = 0;
            out.midi2.data[3] = 0;
            return true;
        }
        if (ev.u.expr.expression_id != MR_EXPR_TUNING)
        {
            return false;
        }
        double norm = ev.u.expr.value / 2.0;
        if (norm > 1.0) norm = 1.0;
        if (norm < -1.0) norm = -1.0;
        int bend = 8192 + static_cast<int>(norm * 8191.0);
        if (bend < 0) bend = 0;
        if (bend > 16383) bend = 16383;
        std::uint16_t channel = ev.u.expr.channel < 0 ? 0 : (ev.u.expr.channel & 0x0F);
        fill_header(out.midi.header, sizeof(clap_event_midi_t), time, CLAP_EVENT_MIDI, flags);
        out.midi.port_index = ev.u.expr.port < 0 ? 0 : static_cast<std::uint16_t>(ev.u.expr.port);
        out.midi.data[0] = static_cast<std::uint8_t>(0xE0 | channel);
        out.midi.data[1] = static_cast<std::uint8_t>(bend & 0x7F);
        out.midi.data[2] = static_cast<std::uint8_t>((bend >> 7) & 0x7F);
        return true;
    }

    case MR_EV_PARAM:
        fill_header(out.param_value.header, sizeof(clap_event_param_value_t), time,
                    CLAP_EVENT_PARAM_VALUE, flags);
        out.param_value.param_id = ev.u.param.param_id;
        out.param_value.cookie = nullptr;
        out.param_value.note_id = ev.u.param.note_id;
        out.param_value.port_index = -1;
        out.param_value.channel = -1;
        out.param_value.key = -1;
        out.param_value.value = ev.u.param.value;
        return true;

    case MR_EV_PARAM_MOD:
        fill_header(out.param_mod.header, sizeof(clap_event_param_mod_t), time, CLAP_EVENT_PARAM_MOD,
                    flags);
        out.param_mod.param_id = ev.u.param.param_id;
        out.param_mod.cookie = nullptr;
        out.param_mod.note_id = ev.u.param.note_id;
        out.param_mod.port_index = -1;
        out.param_mod.channel = -1;
        out.param_mod.key = -1;
        out.param_mod.amount = ev.u.param.value;
        return true;

    case MR_EV_MIDI1:
        fill_header(out.midi.header, sizeof(clap_event_midi_t), time, CLAP_EVENT_MIDI, flags);
        out.midi.port_index = ev.u.midi.port;
        out.midi.data[0] = static_cast<std::uint8_t>(ev.u.midi.ump[0] & 0xFF);
        out.midi.data[1] = static_cast<std::uint8_t>((ev.u.midi.ump[0] >> 8) & 0xFF);
        out.midi.data[2] = static_cast<std::uint8_t>((ev.u.midi.ump[0] >> 16) & 0xFF);
        return true;

    case MR_EV_MIDI2:
        fill_header(out.midi2.header, sizeof(clap_event_midi2_t), time, CLAP_EVENT_MIDI2, flags);
        out.midi2.port_index = ev.u.midi.port;
        out.midi2.data[0] = ev.u.midi.ump[0];
        out.midi2.data[1] = ev.u.midi.ump[1];
        out.midi2.data[2] = ev.u.midi.ump[2];
        out.midi2.data[3] = ev.u.midi.ump[3];
        return true;

    default:
        return false;
    }
}

}

clap_input_events_t make_input_events(NodeBucket* bucket)
{
    clap_input_events_t list;
    list.ctx = bucket;
    list.size = input_size;
    list.get = input_get;
    return list;
}

const clap_output_events_t* null_output_events()
{
    return &g_null_output;
}

void drain_and_demux(SpscRing& ring, const GraphState& g, NodeBucket* buckets,
                     std::uint32_t block_start, std::uint32_t frames, bool drop_realtime,
                     std::uint32_t& last_time)
{
    MR_ASSERT_AUDIO_THREAD();

    PluginNode* nodes[kMaxNodes];
    std::uint32_t node_count = gather_nodes(g, nodes, kMaxNodes);
    for (std::uint32_t i = 0; i < node_count; ++i)
    {
        nodes[i]->bucket_index = static_cast<int>(i);
        buckets[i].count = 0;
    }

    const std::uint64_t block_end = static_cast<std::uint64_t>(block_start) + frames;
    while (const MrEvent* ev = ring.peek())
    {
        if (static_cast<std::uint64_t>(ev->sample_time) >= block_end)
        {
            break;
        }
        assert(ev->sample_time >= last_time && "producer must push nondecreasing global sample_time");
        last_time = ev->sample_time;

        if (!drop_realtime)
        {
            PluginNode* node = resolve_node(g, ev->track_id, ev->dest_slot);
            if (node != nullptr && node->bucket_index >= 0)
            {
                NodeBucket& b = buckets[node->bucket_index];
                if (b.count < kMaxEventsPerBlock)
                {
                    std::int64_t rel =
                        static_cast<std::int64_t>(ev->sample_time) - static_cast<std::int64_t>(block_start);
                    std::uint32_t time = rel < 0 ? 0u
                                         : (rel >= static_cast<std::int64_t>(frames) ? frames - 1
                                                                                     : static_cast<std::uint32_t>(rel));
                    std::uint32_t flags = (ev->flags & MR_FLAG_IS_LIVE) ? CLAP_EVENT_IS_LIVE : 0u;
                    if (translate_event(*ev, node, time, flags, b.events[b.count]))
                    {
                        ++b.count;
                    }
                }
            }
        }
        ring.pop();
    }
}

}
