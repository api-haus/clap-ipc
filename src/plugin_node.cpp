#include "plugin_node.h"

#include <cassert>
#include <cstdio>

#include "dl_compat.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/state.h>
#include <clap/factory/plugin-factory.h>
#include <clap/version.h>

#include "invariants.h"
#include "notification_center.h"

namespace mr
{

namespace
{

std::uint32_t choose_dialect(std::uint32_t supported)
{
    if (supported & CLAP_NOTE_DIALECT_CLAP)
    {
        return CLAP_NOTE_DIALECT_CLAP;
    }
    if (supported & CLAP_NOTE_DIALECT_MIDI2)
    {
        return CLAP_NOTE_DIALECT_MIDI2;
    }
    if (supported & CLAP_NOTE_DIALECT_MIDI_MPE)
    {
        return CLAP_NOTE_DIALECT_MIDI_MPE;
    }
    if (supported & CLAP_NOTE_DIALECT_MIDI)
    {
        return CLAP_NOTE_DIALECT_MIDI;
    }
    return CLAP_NOTE_DIALECT_CLAP;
}

std::int64_t CLAP_ABI file_istream_read(const clap_istream_t* stream, void* buffer, std::uint64_t size)
{
    auto* file = static_cast<std::FILE*>(stream->ctx);
    std::size_t n = std::fread(buffer, 1, size, file);
    if (n == 0 && std::ferror(file))
    {
        return -1;
    }
    return static_cast<std::int64_t>(n);
}

}

std::unique_ptr<PluginNode> PluginNode::create(const char* path, std::uint32_t plugin_index,
                                              bool is_instrument, double sample_rate,
                                              std::uint32_t max_block, NotificationCenter* center)
{
    MR_ASSERT_CONTROL_THREAD();

    void* dso = dl_open(path);
    if (dso == nullptr)
    {
        std::fprintf(stderr, "[clap-ipc] dlopen failed: %s\n", dl_last_error());
        return nullptr;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*>(dl_symbol(dso, "clap_entry"));
    if (entry == nullptr || !clap_version_is_compatible(entry->clap_version))
    {
        dl_close(dso);
        return nullptr;
    }
    if (!entry->init(path))
    {
        dl_close(dso);
        return nullptr;
    }

    auto node = std::unique_ptr<PluginNode>(new PluginNode());
    node->dso_ = dso;
    node->entry_ = entry;
    node->is_instrument_ = is_instrument;
    node->host_ctx_.bind(center, node.get());

    auto* factory =
        static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || plugin_index >= factory->get_plugin_count(factory))
    {
        return nullptr;
    }
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, plugin_index);
    if (desc == nullptr || desc->id == nullptr)
    {
        return nullptr;
    }

    node->plugin_ = factory->create_plugin(factory, node->host_ctx_.handle(), desc->id);
    if (node->plugin_ == nullptr)
    {
        return nullptr;
    }
    if (!node->plugin_->init(node->plugin_))
    {
        return nullptr;
    }

    node->scan_ports();

    if (!node->plugin_->activate(node->plugin_, sample_rate, 1, max_block))
    {
        return nullptr;
    }
    node->state_ = NodeState::Activated;
    return node;
}

PluginNode::~PluginNode()
{
    MR_ASSERT_CONTROL_THREAD();
    assert(state_ != NodeState::Processing && "node must be stopped before destruction");
    if (plugin_ != nullptr)
    {
        if (gui_created_ && gui_ != nullptr)
        {
            gui_->destroy(plugin_);
            gui_created_ = false;
        }
        if (state_ == NodeState::Activated)
        {
            plugin_->deactivate(plugin_);
        }
        plugin_->destroy(plugin_);
    }
    if (entry_ != nullptr)
    {
        entry_->deinit();
    }
    if (dso_ != nullptr)
    {
        dl_close(dso_);
    }
}

void PluginNode::scan_ports()
{
    MR_ASSERT_CONTROL_THREAD();
    out_channels_ = 0;
    in_channels_ = 0;
    note_dialect_ = 0;

    params_ = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    gui_ = static_cast<const clap_plugin_gui_t*>(plugin_->get_extension(plugin_, CLAP_EXT_GUI));

    auto* ports =
        static_cast<const clap_plugin_audio_ports_t*>(plugin_->get_extension(plugin_, CLAP_EXT_AUDIO_PORTS));
    if (ports != nullptr)
    {
        std::uint32_t n_out = ports->count(plugin_, false);
        for (std::uint32_t i = 0; i < n_out; ++i)
        {
            clap_audio_port_info_t info;
            if (ports->get(plugin_, i, false, &info))
            {
                if (i == 0 || (info.flags & CLAP_AUDIO_PORT_IS_MAIN))
                {
                    out_channels_ = info.channel_count;
                }
                if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                {
                    break;
                }
            }
        }
        std::uint32_t n_in = ports->count(plugin_, true);
        for (std::uint32_t i = 0; i < n_in; ++i)
        {
            clap_audio_port_info_t info;
            if (ports->get(plugin_, i, true, &info))
            {
                if (i == 0 || (info.flags & CLAP_AUDIO_PORT_IS_MAIN))
                {
                    in_channels_ = info.channel_count;
                }
                if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                {
                    break;
                }
            }
        }
    }

    if (is_instrument_)
    {
        auto* notes = static_cast<const clap_plugin_note_ports_t*>(
            plugin_->get_extension(plugin_, CLAP_EXT_NOTE_PORTS));
        if (notes != nullptr && notes->count(plugin_, true) > 0)
        {
            clap_note_port_info_t info;
            if (notes->get(plugin_, 0, true, &info))
            {
                note_dialect_ = choose_dialect(info.supported_dialects);
            }
        }
    }
}

