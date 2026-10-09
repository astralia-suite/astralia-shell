#include <GLES3/gl32.h>
#include <algorithm>
#include <cmath>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/osd/view.h"

#include "modules/osd/wayland/osd.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

namespace {

void osd_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t, uint32_t) {
    auto *state = static_cast<OsdState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->configured = true;
}

void osd_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener osd_layer_surface_listener = {
    .configure = osd_layer_surface_configure,
    .closed = osd_layer_surface_closed,
};

void osd_paint(OsdState &state) {
    gl_make_current(state.egl_display, state.egl_surface, state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(kOsdSurfaceWidth, kOsdSurfaceHeight, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.model.tick(std::chrono::steady_clock::now());
    state.canvas.begin(scale);
    astralia::paint_osd(state.canvas, state.model);
    state.canvas.flush();
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.model.animating())
        request_frame(state.frame_clock);
}

} // namespace

bool osd_create_surface(OsdState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "astralia-shell-osd",

        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
        .width = kOsdSurfaceWidth,
        .height = kOsdSurfaceHeight,
        .margin_bottom = kOsdMarginBottom,
        .empty_input_region = true,
    };
    state.layer_surface =
        layer_surface_create(state.surface, compositor, layer_shell, cfg, &osd_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, kOsdSurfaceWidth * scale, kOsdSurfaceHeight * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);
    return true;
}

bool osd_init_egl(OsdState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(state.surface, kOsdSurfaceWidth * scale, kOsdSurfaceHeight * scale);
    state.egl_surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { osd_paint(state); };
    return true;
}

void osd_request_frame(OsdState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void osd_show(OsdState &state, astralia::OsdKind kind, int percent, bool muted) {
    if (state.model.show(kind, percent, muted, std::chrono::steady_clock::now()))
        osd_request_frame(state);
}

void osd_hide(OsdState &state) {
    state.model.hide();
    osd_request_frame(state);
}

namespace {

class OsdPerMonitorModule final : public PerMonitorModule {
  public:
    OsdState &state() { return state_; }

    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void tick(WaylandState &app, MonitorOutput &mon) override;

  private:
    OsdState state_;
};

bool OsdPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (!osd_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("osd: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool OsdPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool OsdPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &) {
    if (state_.layer_surface && osd_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        app_detail::rest_egl_current(app);
    return true;
}

void OsdPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window, state_.egl_surface, &state_.frame_clock);
}

bool OsdPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void OsdPerMonitorModule::tick(WaylandState &, MonitorOutput &) {
    if (state_.model.visible() && std::chrono::steady_clock::now() >= state_.model.hide_at())
        osd_hide(state_);
}

} // namespace

std::unique_ptr<PerMonitorModule> make_osd_per_monitor_module() {
    return std::make_unique<OsdPerMonitorModule>();
}

void osd_show_on_monitors(WaylandState &app, astralia::OsdKind kind, int percent, bool muted) {
    for (auto &mon : app.outputs) {
        if (!osd_effective_enabled(app.cfg, mon->output.name))
            continue;
        auto *m = mon->module<OsdPerMonitorModule>();
        if (!m)
            continue;
        osd_show(m->state(), kind, percent, muted);
    }
}
