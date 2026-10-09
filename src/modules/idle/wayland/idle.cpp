#include <GLES3/gl32.h>
#include <filesystem>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/idle/wayland/idle.h"

#include "render/tokens.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "service/wayland/idle_service.h"

namespace {

void idle_overlay_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
    auto *state = static_cast<IdleOverlayState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    bool changed = state->width != static_cast<int32_t>(width) || state->height != static_cast<int32_t>(height);
    state->width = static_cast<int32_t>(width);
    state->height = static_cast<int32_t>(height);
    if (changed && state->egl_window) {
        int32_t scale = state->output_scale.scale;
        wl_egl_window_resize(state->egl_window, state->width * scale, state->height * scale, 0, 0);
        if (state->frame_clock.surface)
            request_frame(state->frame_clock);
    }
    state->configured = true;
}

void idle_overlay_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener idle_overlay_layer_surface_listener =
    {
        .configure = idle_overlay_layer_surface_configure,
        .closed = idle_overlay_layer_surface_closed,
};

void idle_overlay_bounce(IdleOverlayState &state, float dt) {
    if (dt <= 0.0f || dt > 0.5f || state.logo.frames.empty())
        return;
    float logo_w = kIdleLogoSize;
    float logo_h = kIdleLogoSize;
    state.logo_x += state.logo_vel_x * dt;
    state.logo_y += state.logo_vel_y * dt;
    float max_x = static_cast<float>(state.width) - logo_w;
    float max_y = static_cast<float>(state.height) - logo_h;
    if (state.logo_x < 0.0f) {
        state.logo_x = 0.0f;
        state.logo_vel_x = -state.logo_vel_x;
    } else if (state.logo_x > max_x) {
        state.logo_x = max_x;
        state.logo_vel_x = -state.logo_vel_x;
    }
    if (state.logo_y < 0.0f) {
        state.logo_y = 0.0f;
        state.logo_vel_y = -state.logo_vel_y;
    } else if (state.logo_y > max_y) {
        state.logo_y = max_y;
        state.logo_vel_y = -state.logo_vel_y;
    }
}

void idle_overlay_paint(IdleOverlayState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    gl_make_current(state.egl_display, state.egl_surface, state.egl_context);
    state.renderer->begin_frame(state.width, state.height, state.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::steady_clock::now();
    state.animations.tick(now);
    float dt = std::chrono::duration<float>(now - state.last_tick).count();
    state.last_tick = now;
    if (state.screensaver_active) {
        animated_image_tick(state.logo, now);
        idle_overlay_bounce(state, dt);
    } else if (state.screensaver_opacity <= 0.0f) {
        animated_image_hide(state.logo);
    }

    if (state.ambient_opacity > 0.0f && state.draw_ambient) {
        state.canvas.begin(state.output_scale.scale);
        state.canvas.set_opacity(state.ambient_opacity);
        state.draw_ambient(state.canvas, static_cast<float>(state.width), static_cast<float>(state.height));
        state.canvas.flush();
    }

    if (state.screensaver_opacity > 0.0f) {
        state.canvas.begin(state.output_scale.scale);
        state.canvas.rect({0, 0, static_cast<float>(state.width), static_cast<float>(state.height)}, {0.0f, 0.0f, 0.0f, state.screensaver_opacity});
        animated_image_draw(state.logo, state.canvas, state.logo_x, state.logo_y, kIdleLogoSize, kIdleLogoSize, state.screensaver_opacity);
        state.canvas.flush();
    }

    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive() || state.screensaver_active)
        request_frame(state.frame_clock);
}

} // namespace

bool idle_overlay_create_surface(IdleOverlayState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = kIdleOverlayLayerNamespace,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .empty_input_region = true,
    };
    state.layer_surface = layer_surface_create(state.surface, compositor, layer_shell, cfg, &idle_overlay_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale, state.height * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);
    return true;
}

