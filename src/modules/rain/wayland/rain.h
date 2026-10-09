#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "wayland/config/rain_config.h"

#include "modules/rain/wayland/matrix_rain.h"
#include "modules/rain/wayland/stiletto_rain.h"

#include "wayland/render/renderer.h"
#include "wayland/render/toplevel_window.h"

#include "service/wayland/input_service.h"

struct WaylandState;

struct RainState {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
    MatrixRain matrix;
    StilettoRain stiletto;
    RainMode mode = RainMode::Matrix;
    bool async_speed = false;
    int built_width = 0;
    int built_height = 0;
    std::chrono::steady_clock::time_point last_tick;
};

void rain_request_frame(RainState &state);

void rain_toggle(RainState &state, WaylandState &app);

void rain_handle_key_event(RainState &state, WaylandState &app, const KeyEvent &event);

void rain_apply_params(RainState &state, const RainParams &params);

std::vector<astralia::ShellBinding> rain_shell_bindings(RainState &rain, WaylandState &state);

void rain_paint(RainState &state);

std::unique_ptr<Module> make_rain_module();
