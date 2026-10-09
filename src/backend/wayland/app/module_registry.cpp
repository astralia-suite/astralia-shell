#include "wayland/app/module_registry.h"
#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/bar/wayland/bar.h"
#include "modules/dashboard/wayland/dashboard.h"
#include "modules/idle/wayland/idle.h"
#include "modules/launcher/wayland/launcher.h"
#include "modules/lock/wayland/lock.h"
#include "modules/logout/wayland/logout.h"
#include "modules/notification/wayland/notification.h"
#include "modules/osd/wayland/osd.h"
#include "modules/overview/wayland/overview.h"
#include "modules/polkit/wayland/polkit.h"
#include "modules/rain/wayland/rain.h"
#include "modules/settings/wayland/settings.h"
#include "modules/visualizer/wayland/visualizer.h"
#include "modules/wallpaper/wayland/wallpaper.h"

namespace {

void draw_monitor_wallpaper(MonitorOutput &mon, GlCanvas &canvas, int32_t w, int32_t h) {
    if (auto *wp = mon.module<WallpaperPerMonitorModule>())
        wallpaper_draw_columns(wp->wallpaper_state(), canvas, w, h);
}

void draw_named_wallpaper(WaylandState &app, const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h) {
    for (auto &mon : app.outputs) {
        if (mon->output.name != output_name)
            continue;
        draw_monitor_wallpaper(*mon, canvas, w, h);
        return;
    }
}

MediaDecodeStatus wallpaper_decode_status(WaylandState &app, const std::string &output_name, int column) {
    for (auto &mon : app.outputs) {
        if (mon->output.name != output_name)
            continue;
        if (auto *wp = mon->module<WallpaperPerMonitorModule>())
            return wp->decode_status(column);
    }
    return MediaDecodeStatus::Idle;
}

void set_wallpaper_paused(MonitorOutput &mon, bool paused) {
    auto *wp = mon.module<WallpaperPerMonitorModule>();
    if (!wp)
        return;
    if (paused)
        wp->pause_animation();
    else
        wp->resume_animation();
}

} // namespace

std::vector<std::unique_ptr<Module>> build_app_modules(const astralia::Capabilities &capabilities) {
    std::vector<std::unique_ptr<Module>> modules;
    modules.push_back(make_launcher_module());
    modules.push_back(make_logout_module());
    if (capabilities.dashboard)
        modules.push_back(make_dashboard_module());
    modules.push_back(make_overview_module());
    modules.push_back(make_settings_module(wallpaper_decode_status));
    if (capabilities.rain)
        modules.push_back(make_rain_module());
    if (capabilities.visualizer)
        modules.push_back(make_visualizer_module());
    if (capabilities.lock)
        modules.push_back(make_lock_module(draw_named_wallpaper));
    modules.push_back(make_polkit_module());
    return modules;
}

std::vector<std::unique_ptr<PerMonitorModule>> build_per_monitor_modules(const astralia::Capabilities &capabilities) {
    std::vector<std::unique_ptr<PerMonitorModule>> modules;
    modules.push_back(std::make_unique<BarPerMonitorModule>());
    modules.push_back(std::make_unique<WallpaperPerMonitorModule>());
    modules.push_back(make_osd_per_monitor_module());
    modules.push_back(std::make_unique<NotificationViewPerMonitorModule>());
    if (capabilities.idle)
        modules.push_back(make_idle_per_monitor_module({draw_monitor_wallpaper, set_wallpaper_paused}));
    return modules;
}

void start_session_lock(WaylandState &app) {
    lock_start(app);
}
