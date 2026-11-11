#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "modules/notification/view.h"
#include "modules/notification/wayland/notification.h"

namespace astralia::notification {

namespace {

void layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
                             uint32_t, uint32_t) {
    auto *view = static_cast<View *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    view->configured = true;
}

void layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

} // namespace

bool view_create_surface(View &view, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                         wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "astralia-shell-notification",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .width = surface_width,
        .height = surface_height,
        .margin_right = margin_right,
        .margin_bottom = margin_bottom,
        .empty_input_region = true,
    };
    view.layer_surface = layer_surface_create(view.surface, compositor, layer_shell, cfg,
                                              &layer_surface_listener, &view, output);
    if (!view.layer_surface)
        return false;
    view.compositor = compositor;
    view.output_scale.on_change = [&view](int32_t scale) {
        if (view.egl_window)
            wl_egl_window_resize(view.egl_window, surface_width * scale, surface_height * scale, 0,
                                 0);
        if (view.frame_clock.surface)
            request_frame(view.frame_clock);
    };
    output_scale_watch(view.output_scale, view.surface);
    wl_surface_commit(view.surface);
    return true;
}

bool view_init_egl(View &view, Model &service, Renderer &renderer, EGLDisplay display,
                   EGLConfig config, EGLContext context) {
    view.egl_display = display;
    view.egl_context = context;
    view.renderer = &renderer;
    view.canvas.bind(renderer);
    int32_t scale = view.output_scale.scale;
    view.egl_window =
        wl_egl_window_create(view.surface, surface_width * scale, surface_height * scale);
    view.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(view.egl_window), nullptr);
    if (view.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, view.egl_surface, context))
        return false;
    view.frame_clock.surface = view.surface;
    view.frame_clock.draw = [&view, &service] { render(view, service); };
    return true;
}

void view_request_frame(View &view) {
    if (view.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(view.frame_clock);
}

void render(View &view, Model &model) {
    if (!gl_make_current(view.egl_display, view.egl_surface, view.egl_context))
        return;
    view.renderer->begin_frame(surface_width, surface_height, view.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::steady_clock::now();
    model.tick(now);
    view.state.tick(now);
    view.state.forget_missing(model.entries());

    view.canvas.begin(view.output_scale.scale);
    Layout layout =
        layout_notifications(view.canvas, model, view.state, static_cast<float>(surface_height));
    float origin = static_cast<float>(surface_height) - layout.height;
    view.canvas.begin_group({0.0f, origin, static_cast<float>(surface_width), layout.height}, {});
    notification::paint(view.canvas, view.state, layout);
    view.canvas.end_group();

    auto on_surface = [origin](Boxes boxes) {
        for (auto &[id, box] : boxes)
            box.y += origin;
        return boxes;
    };
    view.close_hitboxes = on_surface(notification::close_boxes(layout, view.state));
    view.card_hitboxes = on_surface(notification::card_boxes(layout, view.state));
    Boxes input = on_surface(notification::input_boxes(layout, view.state));
    if (view.compositor && input != view.input_hitboxes) {
        view.input_hitboxes = input;
        wl_region *region = wl_compositor_create_region(view.compositor);
        for (const auto &[id, r] : input)
            wl_region_add(region, static_cast<int>(r.x), static_cast<int>(r.y),
                          static_cast<int>(r.w), static_cast<int>(r.h));
        wl_surface_set_input_region(view.surface, region);
        wl_region_destroy(region);
    }

    view.canvas.flush();
    if (!eglSwapBuffers(view.egl_display, view.egl_surface))
        klog("notification: eglSwapBuffers failed, egl error 0x%04x", eglGetError());

    if (model.animating() || view.state.animating())
        request_frame(view.frame_clock);
}

bool view_set_hover(View &view, double x, double y) {
    return view.state.set_pointer(view.close_hitboxes, view.card_hitboxes, x, y);
}

bool view_clear_hover(View &view) { return view.state.clear_pointer(); }

bool PerMonitor::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (notifications_enabled_for(app.cfg, mon.output.name) &&
        !view_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("notification: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool PerMonitor::configured() const { return !state_.layer_surface || state_.configured; }

bool PerMonitor::init_egl(WaylandState &app, MonitorOutput &) {
    if (state_.layer_surface && view_init_egl(state_, app.notification, app.renderer,
                                              app.egl_display, app.egl_config, app.egl_context))
        app_detail::rest_egl_current(app);
    return true;
}

void PerMonitor::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window,
                          state_.egl_surface, &state_.frame_clock);
}

bool PerMonitor::owns_surface(wl_surface *surface) const { return surface == state_.surface; }

void PerMonitor::request_frame() { view_request_frame(state_); }

void PerMonitor::handle_click(WaylandState &app, MonitorOutput &, wl_surface *, int button,
                              double x, double y, uint32_t) {
    if (button != BTN_LEFT)
        return;
    if (state_.state.press(state_.close_hitboxes, state_.card_hitboxes, x, y, *app.notifications))
        view_request_frame(state_);
}

void PerMonitor::handle_pointer_move(WaylandState &app, MonitorOutput &, double x, double y) {
    bool changed = app.pointer.focused_surface == state_.surface ? view_set_hover(state_, x, y)
                                                                 : view_clear_hover(state_);
    if (changed)
        view_request_frame(state_);
}

bool PerMonitor::wants_hand_cursor() const { return state_.state.wants_hand(); }

void PerMonitor::apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) {
    bool want = notifications_enabled_for(new_cfg, mon.output.name);
    bool have = state_.layer_surface != nullptr;
    if (want && !have) {
        if (view_create_surface(state_, app.compositor, app.layer_shell, mon.output.wl)) {
            while (!state_.configured)
                wl_display_dispatch(app.display);
            if (view_init_egl(state_, app.notification, app.renderer, app.egl_display,
                              app.egl_config, app.egl_context))
                app_detail::rest_egl_current(app);
        }
    } else if (!want && have) {
        destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface,
                              state_.egl_window, state_.egl_surface, &state_.frame_clock);
        state_.configured = false;
    }
}

} // namespace astralia::notification
