#pragma once

#include <string>
#include <vector>

#include "modules/lock/model.h"

#include "render/canvas.h"

#include "service/battery_service.h"
#include "service/media_service.h"
#include "service/notification_service.h"
#include "service/telemetry_service.h"

namespace astralia::lock {

struct Info {
    std::string user;
    std::string os;
    std::string wm;
    std::string uptime;
    std::string hour;
    std::string minute;
    std::string date;
    std::string face;
    const BatteryStatus *battery = nullptr;
    const MediaStatus *media = nullptr;
    const SystemStatsState *stats = nullptr;
    const CpuTempState *cpu_temp = nullptr;
    const GpuTempState *gpu_temp = nullptr;
    const std::vector<Notification> *notifications = nullptr;
};

void paint(ui::Canvas &canvas, Model &model, Motion &motion, const Info &info, float width,
           float height);

} // namespace astralia::lock