bool idle_overlay_init_egl(IdleOverlayState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(state.surface, state.width * scale, state.height * scale);
    state.egl_surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { idle_overlay_paint(state); };

    const char *logo_candidates[] = {ASTRALIA_SHELL_IDLE_LOGO, "assets/stellar-restoration.svg"};
    std::string logo_path = logo_candidates[1];
    for (const char *candidate : logo_candidates) {
        if (std::filesystem::exists(candidate)) {
            logo_path = candidate;
            break;
        }
    }
    AnimatedImageStyle logo_style;
    logo_style.size = kIdleLogoSize;
    logo_style.decode = {30, static_cast<int>(kIdleLogoSize), AnimateFit::Fit};
    animated_image_set_source(state.logo, logo_path, logo_style);
    return true;
}

void idle_overlay_request_frame(IdleOverlayState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void idle_overlay_set_active(IdleOverlayState &state, bool ambient_active, bool screensaver_active) {
    bool changed = false;
    bool ambient_shown = ambient_active && !screensaver_active;
    if (ambient_shown != state.ambient_active) {
        state.ambient_active = ambient_shown;
        state.animations.animate(state.ambient_opacity, ambient_shown ? 1.0f : 0.0f, kIdleOverlayFadeMs, astralia::Easing::EaseOutCubic, [&state](float v) { state.ambient_opacity = v; }, {}, kIdleAmbientFadeOwner);
        changed = true;
    }
    if (screensaver_active != state.screensaver_active) {
        state.screensaver_active = screensaver_active;
        if (screensaver_active)
            animated_image_show(state.logo, [&state] { idle_overlay_request_frame(state); });
        state.animations.animate(state.screensaver_opacity, screensaver_active ? 1.0f : 0.0f, kIdleOverlayFadeMs, astralia::Easing::EaseOutCubic, [&state](float v) { state.screensaver_opacity = v; }, {}, kIdleScreensaverFadeOwner);
        changed = true;
    }
    if (changed)
        idle_overlay_request_frame(state);
}

namespace {

class IdlePerMonitorModule final : public PerMonitorModule {
  public:
    explicit IdlePerMonitorModule(IdleWallpaperHooks hooks) : hooks_(std::move(hooks)) {}

    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void timer_tick(WaylandState &app, MonitorOutput &mon) override;

  private:
    IdleWallpaperHooks hooks_;
    IdleOverlayState state_;
    bool screensaver_was_active_ = false;
};

bool IdlePerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (mon.output.name == "HEADLESS")
        return true;
    if (!idle_overlay_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("idle-overlay: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool IdlePerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool IdlePerMonitorModule::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (!state_.layer_surface)
        return true;
    if (!idle_overlay_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        return true;
    state_.draw_ambient = [this, &mon](GlCanvas &canvas, float w, float h) {
        if (hooks_.draw)
            hooks_.draw(mon, canvas, static_cast<int32_t>(w), static_cast<int32_t>(h));
    };
    app_detail::rest_egl_current(app);
    return true;
}

void IdlePerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window, state_.egl_surface, &state_.frame_clock);
}

bool IdlePerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void IdlePerMonitorModule::timer_tick(WaylandState &app, MonitorOutput &mon) {
    if (mon.output.name == "HEADLESS")
        return;
    if (!app.idle.last_activity.count(mon.output.name))
        app.idle.last_activity[mon.output.name] =
            std::chrono::steady_clock::now();

    bool ambient_now =
        ambient_effective_enabled(app.cfg, mon.output.name) && is_idle(app.idle, mon.output.name, ambient_effective_timeout_seconds(app.cfg, mon.output.name));
    bool screensaver_now =
        screensaver_effective_enabled(app.cfg, mon.output.name) && is_idle(app.idle, mon.output.name, screensaver_effective_timeout_seconds(app.cfg, mon.output.name));

    idle_overlay_set_active(state_, ambient_now, screensaver_now);

    if (screensaver_now != screensaver_was_active_) {
        screensaver_was_active_ = screensaver_now;
        if (hooks_.set_paused)
            hooks_.set_paused(mon, screensaver_now);
    }
}

} // namespace

std::unique_ptr<PerMonitorModule> make_idle_per_monitor_module(IdleWallpaperHooks hooks) {
    return std::make_unique<IdlePerMonitorModule>(std::move(hooks));
}
