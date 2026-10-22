#include "wayland/app/module_registry.h"
#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/bamboo/wayland/bamboo.h"
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
#include "modules/wallpaper_select/wayland/wallpaper_select.h"

namespace {

void draw_monitor_wallpaper(MonitorOutput &mon, GlCanvas &canvas, int32_t w, int32_t h) {
    if (auto *wp = mon.module<astralia::wallpaper::PerMonitor>())
        astralia::wallpaper::draw_columns(wp->wallpaper_state(), canvas, w, h);
}

void draw_named_wallpaper(WaylandState &app, const std::string &output_name, GlCanvas &canvas,
                          int32_t w, int32_t h) {
    for (auto &mon : app.outputs) {
        if (mon->output.name != output_name)
            continue;
        draw_monitor_wallpaper(*mon, canvas, w, h);
        return;
    }
}

MediaDecodeStatus wallpaper_decode_status(WaylandState &app, const std::string &output_name,
                                          int column) {
    for (auto &mon : app.outputs) {
        if (mon->output.name != output_name)
            continue;
        if (auto *wp = mon->module<astralia::wallpaper::PerMonitor>())
            return wp->decode_status(column);
    }
    return MediaDecodeStatus::Idle;
}

void set_wallpaper_paused(MonitorOutput &mon, bool paused) {
    auto *wp = mon.module<astralia::wallpaper::PerMonitor>();
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
    modules.push_back(astralia::launcher::make_module());
    modules.push_back(astralia::logout::make_module());
    if (capabilities.dashboard)
        modules.push_back(astralia::dashboard::make_module());
    modules.push_back(astralia::overview::make_module());
    modules.push_back(astralia::settings::make_module(wallpaper_decode_status));
    modules.push_back(astralia::wallpaper_select::make_module());
    if (capabilities.rain)
        modules.push_back(astralia::rain::make_module());
    modules.push_back(astralia::bamboo::make_module());
    if (capabilities.visualizer)
        modules.push_back(astralia::visualizer::make_module());
    if (capabilities.lock)
        modules.push_back(astralia::lock::make_module(draw_named_wallpaper));
    modules.push_back(astralia::polkit::make_module());
    return modules;
}

std::vector<std::unique_ptr<PerMonitorModule>>
build_per_monitor_modules(const astralia::Capabilities &capabilities) {
    std::vector<std::unique_ptr<PerMonitorModule>> modules;
    modules.push_back(std::make_unique<astralia::bar::PerMonitor>());
    modules.push_back(std::make_unique<astralia::wallpaper::PerMonitor>());
    modules.push_back(astralia::osd::make_per_monitor_module());
    modules.push_back(std::make_unique<astralia::notification::PerMonitor>());
    if (capabilities.idle)
        modules.push_back(astralia::idle::make_per_monitor_module(
            {draw_monitor_wallpaper, set_wallpaper_paused}));
    return modules;
}
