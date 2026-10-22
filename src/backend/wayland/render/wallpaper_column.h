#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "wayland/render/gl_canvas.h"
#include "wayland/render/texture.h"
#include "wayland/render/video_texture.h"

#include "service/wayland/media_service.h"

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

enum class FillMode { Crop, Fit };

struct WallpaperColumnGl {
    EGLDisplay display = nullptr;
    std::function<void()> make_current;
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
    std::function<void()> on_uploaded;

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

void wallpaper_column_draw(const WallpaperColumn &col, GlCanvas &canvas, float x, float column_w,
                           float height);

bool wallpaper_column_draw_transition(Renderer &renderer, WallpaperColumn &col, float x, float y,
                                      float column_w, float height);

void wallpaper_column_upload_pending(WallpaperColumn &col, const WallpaperColumnGl &gl);

void wallpaper_column_clear(WallpaperColumn &col, const WallpaperColumnGl &gl);

void wallpaper_column_set_static(WallpaperColumn &col, const WallpaperColumnGl &gl,
                                 const std::string &path, int target_w, int target_h,
                                 FillMode mode);

void wallpaper_column_set_animated(WallpaperColumn &col, const WallpaperColumnGl &gl,
                                   const std::string &path, int target_w, int target_h,
                                   FillMode mode);

MediaDecodeStatus wallpaper_column_decode_status(const WallpaperColumn &col);
