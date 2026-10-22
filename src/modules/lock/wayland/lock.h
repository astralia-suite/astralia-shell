#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/module.h"
#include "wayland/render/gl_canvas.h"

#include "config/lock_config.h"

#include "modules/lock/model.h"
#include "modules/lock/wayland/transition.h"
#include "modules/lock/wayland/wallpaper.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/input_service.h"
#include "service/wayland/output_service.h"

#include "ext-session-lock-v1-client-protocol.h"

struct WaylandState;

namespace astralia::lock {

struct State;

struct OutputSurface {
    State *owner = nullptr;
    wl_output *output = nullptr;
    std::string output_name;
    wl_surface *surface = nullptr;
    ext_session_lock_surface_v1 *lock_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    int32_t height = 0;
    bool configured = false;
    bool panel_gated = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    Motion motion;
    Hits hits;

    OutputSurface() = default;
    OutputSurface(const OutputSurface &) = delete;
    OutputSurface &operator=(const OutputSurface &) = delete;
};

struct State {
    WaylandState *app = nullptr;
    ext_session_lock_v1 *lock = nullptr;
    bool active = false;
    bool locked = false;
    bool unlocking = false;
    bool acknowledge_startup = false;
    std::chrono::steady_clock::time_point locked_at{};

    std::vector<std::unique_ptr<OutputSurface>> surfaces;

    Model model;
    Transition transition;
    Wallpaper wallpaper;
    std::string user;

    std::function<void(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h)>
        draw_wallpaper;
    std::function<bool(const std::string &output_name)> panel_gated_for;
};

bool request(State &st, WaylandState &app);
void lock_begin(State &st, WaylandState &app, bool animate = true);
void teardown(State &st);
void begin_unlock(State &st);
void handle_key(State &st, const KeyEvent &ev);
void handle_click(State &st, wl_surface *surf, double x, double y);
void timer_tick(State &st);
void hotplug_add(State &st, wl_output *output, const char *name);
void hotplug_remove(State &st, wl_output *output);
wl_surface *focused_surface(const State &st);
bool owns_surface(const State &st, wl_surface *s);

using WallpaperDrawFn = std::function<void(WaylandState &app, const std::string &output_name,
                                           GlCanvas &canvas, int32_t w, int32_t h)>;

std::unique_ptr<Module> make_module(WallpaperDrawFn draw_wallpaper);
void at_startup(WaylandState &app);
void notify_output_added(WaylandState &app, wl_output *output, const char *name);
void notify_output_removed(WaylandState &app, wl_output *output);

} // namespace astralia::lock
