#include "wayland/config/idle_config.h"

#include "wayland/core/log.h"

#include "service/wayland/idle_service.h"

namespace {

void idle_recent_activity_idled(void *data, ext_idle_notification_v1 *) {
    static_cast<IdleState *>(data)->recent_activity_idled = true;
}

void idle_recent_activity_resumed(void *data, ext_idle_notification_v1 *) {
    static_cast<IdleState *>(data)->recent_activity_idled = false;
}

constexpr ext_idle_notification_v1_listener idle_recent_activity_listener = {
    .idled = idle_recent_activity_idled,
    .resumed = idle_recent_activity_resumed,
};

} // namespace

bool idle_init(IdleState &state, wl_seat *seat) {
    if (!state.notifier || !seat) {
        klog("idle: compositor is missing ext_idle_notifier_v1 or wl_seat, "
             "skipping");
        return false;
    }
    state.recent_activity_notification = ext_idle_notifier_v1_get_idle_notification(state.notifier, kIdleRecentActivityPulseSeconds * 1000, seat);
    if (state.recent_activity_notification)
        ext_idle_notification_v1_add_listener(state.recent_activity_notification, &idle_recent_activity_listener, &state);
    else
        klog("idle: recent-activity notification failed, ambient/screensaver "
             "clock disabled");
    return state.recent_activity_notification != nullptr;
}

void idle_reset(IdleState &state, const std::vector<std::string> &monitor_names) {
    auto now = std::chrono::steady_clock::now();
    for (const auto &name : monitor_names)
        state.last_activity[name] = now;
}

void idle_tick(IdleState &state, const std::string &focused_monitor) {
    if (!state.recent_activity_idled && !focused_monitor.empty())
        state.last_activity[focused_monitor] = std::chrono::steady_clock::now();
}

bool is_idle(const IdleState &state, const std::string &monitor, uint32_t timeout_seconds) {
    auto it = state.last_activity.find(monitor);
    if (it == state.last_activity.end())
        return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second).count();
    return elapsed > static_cast<int64_t>(timeout_seconds);
}