void PluginNode::start_processing()
{
    MR_ASSERT_AUDIO_THREAD();
    assert(state_ == NodeState::Activated);
    if (plugin_->start_processing(plugin_))
    {
        state_ = NodeState::Processing;
    }
}

void PluginNode::stop_processing()
{
    MR_ASSERT_AUDIO_THREAD();
    if (state_ == NodeState::Processing)
    {
        plugin_->stop_processing(plugin_);
        state_ = NodeState::Activated;
    }
}

clap_process_status PluginNode::process(const clap_process_t* p)
{
    MR_ASSERT_AUDIO_THREAD();
    assert(state_ == NodeState::Processing);
    return plugin_->process(plugin_, p);
}

bool PluginNode::load_state(const char* path)
{
    MR_ASSERT_CONTROL_THREAD();
    auto* state =
        static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
    if (state == nullptr)
    {
        return false;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return false;
    }
    clap_istream_t stream{file, file_istream_read};
    bool ok = state->load(plugin_, &stream);
    std::fclose(file);
    return ok;
}

void PluginNode::pump_main_thread()
{
    MR_ASSERT_CONTROL_THREAD();
    if (host_ctx_.take_callback_requested())
    {
        plugin_->on_main_thread(plugin_);
    }
}

std::uint32_t PluginNode::param_count() const
{
    MR_ASSERT_CONTROL_THREAD();
    return params_ != nullptr ? params_->count(plugin_) : 0u;
}

bool PluginNode::param_info(std::uint32_t index, MrParamInfo* out, char* name, std::size_t name_cap,
                            char* module, std::size_t module_cap) const
{
    MR_ASSERT_CONTROL_THREAD();
    if (params_ == nullptr)
    {
        return false;
    }
    clap_param_info_t inf;
    if (!params_->get_info(plugin_, index, &inf))
    {
        return false;
    }
    out->id = inf.id;
    out->flags = inf.flags;
    out->min_value = inf.min_value;
    out->max_value = inf.max_value;
    out->default_value = inf.default_value;
    double v = inf.default_value;
    params_->get_value(plugin_, inf.id, &v);
    out->current_value = v;
    if (name != nullptr && name_cap > 0)
    {
        std::snprintf(name, name_cap, "%s", inf.name);
    }
    if (module != nullptr && module_cap > 0)
    {
        std::snprintf(module, module_cap, "%s", inf.module);
    }
    return true;
}

bool PluginNode::param_info_by_id(std::uint32_t param_id, MrParamInfo* out, char* name,
                                  std::size_t name_cap, char* module, std::size_t module_cap) const
{
    MR_ASSERT_CONTROL_THREAD();
    if (params_ == nullptr)
    {
        return false;
    }
    std::uint32_t n = params_->count(plugin_);
    for (std::uint32_t i = 0; i < n; ++i)
    {
        clap_param_info_t inf;
        if (params_->get_info(plugin_, i, &inf) && inf.id == param_id)
        {
            return param_info(i, out, name, name_cap, module, module_cap);
        }
    }
    return false;
}

bool PluginNode::param_value(std::uint32_t param_id, double* out) const
{
    MR_ASSERT_CONTROL_THREAD();
    return params_ != nullptr && params_->get_value(plugin_, param_id, out);
}

bool PluginNode::param_value_text(std::uint32_t param_id, double value, char* buf,
                                  std::size_t cap) const
{
    MR_ASSERT_CONTROL_THREAD();
    return params_ != nullptr
        && params_->value_to_text(plugin_, param_id, value, buf, static_cast<std::uint32_t>(cap));
}

bool PluginNode::show_gui()
{
    MR_ASSERT_CONTROL_THREAD();
    if (gui_ == nullptr)
    {
        return false;
    }
    if (!gui_created_)
    {
        if (!gui_->create(plugin_, nullptr, true) && !gui_->create(plugin_, CLAP_WINDOW_API_X11, true))
        {
            return false;
        }
        gui_created_ = true;
    }
    return gui_->show(plugin_);
}

void PluginNode::hide_gui()
{
    MR_ASSERT_CONTROL_THREAD();
    if (gui_ != nullptr && gui_created_)
    {
        gui_->hide(plugin_);
    }
}

}
