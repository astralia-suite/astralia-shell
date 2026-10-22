#pragma once

#include <memory>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"
#include "wayland/render/renderer.h"
#include "wayland/render/toplevel_window.h"

#include "config/dashboard_config.h"

#include "service/wayland/input_service.h"

struct WaylandState;

namespace astralia::dashboard {

struct State {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
};

void request_frame(State &state);

void toggle(State &state, WaylandState &app);

void handle_key_event(State &state, WaylandState &app, const KeyEvent &event);

std::vector<astralia::ShellBinding> shell_bindings(State &dashboard, WaylandState &state);

void render(State &state);

std::unique_ptr<Module> make_module();

} // namespace astralia::dashboard
