#include <GLES3/gl32.h>
#include <algorithm>
#include <cmath>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "modules/osd/view.h"
#include "modules/osd/wayland/osd.h"

namespace astralia::osd {

namespace {

void layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
                             uint32_t, uint32_t) {
    auto *state = static_cast<State *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->configured = true;
}

void layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

void render(State &state) {
    gl_make_current(state.egl_display, state.egl_surface, state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(kSurfaceWidth, kSurfaceHeight, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.model.tick(std::chrono::steady_clock::now());
    state.canvas.begin(scale);
    osd::paint(state.canvas, state.model);
    state.canvas.flush();
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.model.animating())
        request_frame(state.frame_clock);
}

} // namespace

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "astralia-shell-osd",

        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
        .width = kSurfaceWidth,
        .height = kSurfaceHeight,
        .margin_bottom = kMarginBottom,
        .empty_input_region = true,
    };
    state.layer_surface = layer_surface_create(state.surface, compositor, layer_shell, cfg,
                                               &layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, kSurfaceWidth * scale, kSurfaceHeight * scale, 0,
                                 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);
    return true;
}

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    int32_t scale = state.output_scale.scale;
    state.egl_window =
        wl_egl_window_create(state.surface, kSurfaceWidth * scale, kSurfaceHeight * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { render(state); };
    return true;
}

void request_frame(State &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void show(State &state, OsdKind kind, int percent, bool muted) {
    if (state.model.show(kind, percent, muted, std::chrono::steady_clock::now()))
        osd::request_frame(state);
}

void hide(State &state) {
    state.model.hide();
    osd::request_frame(state);
}

namespace {

class PerMonitor final : public PerMonitorModule {
  public:
    State &state() { return state_; }

    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void tick(WaylandState &app, MonitorOutput &mon) override;

  private:
    State state_;
};

bool PerMonitor::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (!osd::create_surface(state_, app.compositor, app.layer_shell, output))
        klog("osd: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool PerMonitor::configured() const { return !state_.layer_surface || state_.configured; }

bool PerMonitor::init_egl(WaylandState &app, MonitorOutput &) {
    if (state_.layer_surface &&
        osd::init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        app_detail::rest_egl_current(app);
    return true;
}

void PerMonitor::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window,
                          state_.egl_surface, &state_.frame_clock);
}

bool PerMonitor::owns_surface(wl_surface *surface) const { return surface == state_.surface; }

void PerMonitor::tick(WaylandState &, MonitorOutput &) {
    if (state_.model.visible() && std::chrono::steady_clock::now() >= state_.model.hide_at())
        osd::hide(state_);
}

} // namespace

std::unique_ptr<PerMonitorModule> make_per_monitor_module() {
    return std::make_unique<PerMonitor>();
}

void show_on_monitors(WaylandState &app, OsdKind kind, int percent, bool muted) {
    for (auto &mon : app.outputs) {
        if (!osd_enabled_for(app.cfg, mon->output.name))
            continue;
        auto *m = mon->module<PerMonitor>();
        if (!m)
            continue;
        osd::show(m->state(), kind, percent, muted);
    }
}

} // namespace astralia::osd
