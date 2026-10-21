#include <GLES3/gl32.h>
#include <algorithm>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "core/deferred_call.h"
#include "wayland/core/log.h"

#include "config/lock_config.h"

#include "modules/lock/transition_view.h"

#include "modules/lock/wayland/transition.h"
#include "render/tokens.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

namespace {

constexpr int kTransitionDeadlineMarginMs = 1500;
constexpr int kTransitionReadyMs = 400;

void transition_paint(LockTransition &transition, TransitionSurface &surface);

void finish(LockTransition &transition) {
    if (!transition.playing)
        return;
    transition.playing = false;
    transition.finishing = false;
    std::function<void()> done = std::move(transition.on_finished);
    transition.on_finished = nullptr;
    if (done)
        done();
}

void check_ready(LockTransition &transition) {
    if (!transition.on_ready)
        return;
    for (auto &surface : transition.surfaces)
        if (!surface->held)
            return;
    std::function<void(bool)> ready = std::move(transition.on_ready);
    transition.on_ready = nullptr;
    astralia::DeferredCall::call_later([ready = std::move(ready)] { ready(true); });
}

void hold(LockTransition &transition, TransitionSurface &surface) {
    WaylandState &app = *transition.app;
    if (!gl_make_current(app.egl_display, surface.egl_surface, app.egl_context))
        return;
    int32_t scale = surface.output_scale.scale;
    app.renderer.begin_frame(surface.width, surface.height, scale);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    surface.canvas.begin(scale);
    if (transition.draw_lock)
        transition.draw_lock(surface.output_name, surface.canvas, surface.width, surface.height,
                             surface.motion);
    surface.canvas.flush();
    surface.canvas.snapshot();
    gl_check("lock_transition_hold");
    if (!eglSwapBuffers(app.egl_display, surface.egl_surface)) {
        klog("lock: transition hold eglSwapBuffers failed on '%s'", surface.output_name.c_str());
        return;
    }
    surface.snapshotted = true;
    surface.held = true;
    check_ready(transition);
}

void surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
                       uint32_t width, uint32_t height) {
    auto *surface = static_cast<TransitionSurface *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    LockTransition *transition = surface->owner;
    int32_t scale = surface->output_scale.scale;
    surface->width = static_cast<int32_t>(width);
    surface->height = static_cast<int32_t>(height);

    if (surface->egl_surface == EGL_NO_SURFACE) {
        WaylandState &app = *transition->app;
        surface->egl_window =
            wl_egl_window_create(surface->surface, surface->width * scale, surface->height * scale);
        surface->egl_surface = eglCreateWindowSurface(
            app.egl_display, app.egl_config,
            reinterpret_cast<EGLNativeWindowType>(surface->egl_window), nullptr);
        if (surface->egl_surface == EGL_NO_SURFACE) {
            klog("lock: transition eglCreateWindowSurface failed on '%s'",
                 surface->output_name.c_str());
            return;
        }
        surface->canvas.bind(app.renderer);
        surface->frame_clock.surface = surface->surface;
        surface->frame_clock.draw = [transition, surface] {
            transition_paint(*transition, *surface);
        };
    } else if (surface->egl_window) {
        wl_egl_window_resize(surface->egl_window, surface->width * scale, surface->height * scale,
                             0, 0);
    }
    surface->plan = std::make_unique<astralia::SlashPlan>(
        transition->seed, static_cast<float>(surface->width), static_cast<float>(surface->height));
    surface->snapshotted = false;
    surface->held = false;
    surface->configured = true;
    if (transition->mode == astralia::LockTransitionMode::unlock && !transition->playing)
        hold(*transition, *surface);
    else
        request_frame(surface->frame_clock);
    app_detail::rest_egl_current(*transition->app);
}

void surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener kSurfaceListener = {
    .configure = surface_configure,
    .closed = surface_closed,
};

