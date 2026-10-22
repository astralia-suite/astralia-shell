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

#include "modules/lock/model.h"
#include "modules/lock/slash.h"
#include "modules/lock/transition_view.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/thunder.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

namespace astralia::lock {

struct Transition;

struct TransitionSurface {
    Transition *owner = nullptr;
    wl_output *output = nullptr;
    std::string output_name;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    int32_t height = 0;
    bool configured = false;
    bool snapshotted = false;
    bool held = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    Motion motion;
    std::unique_ptr<SlashPlan> plan;

    TransitionSurface() = default;
    TransitionSurface(const TransitionSurface &) = delete;
    TransitionSurface &operator=(const TransitionSurface &) = delete;
};

struct Transition {
    WaylandState *app = nullptr;
    bool active = false;
    bool playing = false;
    bool running = false;
    bool finishing = false;
    TransitionMode mode = TransitionMode::lock;
    uint32_t seed = 0;
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::steady_clock::time_point ready_deadline{};
    std::vector<std::unique_ptr<TransitionSurface>> surfaces;
    ThunderBurst thunder;
    std::function<void(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h)>
        draw_wallpaper;
    std::function<void(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h,
                       Motion &motion)>
        draw_lock;
    std::function<bool(const std::string &output_name)> wallpaper_ready;
    std::function<void()> on_finished;
    std::function<void(bool held)> on_ready;
};

bool transition_open(Transition &transition, WaylandState &app,
                     TransitionMode mode = TransitionMode::lock);
void transition_release_keyboard(Transition &transition);
void transition_rehold(Transition &transition, TransitionMode mode);
bool transition_play(Transition &transition, std::function<void()> on_finished = {});
void transition_stop(Transition &transition);
void transition_check_deadline(Transition &transition);
void transition_output_removed(Transition &transition, wl_output *output);

} // namespace astralia::lock
