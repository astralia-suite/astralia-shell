#include <algorithm>

#include "wayland/app/module_registry.h"
#include "wayland/app/monitor_output.h"

#include "wayland/core/log.h"

#include "core/animation.h"

#include "service/wayland/settings_service.h"

void monitor_output_destroy(MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->destroy(*mon.app, mon);
    app_detail::rest_egl_current(*mon.app);
    if (mon.output.wl)
        wl_output_release(mon.output.wl);
}

MonitorOutput *find_monitor_by_name_wl(WaylandState &app, wl_output *wl) {
    for (auto &mon : app.outputs)
        if (mon->output.wl == wl)
            return mon.get();
    return nullptr;
}

MonitorOutput *find_monitor_for_surface(WaylandState &app, wl_surface *surface) {
    if (!surface)
        return nullptr;
    for (auto &mon : app.outputs)
        for (auto &m : mon->modules)
            if (m->owns_surface(surface))
                return mon.get();
    return nullptr;
}

void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon) {
    mon.modules = build_per_monitor_modules(app.capabilities);
    for (auto &m : mon.modules)
        m->create_surface(app, mon, mon.output.wl);
}

void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon) {
    for (;;) {
        bool all_configured = true;
        for (auto &m : mon.modules)
            if (!m->configured())
                all_configured = false;
        if (all_configured)
            return;
        wl_display_dispatch(app.display);
    }
}

void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->init_egl(app, mon);
}

void monitor_output_activate(WaylandState &app, MonitorOutput &mon) {
    if (mon.activated)
        return;
    klog("output: activating '%s'", mon.output.name.c_str());
    monitor_output_create_surfaces(app, mon);
    monitor_output_wait_configured(app, mon);
    monitor_output_finish_egl(app, mon);
    mon.activated = true;
}

void request_all_frames(MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->request_frame();
}

namespace app_detail {

void rest_egl_current(WaylandState &app) {
    eglMakeCurrent(app.egl_display, app.egl_rest_surface, app.egl_rest_surface, app.egl_context);
}

const std::vector<astralia::Workspace> &monitor_workspaces(const MonitorOutput &mon) {
    static const std::vector<astralia::Workspace> empty;
    if (!compositor_available(*mon.app))
        return empty;
    auto it = mon.app->desktop->state().by_monitor.find(mon.output.name);
    return it != mon.app->desktop->state().by_monitor.end() ? it->second.workspaces : empty;
}

int monitor_active_workspace_id(const MonitorOutput &mon) {
    if (!compositor_available(*mon.app))
        return -1;
    auto it = mon.app->desktop->state().by_monitor.find(mon.output.name);
    return it != mon.app->desktop->state().by_monitor.end() ? it->second.active_id : -1;
}

void apply_config_update(WaylandState &app, Config new_cfg) {
    bool idle_changed =
        app.cfg.idle_management_enabled != new_cfg.idle_management_enabled || app.cfg.ambient_enabled != new_cfg.ambient_enabled || app.cfg.ambient_timeout_seconds != new_cfg.ambient_timeout_seconds || app.cfg.screensaver_enabled != new_cfg.screensaver_enabled || app.cfg.screensaver_timeout_seconds != new_cfg.screensaver_timeout_seconds || app.cfg.monitor_overrides != new_cfg.monitor_overrides;
    if (idle_changed) {
        std::vector<std::string> names;
        for (auto &mon : app.outputs)
            names.push_back(mon->output.name);
        idle_reset(app.idle, names);
    }

    for (auto &mon : app.outputs)
        for (auto &m : mon->modules)
            m->apply_config(app, *mon, new_cfg);

    astralia::animation_set_instant(new_cfg.animations_disabled);

    app.cfg = new_cfg;
    for (auto &m : app.overlays)
        m->apply_config(app, app.cfg);
    for (auto &mon : app.outputs)
        for (auto &m : mon->modules)
            m->request_frame();
}

void save_and_apply_config_update(WaylandState &app, Config new_cfg) {
    apply_config_update(app, new_cfg);
    settings_service_save(app.cfg);
    app.config_own_write_pending = true;
}

MonitorOutput *active_target_monitor(WaylandState &app) {
    std::vector<Output *> outputs;
    for (auto &mon : app.outputs)
        outputs.push_back(&mon->output);
    std::string focused_name =
        compositor_available(app)
            ? app.desktop->state().focused_monitor
            : std::string();
    wl_output *pointer_hint = app.last_pointer_monitor ? app.last_pointer_monitor->output.wl : nullptr;
    wl_output *target =
        active_output_select(outputs, focused_name, pointer_hint);
    return target ? find_monitor_by_name_wl(app, target) : nullptr;
}

} // namespace app_detail
