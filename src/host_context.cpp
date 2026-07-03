#include "host_context.h"

#include <cstdio>
#include <cstring>

#include <clap/ext/gui.h>
#include <clap/ext/log.h>
#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/ext/thread-check.h>
#include <clap/version.h>

#include "invariants.h"
#include "notification_center.h"

namespace mr
{

namespace
{

void CLAP_ABI host_log(const clap_host_t*, clap_log_severity severity, const char* msg)
{
    static const char* const names[] = {"debug",           "info",
                                         "warning",         "error",
                                         "fatal",           "host-misbehaving",
                                         "plugin-misbehaving"};
    const char* name = (severity >= 0 && severity <= 6) ? names[severity] : "log";
    std::fprintf(stderr, "[clap-ipc plugin %s] %s\n", name, msg ? msg : "");
}

bool CLAP_ABI host_is_main_thread(const clap_host_t*)
{
    return on_control_thread();
}

bool CLAP_ABI host_is_audio_thread(const clap_host_t*)
{
    return on_audio_thread();
}

NotificationCenter* center_of(const clap_host_t* host, PluginNode*& owner)
{
    auto* self = static_cast<HostContext*>(host->host_data);
    return self->notification_target(owner);
}

void CLAP_ABI host_params_rescan(const clap_host_t* host, std::uint32_t flags)
{
    PluginNode* owner = nullptr;
    NotificationCenter* center = center_of(host, owner);
    if (center != nullptr)
    {
        center->on_rescan(owner, flags);
    }
}

void CLAP_ABI host_params_clear(const clap_host_t*, clap_id, clap_param_clear_flags)
{
}

void CLAP_ABI host_params_request_flush(const clap_host_t*)
{
}

void CLAP_ABI host_gui_resize_hints_changed(const clap_host_t*)
{
}

bool CLAP_ABI host_gui_request_resize(const clap_host_t*, std::uint32_t, std::uint32_t)
{
    return false;
}

bool CLAP_ABI host_gui_request_show(const clap_host_t*)
{
    return false;
}

bool CLAP_ABI host_gui_request_hide(const clap_host_t*)
{
    return false;
}

void CLAP_ABI host_gui_closed(const clap_host_t* host, bool)
{
    PluginNode* owner = nullptr;
    NotificationCenter* center = center_of(host, owner);
    if (center != nullptr)
    {
        center->on_gui_closed(owner);
    }
}

void CLAP_ABI host_preset_on_error(const clap_host_t*, std::uint32_t, const char* location,
                                   const char*, std::int32_t, const char* msg)
{
    std::fprintf(stderr, "[clap-ipc preset-load error] %s: %s\n", location ? location : "",
                 msg ? msg : "");
}

void CLAP_ABI host_preset_loaded(const clap_host_t*, std::uint32_t, const char*, const char*)
{
}

const clap_host_log_t g_host_log = {host_log};
const clap_host_thread_check_t g_host_thread_check = {host_is_main_thread, host_is_audio_thread};
const clap_host_params_t g_host_params = {host_params_rescan, host_params_clear,
                                          host_params_request_flush};
const clap_host_gui_t g_host_gui = {host_gui_resize_hints_changed, host_gui_request_resize,
                                    host_gui_request_show, host_gui_request_hide, host_gui_closed};
const clap_host_preset_load_t g_host_preset_load = {host_preset_on_error, host_preset_loaded};

}

HostContext::HostContext()
{
    host_.clap_version = CLAP_VERSION;
    host_.host_data = this;
    host_.name = "music-router";
    host_.vendor = "clap-router";
    host_.url = "";
    host_.version = "1.0";
    host_.get_extension = &HostContext::get_extension;
    host_.request_restart = &HostContext::request_restart;
    host_.request_process = &HostContext::request_process;
    host_.request_callback = &HostContext::request_callback;
}

const void* CLAP_ABI HostContext::get_extension(const clap_host_t*, const char* id)
{
    if (id == nullptr)
    {
        return nullptr;
    }
    if (std::strcmp(id, CLAP_EXT_LOG) == 0)
    {
        return &g_host_log;
    }
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0)
    {
        return &g_host_thread_check;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
    {
        return &g_host_params;
    }
    if (std::strcmp(id, CLAP_EXT_GUI) == 0)
    {
        return &g_host_gui;
    }
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
    {
        return &g_host_preset_load;
    }
    return nullptr;
}

void CLAP_ABI HostContext::request_restart(const clap_host_t*)
{
}

void CLAP_ABI HostContext::request_process(const clap_host_t*)
{
}

void CLAP_ABI HostContext::request_callback(const clap_host_t* host)
{
    auto* self = static_cast<HostContext*>(host->host_data);
    self->callback_requested_.store(true, std::memory_order_release);
}

}
