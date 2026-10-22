#pragma once

#include <EGL/egl.h>
#include <cstdint>
#include <utility>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/per_monitor_module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/renderer.h"

#include "modules/notification/model.h"
#include "modules/notification/wayland/notification_style_config.h"

#include "render/geometry.h"

#include "service/notification_service.h"
#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace astralia::notification {

struct View {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    wl_compositor *compositor = nullptr;
    Renderer *renderer = nullptr;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    ViewState state;
    std::vector<std::pair<uint32_t, astralia::ui::Box>> close_hitboxes;
};

bool view_create_surface(View &view, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                         wl_output *output = nullptr);

bool view_init_egl(View &view, Model &service, Renderer &renderer, EGLDisplay display,
                   EGLConfig config, EGLContext context);

void view_request_frame(View &view);

bool view_handle_close_click(View &view, double x, double y);

bool view_set_close_hover(View &view, double x, double y);

bool view_clear_close_hover(View &view);

void render(View &view, Model &service);

class PerMonitor final : public PerMonitorModule {
  public:
    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;
    void handle_click(WaylandState &app, MonitorOutput &mon, wl_surface *surface, int button,
                      double x, double y, uint32_t serial) override;
    void handle_pointer_move(WaylandState &app, MonitorOutput &mon, double x, double y) override;
    bool wants_hand_cursor() const override;

    void apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) override;

  private:
    View state_;
};

} // namespace astralia::notification
