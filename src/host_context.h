#pragma once

#include <atomic>

#include <clap/host.h>

namespace mr
{

class NotificationCenter;
class PluginNode;

class HostContext
{
public:
    HostContext();

    HostContext(const HostContext&) = delete;
    HostContext& operator=(const HostContext&) = delete;

    const clap_host_t* handle() const noexcept { return &host_; }

    void bind(NotificationCenter* center, PluginNode* owner) noexcept
    {
        center_ = center;
        owner_ = owner;
    }

    NotificationCenter* notification_target(PluginNode*& owner) noexcept
    {
        owner = owner_;
        return center_;
    }

    bool take_callback_requested() noexcept
    {
        return callback_requested_.exchange(false, std::memory_order_acq_rel);
    }

private:
    static const void* CLAP_ABI get_extension(const clap_host_t* host, const char* id);
    static void CLAP_ABI request_restart(const clap_host_t* host);
    static void CLAP_ABI request_process(const clap_host_t* host);
    static void CLAP_ABI request_callback(const clap_host_t* host);

    clap_host_t host_;
    std::atomic<bool> callback_requested_{false};
    NotificationCenter* center_ = nullptr;
    PluginNode* owner_ = nullptr;
};

}
