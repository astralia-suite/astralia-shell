#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/config.h"
#include "wayland/app/per_monitor_module.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/texture.h"
#include "wayland/render/video_texture.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/media_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

inline constexpr const char *kWallpaperLayerNamespace = "astralia-shell-wallpaper";

enum class WallpaperTransition : uint8_t {
    None,
    Fade,
    Wipe,
    Disc,
    Stripes,
    Zoom,
    Honeycomb,
    Random,
};

// transition timing
inline constexpr float kWallpaperTransitionDurationMs = 900.0f;
inline constexpr float kWallpaperTransitionSmoothness = 0.3f;

class Renderer;
struct WaylandState;

enum class FillMode { Crop,
                      Fit };

struct WallpaperColumnGl {
    EGLDisplay display = nullptr;
    EGLContext context = nullptr;
    EGLSurface surface = EGL_NO_SURFACE;
    std::function<void()> request_frame;
};

struct WallpaperColumn {
    Texture tex;
    uint64_t generation = 0;

    unsigned char *pending_pixels = nullptr;
    int pending_width = 0;
    int pending_height = 0;
    int pending_stride = 0;

    MediaDecodePlayback decode;
    std::string path;
    FillMode mode = FillMode::Crop;
    std::unordered_map<uint64_t, VideoTexture> video_texs;
    uint64_t video_surface = 0;
    void *pinned_frame = nullptr;
    void *pinned_frame_prev = nullptr;
    bool zero_copy = false;

    int target_w = 0;
    int target_h = 0;

    std::shared_ptr<int> life = std::make_shared<int>(0);

    Texture tex_prev;
    WallpaperTransition pending_transition = WallpaperTransition::None;
    bool transitioning = false;
    WallpaperTransition transition_kind = WallpaperTransition::None;
    std::chrono::steady_clock::time_point transition_start{};
    float tr_direction = 0.0f;
    float tr_center_x = 0.5f;
    float tr_center_y = 0.5f;
    float tr_stripe_count = 12.0f;
    float tr_angle = 30.0f;
    float tr_cell_size = 0.04f;

    WallpaperColumn() = default;
    WallpaperColumn(const WallpaperColumn &) = delete;
    WallpaperColumn &operator=(const WallpaperColumn &) = delete;
    ~WallpaperColumn() { delete[] pending_pixels; }
};

struct WallpaperState {
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

    std::function<void()> on_resize;
};

bool wallpaper_create_surface(WallpaperState &wp, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool wallpaper_init_egl(WallpaperState &wp, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context);

void wallpaper_request_frame(WallpaperState &wp);

void wallpaper_wake(WallpaperState &wp);

void wallpaper_draw_columns(const WallpaperState &wp, GlCanvas &canvas, int32_t width, int32_t height);

void wallpaper_sync_from_config(WallpaperState &wp, const Config &cfg, const std::string &monitor_name, bool animated);

void wallpaper_columns_stop_all(WallpaperState &wp);
void wallpaper_columns_pause_all(WallpaperState &wp);
void wallpaper_columns_resume_all(WallpaperState &wp);

MediaDecodeStatus wallpaper_column_status(const WallpaperState &wp, int column_index);

class WallpaperPerMonitorModule final : public PerMonitorModule {
  public:
    bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;

    void apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) override;

    void pause_animation();
    void resume_animation();
    MediaDecodeStatus decode_status(int column_index) const;
    const WallpaperState &wallpaper_state() const { return state_; }

  private:
    WallpaperState state_;
};
