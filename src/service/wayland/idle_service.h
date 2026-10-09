#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

struct IdleState {
    ext_idle_notifier_v1 *notifier = nullptr;

    ext_idle_notification_v1 *recent_activity_notification = nullptr;
    bool recent_activity_idled = false;
    std::map<std::string, std::chrono::steady_clock::time_point> last_activity;
};

bool idle_init(IdleState &state, wl_seat *seat);

void idle_tick(IdleState &state, const std::string &focused_monitor);

void idle_reset(IdleState &state, const std::vector<std::string> &monitor_names);

bool is_idle(const IdleState &state, const std::string &monitor, uint32_t timeout_seconds);
