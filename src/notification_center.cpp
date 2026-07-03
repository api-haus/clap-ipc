#include "notification_center.h"

#include "graph_swap.h"
#include "invariants.h"

namespace mr
{

namespace
{
constexpr std::size_t kMaxNotifications = 2048;
}

void NotificationCenter::enqueue_rescan(std::uint32_t track_id, std::int32_t dest_slot,
                                        std::uint32_t flags)
{
    MR_ASSERT_CONTROL_THREAD();
    for (MrNotification& n : queue_)
    {
        if (n.kind == MR_NOTIFY_PARAM_RESCAN && n.track_id == track_id && n.dest_slot == dest_slot)
        {
            n.rescan_flags |= flags;
            return;
        }
    }
    MrNotification n{};
    n.kind = MR_NOTIFY_PARAM_RESCAN;
    n.track_id = track_id;
    n.dest_slot = dest_slot;
    n.rescan_flags = flags;
    if (queue_.size() >= kMaxNotifications)
    {
        n.rescan_flags = MR_RESCAN_ALL;
        queue_.clear();
    }
    queue_.push_back(n);
}

void NotificationCenter::on_rescan(PluginNode* node, std::uint32_t clap_rescan_flags)
{
    MR_ASSERT_CONTROL_THREAD();
    std::uint32_t track_id = 0;
    std::int32_t dest_slot = 0;
    if (graph_ != nullptr && graph_->locate(node, track_id, dest_slot))
    {
        enqueue_rescan(track_id, dest_slot, clap_rescan_flags);
    }
}

void NotificationCenter::note_state_loaded(std::uint32_t track_id, std::int32_t dest_slot)
{
    MR_ASSERT_CONTROL_THREAD();
    enqueue_rescan(track_id, dest_slot, MR_RESCAN_ALL);
}

void NotificationCenter::on_gui_closed(PluginNode* node)
{
    MR_ASSERT_CONTROL_THREAD();
    std::uint32_t track_id = 0;
    std::int32_t dest_slot = 0;
    if (graph_ == nullptr || !graph_->locate(node, track_id, dest_slot))
    {
        return;
    }
    MrNotification n{};
    n.kind = MR_NOTIFY_GUI_CLOSED;
    n.track_id = track_id;
    n.dest_slot = dest_slot;
    if (queue_.size() < kMaxNotifications)
    {
        queue_.push_back(n);
    }
}

void NotificationCenter::push_param_value(std::uint32_t track_id, std::int32_t dest_slot,
                                          std::uint32_t param_id, double value)
{
    MR_ASSERT_CONTROL_THREAD();
    MrNotification n{};
    n.kind = MR_NOTIFY_PARAM_VALUE;
    n.track_id = track_id;
    n.dest_slot = dest_slot;
    n.param_id = param_id;
    n.value = value;
    if (queue_.size() >= kMaxNotifications)
    {
        enqueue_rescan(track_id, dest_slot, MR_RESCAN_VALUES);
        return;
    }
    queue_.push_back(n);
}

bool NotificationCenter::poll(MrNotification& out)
{
    MR_ASSERT_CONTROL_THREAD();
    ParamEcho e;
    while (echo_.pop(e))
    {
        push_param_value(e.track_id, e.dest_slot, e.param_id, e.value);
    }
    if (queue_.empty())
    {
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

}
