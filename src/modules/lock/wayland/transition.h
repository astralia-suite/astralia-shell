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
struct LockTransition;

struct TransitionSurface {
    LockTransition *owner = nullptr;
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
    astralia::LockMotion motion;
    std::unique_ptr<astralia::SlashPlan> plan;

    TransitionSurface() = default;
    TransitionSurface(const TransitionSurface &) = delete;
    TransitionSurface &operator=(const TransitionSurface &) = delete;
};

struct LockTransition {
    WaylandState *app = nullptr;
    bool active = false;
    bool playing = false;
    bool running = false;
    bool finishing = false;
    astralia::LockTransitionMode mode = astralia::LockTransitionMode::lock;
    uint32_t seed = 0;
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::steady_clock::time_point ready_deadline{};
    std::vector<std::unique_ptr<TransitionSurface>> surfaces;
    ThunderBurst thunder;
    std::function<void(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h)>
        draw_wallpaper;
    std::function<void(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h,
                       astralia::LockMotion &motion)>
        draw_lock;
    std::function<void()> on_finished;
    std::function<void(bool held)> on_ready;
};

bool transition_open(LockTransition &transition, WaylandState &app,
                     astralia::LockTransitionMode mode = astralia::LockTransitionMode::lock);
void transition_release_keyboard(LockTransition &transition);
void transition_rehold(LockTransition &transition, astralia::LockTransitionMode mode);
bool transition_play(LockTransition &transition, std::function<void()> on_finished = {});
void transition_stop(LockTransition &transition);
void transition_check_deadline(LockTransition &transition);
void transition_output_removed(LockTransition &transition, wl_output *output);
