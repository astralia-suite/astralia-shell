#pragma once

#include <functional>
#include <string>
#include <vector>

#include "modules/lock/model.h"

#include "render/canvas.h"

#include "service/battery_service.h"
#include "service/media_service.h"
#include "service/notification_service.h"
#include "service/telemetry_service.h"

namespace astralia {

struct LockInfo {
    std::string user;
    std::string os;
    std::string wm;
    std::string uptime;
    std::string hour;
    std::string minute;
    std::string date;
    const BatteryStatus *battery = nullptr;
    const MediaStatus *media = nullptr;
    const SystemStatsState *stats = nullptr;
    const CpuTempState *cpu_temp = nullptr;
    const GpuTempState *gpu_temp = nullptr;
    const std::vector<Notification> *notifications = nullptr;
};

// Draws the avatar into its box and returns true, or returns false to fall back to the static image.
using LockAvatarArt = std::function<bool(ui::Canvas &, const ui::Box &)>;

void paint_lock(ui::Canvas &canvas, LockModel &model, LockMotion &motion, const LockInfo &info, float width, float height, const LockAvatarArt &avatar = {});

} // namespace astralia
