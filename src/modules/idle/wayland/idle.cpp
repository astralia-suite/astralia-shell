#include <GLES3/gl32.h>
#include <filesystem>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "modules/idle/wayland/idle.h"

#include "render/tokens.h"

#include "service/wayland/idle_service.h"

namespace astralia::idle {

namespace {

void overlay_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface,
                                     uint32_t serial, uint32_t width, uint32_t height) {
    auto *state = static_cast<OverlayState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    bool changed = state->width != static_cast<int32_t>(width) ||
                   state->height != static_cast<int32_t>(height);
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

void overlay_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener overlay_layer_surface_listener = {
    .configure = overlay_layer_surface_configure,
    .closed = overlay_layer_surface_closed,
};

void overlay_bounce(OverlayState &state, float dt) {
    if (dt <= 0.0f || dt > 0.5f)
        return;
    bounce_step(state.bounce, dt, static_cast<float>(state.width), static_cast<float>(state.height),
                astralia::idle_config::kLogoSize);
}

void overlay_paint(OverlayState &state) {
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
    if (state.screensaver_active)
        overlay_bounce(state, dt);

    if (state.ambient_opacity > 0.0f && state.draw_ambient) {
        state.canvas.begin(state.output_scale.scale);
        state.canvas.set_opacity(state.ambient_opacity);
        state.draw_ambient(state.canvas, static_cast<float>(state.width),
                           static_cast<float>(state.height));
        state.canvas.flush();
    }

    if (state.screensaver_opacity > 0.0f) {
        state.canvas.begin(state.output_scale.scale);
        state.canvas.rect({0, 0, static_cast<float>(state.width), static_cast<float>(state.height)},
                          {0.0f, 0.0f, 0.0f, state.screensaver_opacity});
        astralia::ui::ImageId logo =
            state.logo_path.empty()
                ? astralia::ui::no_image
                : state.canvas.image(state.logo_path,
                                     static_cast<int>(astralia::idle_config::kLogoSize) *
                                         state.output_scale.scale);
        if (logo != astralia::ui::no_image) {
            astralia::ui::TextSize extent = state.canvas.image_size(logo);
            float fit = std::min(astralia::idle_config::kLogoSize / extent.w,
                                 astralia::idle_config::kLogoSize / extent.h);
            float w = extent.w * fit;
            float h = extent.h * fit;
            state.canvas.draw_image(
                logo,
                {state.bounce.x + (astralia::idle_config::kLogoSize - w) / 2.0f,
                 state.bounce.y + (astralia::idle_config::kLogoSize - h) / 2.0f, w, h},
                astralia::with_alpha(astralia::palette::white, state.screensaver_opacity));
        }
        state.canvas.flush();
    }

    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.has_active() || state.screensaver_active)
        request_frame(state.frame_clock);
}

} // namespace

bool overlay_create_surface(OverlayState &state, wl_compositor *compositor,
                            zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = idle_overlay_config::kLayerNamespace,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .empty_input_region = true,
    };
    state.layer_surface = layer_surface_create(state.surface, compositor, layer_shell, cfg,
                                               &overlay_layer_surface_listener, &state, output);
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

bool overlay_init_egl(OverlayState &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
                      EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    int32_t scale = state.output_scale.scale;
    state.egl_window =
        wl_egl_window_create(state.surface, state.width * scale, state.height * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { overlay_paint(state); };

    return true;
}

void overlay_request_frame(OverlayState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void overlay_set_active(OverlayState &state, bool ambient_active, bool screensaver_active) {
    bool changed = false;
    bool ambient_shown = ambient_active && !screensaver_active;
    if (ambient_shown != state.ambient_active) {
        state.ambient_active = ambient_shown;
        state.animations.animate(
            state.ambient_opacity, ambient_shown ? 1.0f : 0.0f, idle_overlay_config::kFadeMs,
            astralia::Easing::EaseOutCubic, [&state](float v) { state.ambient_opacity = v; }, {},
            idle_overlay_config::kAmbientFadeOwner);
        changed = true;
    }
    if (screensaver_active != state.screensaver_active) {
        state.screensaver_active = screensaver_active;
        state.animations.animate(
            state.screensaver_opacity, screensaver_active ? 1.0f : 0.0f,
            idle_overlay_config::kFadeMs, astralia::Easing::EaseOutCubic,
            [&state](float v) { state.screensaver_opacity = v; }, {},
            idle_overlay_config::kScreensaverFadeOwner);
        changed = true;
    }
    if (changed)
        overlay_request_frame(state);
}

namespace {

class PerMonitor final : public PerMonitorModule {
  public:
    explicit PerMonitor(WallpaperHooks hooks) : hooks_(std::move(hooks)) {}

    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void timer_tick(WaylandState &app, MonitorOutput &mon) override;

  private:
    WallpaperHooks hooks_;
    OverlayState state_;
    bool was_screensaver_ = false;
};

bool PerMonitor::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (mon.output.name == "HEADLESS")
        return true;
    if (!overlay_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("idle-overlay: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool PerMonitor::configured() const { return !state_.layer_surface || state_.configured; }

bool PerMonitor::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (!state_.layer_surface)
        return true;
    if (!overlay_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        return true;
    state_.draw_ambient = [this, &mon](GlCanvas &canvas, float w, float h) {
        if (hooks_.draw)
            hooks_.draw(mon, canvas, static_cast<int32_t>(w), static_cast<int32_t>(h));
    };
    app_detail::rest_egl_current(app);
    return true;
}

void PerMonitor::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window,
                          state_.egl_surface, &state_.frame_clock);
}

bool PerMonitor::owns_surface(wl_surface *surface) const { return surface == state_.surface; }

void PerMonitor::timer_tick(WaylandState &app, MonitorOutput &mon) {
    if (mon.output.name == "HEADLESS")
        return;
    if (!app.idle.last_activity.count(mon.output.name))
        app.idle.last_activity[mon.output.name] = std::chrono::steady_clock::now();

    bool ambient_now =
        ambient_enabled_for(app.cfg, mon.output.name) &&
        is_idle(app.idle, mon.output.name, ambient_timeout_for(app.cfg, mon.output.name));
    bool screensaver_now =
        screensaver_enabled_for(app.cfg, mon.output.name) &&
        is_idle(app.idle, mon.output.name, screensaver_timeout_for(app.cfg, mon.output.name));

    state_.logo_path = app.cfg.idle_logo_path;
    overlay_set_active(state_, ambient_now, screensaver_now);

    if (screensaver_now != was_screensaver_) {
        was_screensaver_ = screensaver_now;
        if (hooks_.set_paused)
            hooks_.set_paused(mon, screensaver_now);
    }
}

} // namespace

std::unique_ptr<PerMonitorModule> make_per_monitor_module(WallpaperHooks hooks) {
    return std::make_unique<PerMonitor>(std::move(hooks));
}

} // namespace astralia::idle
