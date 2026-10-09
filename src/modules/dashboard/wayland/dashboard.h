#pragma once

#include <memory>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "modules/dashboard/wayland/dashboard_config.h"

#include "wayland/render/renderer.h"
#include "wayland/render/toplevel_window.h"

#include "service/wayland/input_service.h"

struct WaylandState;

struct DashboardState {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
};

void dashboard_request_frame(DashboardState &state);

void dashboard_toggle(DashboardState &state, WaylandState &app);

void dashboard_handle_key_event(DashboardState &state, WaylandState &app, const KeyEvent &event);

std::vector<astralia::ShellBinding> dashboard_shell_bindings(DashboardState &dashboard, WaylandState &state);

void dashboard_paint(DashboardState &state);

std::unique_ptr<Module> make_dashboard_module();
