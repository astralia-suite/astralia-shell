#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/notification/view.h"

#include "modules/notification/wayland/notification.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

namespace {

void notification_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t, uint32_t) {
    auto *view = static_cast<NotificationView *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    view->configured = true;
}

void notification_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener notification_layer_surface_listener = {
    .configure = notification_layer_surface_configure,
    .closed = notification_layer_surface_closed,
};

} // namespace

bool notification_view_create_surface(NotificationView &view, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "astralia-shell-notification",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .width = kNotificationSurfaceWidth,
        .height = kNotificationSurfaceHeight,
        .margin_right = kNotificationMarginRight,
        .margin_bottom = kNotificationMarginBottom,
        .empty_input_region = true,
    };
    view.layer_surface =
        layer_surface_create(view.surface, compositor, layer_shell, cfg, &notification_layer_surface_listener, &view, output);
    if (!view.layer_surface)
        return false;
    view.compositor = compositor;
    view.output_scale.on_change = [&view](int32_t scale) {
        if (view.egl_window)
            wl_egl_window_resize(view.egl_window, kNotificationSurfaceWidth * scale, kNotificationSurfaceHeight * scale, 0, 0);
        if (view.frame_clock.surface)
            request_frame(view.frame_clock);
    };
    output_scale_watch(view.output_scale, view.surface);
    wl_surface_commit(view.surface);
    return true;
}

bool notification_view_init_egl(NotificationView &view, astralia::NotificationModel &service, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    view.egl_display = display;
    view.egl_context = context;
    view.renderer = &renderer;
    view.canvas.bind(renderer);
    int32_t scale = view.output_scale.scale;
    view.egl_window =
        wl_egl_window_create(view.surface, kNotificationSurfaceWidth * scale, kNotificationSurfaceHeight * scale);
    view.egl_surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(view.egl_window), nullptr);
    if (view.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, view.egl_surface, context))
        return false;
    view.frame_clock.surface = view.surface;
    view.frame_clock.draw = [&view, &service] { notification_paint(view, service); };
    return true;
}

void notification_view_request_frame(NotificationView &view) {
    if (view.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(view.frame_clock);
}

void notification_paint(NotificationView &view, astralia::NotificationModel &model) {
    if (!gl_make_current(view.egl_display, view.egl_surface, view.egl_context))
        return;
    view.renderer->begin_frame(kNotificationSurfaceWidth, kNotificationSurfaceHeight, view.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::steady_clock::now();
    model.tick(now);
    view.state.tick(now);
    view.state.forget_missing(model.entries());

    view.canvas.begin(view.output_scale.scale);
    astralia::NotificationLayout layout = astralia::layout_notifications(view.canvas, model, view.state, static_cast<float>(kNotificationSurfaceHeight));
    float origin = static_cast<float>(kNotificationSurfaceHeight) - layout.height;
    view.canvas.begin_group({0.0f, origin, static_cast<float>(kNotificationSurfaceWidth), layout.height}, {});
    astralia::paint_notifications(view.canvas, view.state, layout);
    view.canvas.end_group();

    std::vector<std::pair<uint32_t, astralia::ui::Box>> hitboxes;
    for (const auto &[id, box] : astralia::notification_close_boxes(layout, view.state))
        hitboxes.push_back({id, astralia::ui::Box{box.x, box.y + origin, box.w, box.h}});
    if (view.compositor && hitboxes != view.close_hitboxes) {
        view.close_hitboxes = hitboxes;
        wl_region *region = wl_compositor_create_region(view.compositor);
        for (const auto &[id, r] : hitboxes)
            wl_region_add(region, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w), static_cast<int>(r.h));
        wl_surface_set_input_region(view.surface, region);
        wl_region_destroy(region);
    }

    view.canvas.flush();
    if (!eglSwapBuffers(view.egl_display, view.egl_surface))
        klog("notification: eglSwapBuffers failed, egl error 0x%04x", eglGetError());

    if (model.animating() || view.state.animating())
        request_frame(view.frame_clock);
}

namespace {

std::optional<uint32_t> notification_close_hit(const NotificationView &view, double x, double y) {
    for (const auto &[id, r] : view.close_hitboxes)
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return id;
    return std::nullopt;
}

} // namespace

bool notification_view_handle_close_click(NotificationView &view, double x, double y) {
    auto id = notification_close_hit(view, x, y);
    return id && view.state.hide_locally(*id);
}

bool notification_view_set_close_hover(NotificationView &view, double x, double y) {
    return view.state.set_hover(notification_close_hit(view, x, y));
}

bool notification_view_clear_close_hover(NotificationView &view) {
    return view.state.set_hover(std::nullopt);
}

bool NotificationViewPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    if (notifications_effective_enabled(app.cfg, mon.output.name) && !notification_view_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("notification: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool NotificationViewPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool NotificationViewPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &) {
    if (state_.layer_surface && notification_view_init_egl(state_, app.notification, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        app_detail::rest_egl_current(app);
    return true;
}

void NotificationViewPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window, state_.egl_surface, &state_.frame_clock);
}

bool NotificationViewPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void NotificationViewPerMonitorModule::request_frame() {
    notification_view_request_frame(state_);
}

void NotificationViewPerMonitorModule::handle_click(WaylandState &, MonitorOutput &, wl_surface *, int button, double x, double y, uint32_t) {
    if (button != BTN_LEFT)
        return;
    if (notification_view_handle_close_click(state_, x, y))
        notification_view_request_frame(state_);
}

void NotificationViewPerMonitorModule::handle_pointer_move(WaylandState &app, MonitorOutput &, double x, double y) {
    bool changed = app.pointer.focused_surface == state_.surface ? notification_view_set_close_hover(state_, x, y) : notification_view_clear_close_hover(state_);
    if (changed)
        notification_view_request_frame(state_);
}

bool NotificationViewPerMonitorModule::wants_pointing_hand_cursor() const {
    return state_.state.hovered() != 0;
}

void NotificationViewPerMonitorModule::apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) {
    bool want = notifications_effective_enabled(new_cfg, mon.output.name);
    bool have = state_.layer_surface != nullptr;
    if (want && !have) {
        if (notification_view_create_surface(state_, app.compositor, app.layer_shell, mon.output.wl)) {
            while (!state_.configured)
                wl_display_dispatch(app.display);
            if (notification_view_init_egl(state_, app.notification, app.renderer, app.egl_display, app.egl_config, app.egl_context))
                app_detail::rest_egl_current(app);
        }
    } else if (!want && have) {
        destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window, state_.egl_surface, &state_.frame_clock);
        state_.configured = false;
    }
}
