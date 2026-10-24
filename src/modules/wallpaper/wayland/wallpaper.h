#pragma once

#include <EGL/egl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/config.h"
#include "wayland/app/per_monitor_module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/wallpaper_column.h"

#include "modules/wallpaper/model.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/media_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

class Renderer;
struct WaylandState;

namespace astralia::wallpaper {

inline constexpr const char *layer_namespace = "astralia-shell-wallpaper";

struct State {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    WaylandState *app = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    bool configured = false;
    std::string output_name;
    int dbg_frame = 0;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;

    WallpaperColumnGl gl;
    std::vector<std::unique_ptr<WallpaperColumn>> columns;
    DesktopMenuModel menu;

    std::function<void()> on_resize;
};

bool create_surface(State &wp, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &wp, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context);

void request_frame(State &wp);

void wake(State &wp);

void draw_columns(const State &wp, GlCanvas &canvas, int32_t width, int32_t height);

void wallpaper_sync_from_config(State &wp, const Config &cfg, const std::string &monitor_name,
                                bool animated);

void columns_stop_all(State &wp);
void columns_pause_all(State &wp);
void columns_resume_all(State &wp);

MediaDecodeStatus column_status(const State &wp, int column_index);

class PerMonitor final : public PerMonitorModule {
  public:
    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;

    void apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) override;

    void handle_click(WaylandState &app, MonitorOutput &mon, wl_surface *surface, int button,
                      double x, double y, uint32_t serial) override;
    void handle_pointer_move(WaylandState &app, MonitorOutput &mon, double x, double y) override;
    bool wants_hand_cursor() const override;

    void pause_animation();
    void resume_animation();
    MediaDecodeStatus decode_status(int column_index) const;
    const State &wallpaper_state() const { return state_; }

  private:
    State state_;
};

} // namespace astralia::wallpaper