void transition_paint(LockTransition &transition, TransitionSurface &surface) {
    if (!transition.playing || !surface.configured || surface.egl_surface == EGL_NO_SURFACE ||
        !surface.plan)
        return;
    using clock = std::chrono::steady_clock;
    WaylandState &app = *transition.app;

    if (!transition.running) {
        transition.running = true;
        transition.started = clock::now();
        klog("lock: transition first frame (%s)",
             transition.mode == astralia::LockTransitionMode::unlock ? "unlock" : "lock");
    }
    float elapsed =
        std::chrono::duration<float, std::milli>(clock::now() - transition.started).count();
    float progress = astralia::slash_run_progress(elapsed);

    if (!gl_make_current(app.egl_display, surface.egl_surface, app.egl_context))
        return;
    int32_t scale = surface.output_scale.scale;
    app.renderer.begin_frame(surface.width, surface.height, scale);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    astralia::Color core = astralia::with_alpha(astralia::palette::white, 1.0f);
    astralia::Color glow = astralia::with_alpha(astralia::palette::electro, 1.0f);
    astralia::LockThunder thunder;
    thunder.strike = [&](const astralia::SlashStrike &strike) {
        ThunderParams params;
        params.ax = strike.a.x;
        params.ay = strike.a.y;
        params.bx = strike.b.x;
        params.by = strike.b.y;
        params.time_s = strike.time_s;
        params.progress = strike.progress;
        params.intensity = strike.intensity;
        params.seed = strike.seed;
        params.amp = strike.amp;
        params.thick = strike.thick;
        params.core = astralia::rgba(core);
        params.glow = astralia::rgba(glow);
        thunder_burst_draw(transition.thunder, app.renderer, params);
    };
    thunder.shock = [&](const astralia::SlashShock &shock) {
        ThunderShockParams params;
        params.cx = shock.center.x;
        params.cy = shock.center.y;
        params.radius = shock.radius;
        params.time_s = shock.time_s;
        params.progress = shock.progress;
        params.intensity = shock.intensity;
        params.core = astralia::rgba(core);
        params.glow = astralia::rgba(glow);
        thunder_shock_draw(transition.thunder, app.renderer, params);
    };

    surface.canvas.begin(scale);
    if (!surface.snapshotted) {
        if (transition.mode == astralia::LockTransitionMode::unlock) {
            if (transition.draw_lock)
                transition.draw_lock(surface.output_name, surface.canvas, surface.width,
                                     surface.height, surface.motion);
        } else if (transition.draw_wallpaper)
            transition.draw_wallpaper(surface.output_name, surface.canvas, surface.width,
                                      surface.height);
        surface.canvas.flush();
        surface.canvas.snapshot();
        glClear(GL_COLOR_BUFFER_BIT);
        surface.snapshotted = true;
        surface.canvas.begin(scale);
    }
    astralia::paint_lock_transition(
        surface.canvas, *surface.plan, progress,
        [&transition, &surface] {
            if (transition.draw_lock)
                transition.draw_lock(surface.output_name, surface.canvas, surface.width,
                                     surface.height, surface.motion);
            else if (transition.draw_wallpaper)
                transition.draw_wallpaper(surface.output_name, surface.canvas, surface.width,
                                          surface.height);
        },
        thunder, transition.mode);
    surface.canvas.flush();
    gl_check("lock_transition_paint");
    if (!eglSwapBuffers(app.egl_display, surface.egl_surface))
        klog("lock: transition eglSwapBuffers failed on '%s'", surface.output_name.c_str());

    if (elapsed < kLockSlashMs) {
        request_frame(surface.frame_clock);
    } else if (!transition.finishing && &surface == transition.surfaces.front().get()) {
        transition.finishing = true;
        astralia::DeferredCall::call_later([&transition] { finish(transition); });
    }
}

void destroy_surface(WaylandState &app, TransitionSurface &surface) {
    if (surface.egl_surface != EGL_NO_SURFACE &&
        gl_make_current(app.egl_display, surface.egl_surface, app.egl_context))
        surface.canvas.release_snapshot();
    destroy_layer_surface(app.egl_display, surface.surface, surface.layer_surface,
                          surface.egl_window, surface.egl_surface, &surface.frame_clock);
    surface.frame_clock.surface = nullptr;
    surface.frame_clock.mapped = false;
    surface.configured = false;
}

} // namespace

bool transition_open(LockTransition &transition, WaylandState &app,
                     astralia::LockTransitionMode mode) {
    if (transition.active || !app.layer_shell)
        return false;
    transition.app = &app;
    transition.mode = mode;
    transition.ready_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransitionReadyMs);
    transition.seed = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
    transition.playing = false;
    transition.running = false;
    transition.finishing = false;

    for (auto &monitor : app.outputs) {
        auto surface = std::make_unique<TransitionSurface>();
        surface->owner = &transition;
        surface->output = monitor->output.wl;
        surface->output_name = monitor->output.name;
        LayerSurfaceConfig config{
            .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
            .name_space = "astralia-shell-lock-transition",
            .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
            .empty_input_region = true,
        };
        surface->layer_surface =
            layer_surface_create(surface->surface, app.compositor, app.layer_shell, config,
                                 &kSurfaceListener, surface.get(), surface->output);
        if (!surface->layer_surface) {
            klog("lock: transition layer surface failed on '%s'", surface->output_name.c_str());
            if (surface->surface)
                wl_surface_destroy(surface->surface);
            continue;
        }
        TransitionSurface *raw = surface.get();
        surface->output_scale.on_change = [raw](int32_t scale) {
            if (raw->egl_window)
                wl_egl_window_resize(raw->egl_window, raw->width * scale, raw->height * scale, 0,
                                     0);
            if (raw->frame_clock.surface)
                request_frame(raw->frame_clock);
        };
        output_scale_watch(surface->output_scale, surface->surface);
        wl_surface_commit(surface->surface);
        transition.surfaces.push_back(std::move(surface));
    }
    if (transition.surfaces.empty())
        return false;
    transition.active = true;
    wl_display_flush(app.display);
    klog("lock: transition opened (%zu surface(s))", transition.surfaces.size());
    return true;
}

