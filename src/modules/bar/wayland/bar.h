#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <memory>
#include <string>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/monitor_output.h"
#include "wayland/app/per_monitor_module.h"
#include "wayland/app/wayland_state.h"

#include "modules/bar/model.h"
#include "modules/bar/wayland/frame.h"
#include "modules/bar/wayland/panel_surface.h"

#include "core/animation.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/renderer.h"

#include "service/compositor_service.h"
#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct AutoHideState {
    bool hidden = false;
    bool collapsed = false;
    float opacity = 1.0f;

    bool enabled = false;
};

struct BarPerMonitorState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    astralia::AnimationManager animations;
    AutoHideState autohide;
    BarDecor decor;

    std::unique_ptr<astralia::BarModel> model;
    std::unique_ptr<PanelSurface> panels;
    std::string clock_label;
    int32_t applied_hug_radius_px = 0;
};

class BarPerMonitorModule final : public PerMonitorModule {
  public:
    BarPerMonitorState state;

    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;
    void apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) override;
    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override;
    void tick(WaylandState &app, MonitorOutput &mon) override;
    void timer_tick(WaylandState &app, MonitorOutput &mon) override;
    bool is_open() const override;
    void handle_click(WaylandState &app, MonitorOutput &mon, wl_surface *surface, int button, double x, double y, uint32_t serial) override;
    void handle_scroll(WaylandState &app, MonitorOutput &mon, wl_surface *surface, double dy) override;
    void handle_key_event(WaylandState &app, MonitorOutput &mon, const KeyEvent &event) override;
    void handle_pointer_move(WaylandState &app, MonitorOutput &mon, double x, double y) override;
    void handle_pointer_release() override;
    bool wants_pointing_hand_cursor() const override;

  private:
    MonitorOutput *mon_ = nullptr;
    double pointer_x_ = -1, pointer_y_ = -1;
};

BarPerMonitorState &bar_state(MonitorOutput &mon);
const BarPerMonitorState &bar_state(const MonitorOutput &mon);

inline const astralia::BarStyleSpec &bar_style_of(const MonitorOutput &mon) {
    return astralia::bar_style_spec(mon.app->cfg.bar_style, true);
}

inline int32_t bar_hug_radius_px(const MonitorOutput &mon) {
    if (mon.app->cfg.bar_style != BarStyle::okinami || bar_state(mon).autohide.enabled)
        return 0;
    return mon.app->desktop->state().hug_radius_px;
}

namespace bar_detail {

void close_other_overlays(MonitorOutput &mon, astralia::BarItem keep);

void toggle_panel(MonitorOutput &mon, astralia::BarItem item);

int32_t bar_current_height(const MonitorOutput &mon);

void bar_autohide_apply_geometry(MonitorOutput &mon, bool autohide, bool collapsed, const astralia::BarStyleSpec &style);

void monitor_autohide_apply(MonitorOutput &mon, bool enabled, const astralia::BarStyleSpec &style);

} // namespace bar_detail

void bar_paint(MonitorOutput &mon);
void bar_request_frame(MonitorOutput &mon);
bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context);
void dispatch_bar_click(MonitorOutput &mon, double click_x, double click_y);
