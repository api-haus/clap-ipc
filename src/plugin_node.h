#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <clap/entry.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/plugin.h>
#include <clap/process.h>

#include <music_router/params.h>

#include "host_context.h"

namespace mr
{

class NotificationCenter;

enum class NodeState
{
    Created,
    Activated,
    Processing
};

class PluginNode
{
public:
    static std::unique_ptr<PluginNode> create(const char* path, std::uint32_t plugin_index,
                                              bool is_instrument, double sample_rate,
                                              std::uint32_t max_block, NotificationCenter* center);
    ~PluginNode();

    PluginNode(const PluginNode&) = delete;
    PluginNode& operator=(const PluginNode&) = delete;

    void start_processing();
    void stop_processing();
    clap_process_status process(const clap_process_t* p);

    bool load_state(const char* path);
    void pump_main_thread();

    std::uint32_t param_count() const;
    bool param_info(std::uint32_t index, MrParamInfo* out, char* name, std::size_t name_cap,
                    char* module, std::size_t module_cap) const;
    bool param_info_by_id(std::uint32_t param_id, MrParamInfo* out, char* name, std::size_t name_cap,
                          char* module, std::size_t module_cap) const;
    bool param_value(std::uint32_t param_id, double* out) const;
    bool param_value_text(std::uint32_t param_id, double value, char* buf, std::size_t cap) const;

    bool show_gui();
    void hide_gui();

    NodeState state() const noexcept { return state_; }
    bool is_instrument() const noexcept { return is_instrument_; }
    std::uint32_t out_channels() const noexcept { return out_channels_; }
    std::uint32_t in_channels() const noexcept { return in_channels_; }
    std::uint32_t note_dialect() const noexcept { return note_dialect_; }

    int bucket_index = -1;

private:
    PluginNode() = default;
    void scan_ports();

    void* dso_ = nullptr;
    const clap_plugin_entry_t* entry_ = nullptr;
    const clap_plugin_t* plugin_ = nullptr;
    const clap_plugin_params_t* params_ = nullptr;
    const clap_plugin_gui_t* gui_ = nullptr;
    HostContext host_ctx_;
    NodeState state_ = NodeState::Created;
    bool is_instrument_ = false;
    bool gui_created_ = false;
    std::uint32_t out_channels_ = 0;
    std::uint32_t in_channels_ = 0;
    std::uint32_t note_dialect_ = 0;
};

}