void transition_release_keyboard(LockTransition &transition) {
    if (!transition.active || !transition.app)
        return;
    for (auto &surface : transition.surfaces) {
        if (!surface->layer_surface)
            continue;
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            surface->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        wl_surface_commit(surface->surface);
    }
    wl_display_flush(transition.app->display);
}

void transition_rehold(LockTransition &transition, astralia::LockTransitionMode mode) {
    transition.mode = mode;
    transition.ready_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransitionReadyMs);
    transition.running = false;
    transition.finishing = false;
    for (auto &surface : transition.surfaces) {
        surface->held = false;
        surface->snapshotted = false;
        if (surface->configured && surface->egl_surface != EGL_NO_SURFACE)
            hold(transition, *surface);
    }
    app_detail::rest_egl_current(*transition.app);
}

bool transition_play(LockTransition &transition, std::function<void()> on_finished) {
    if (!transition.active || transition.playing || !transition.app)
        return false;
    using clock = std::chrono::steady_clock;
    transition.on_finished = std::move(on_finished);
    transition.seed = static_cast<uint32_t>(clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
    transition.running = false;
    transition.finishing = false;
    transition.playing = true;
    transition.deadline = clock::now() + std::chrono::milliseconds(static_cast<int>(kLockSlashMs)) +
                          std::chrono::milliseconds(kTransitionDeadlineMarginMs);

    for (auto &surface : transition.surfaces) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            surface->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        if (transition.mode == astralia::LockTransitionMode::lock)
            surface->snapshotted = false;
        if (surface->configured) {
            surface->plan = std::make_unique<astralia::SlashPlan>(
                transition.seed, static_cast<float>(surface->width),
                static_cast<float>(surface->height));
            frame_clock_drop_callback(surface->frame_clock);
            request_frame(surface->frame_clock);
        } else {
            wl_surface_commit(surface->surface);
        }
    }
    wl_display_flush(transition.app->display);
    klog("lock: transition playing");
    return true;
}

void transition_check_deadline(LockTransition &transition) {
    if (transition.on_ready && std::chrono::steady_clock::now() > transition.ready_deadline) {
        klog("lock: transition overlays not ready, giving up");
        std::function<void(bool)> ready = std::move(transition.on_ready);
        transition.on_ready = nullptr;
        ready(false);
        return;
    }
    if (transition.playing && std::chrono::steady_clock::now() > transition.deadline) {
        klog("lock: transition overdue, finishing it");
        finish(transition);
    }
}

void transition_stop(LockTransition &transition) {
    if (transition.app) {
        for (auto &surface : transition.surfaces)
            destroy_surface(*transition.app, *surface);
        app_detail::rest_egl_current(*transition.app);
        wl_display_flush(transition.app->display);
    }
    transition.surfaces.clear();
    transition.active = false;
    transition.playing = false;
    transition.running = false;
    transition.finishing = false;
    transition.on_ready = nullptr;
}

void transition_output_removed(LockTransition &transition, wl_output *output) {
    if (!transition.active || !transition.app)
        return;
    auto it = std::find_if(
        transition.surfaces.begin(), transition.surfaces.end(),
        [output](const std::unique_ptr<TransitionSurface> &s) { return s->output == output; });
    if (it == transition.surfaces.end())
        return;
    bool was_front = it == transition.surfaces.begin();
    destroy_surface(*transition.app, **it);
    transition.surfaces.erase(it);
    if (transition.on_ready) {
        if (transition.surfaces.empty()) {
            std::function<void(bool)> ready = std::move(transition.on_ready);
            transition.on_ready = nullptr;
            astralia::DeferredCall::call_later([ready = std::move(ready)] { ready(false); });
        } else {
            check_ready(transition);
        }
    }
    if (transition.playing && (transition.surfaces.empty() || was_front))
        astralia::DeferredCall::call_later([&transition] { finish(transition); });
}
