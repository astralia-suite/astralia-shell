#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "config/bamboo_config.h"

#include "modules/bamboo/bamboo_grow.h"

#include "wayland/render/renderer.h"
#include "wayland/render/texture.h"
#include "wayland/render/toplevel_window.h"

#include "service/wayland/input_service.h"

struct WaylandState;

struct BambooState {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
    astralia::BambooGrow grow;
    Texture texture;
    int built_width = 0;
    int built_height = 0;
    std::chrono::steady_clock::time_point last_tick;
};

void bamboo_request_frame(BambooState &state);

void bamboo_toggle(BambooState &state, WaylandState &app);

void bamboo_handle_key_event(BambooState &state, WaylandState &app, const KeyEvent &event);

std::vector<astralia::ShellBinding> bamboo_shell_bindings(BambooState &bamboo, WaylandState &state);

void bamboo_paint(BambooState &state);

std::unique_ptr<Module> make_bamboo_module();
