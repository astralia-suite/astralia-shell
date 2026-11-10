#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

#include "core/animation.h"
#include "core/deferred_call.h"

#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/renderer.h"
#include "wayland/render/wallpaper_column.h"

#include "render/palette_extract.h"
#include "render/tokens.h"

namespace {

AnimateFit to_fit(FillMode mode) {
    return mode == FillMode::Fit ? AnimateFit::Fit : AnimateFit::Crop;
}

void column_make_current(const WallpaperColumnGl &gl) {
    if (!gl.make_current)
        return;
    auto t0 = std::chrono::steady_clock::now();
    gl.make_current();
    float ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 5.0f)
        klog("wallpaper: column eglMakeCurrent %.1fms", ms);
}

GLuint g_spread_prog = 0;
bool g_spread_tried = false;

GLuint spread_program() {
    if (!g_spread_tried) {
        g_spread_tried = true;
        g_spread_prog = gl_compile_program_files("renderer/quad.vert", "wallpaper/spread.frag",
                                                 "wallpaper_spread");
    }
    return g_spread_prog;
}

float rand_range(float lo, float hi) {
    static std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

Texture map_texture(int width, int height, const uint8_t *rgba) {
    Texture tex = make_texture_rgba(width, height, rgba);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void transition_begin(WallpaperColumn &col) {
    const astralia::SpreadMap &map = col.pending_spread;
    if (!map.rgba.empty() && spread_program()) {
        col.tr_map = map_texture(map.width, map.height, map.rgba.data());
        col.tr_seed = rand_range(0.0f, 100.0f);
        col.transition_kind = WallpaperTransition::Spread;
        col.transition_start = std::chrono::steady_clock::now();
        col.transitioning = true;
    } else {
        col.tex_prev.reset();
    }
    col.pending_spread = {};
}

void transition_uv(FillMode mode, float col_w, float col_h, int tw, int th, float out[4]) {
    float scale =
        mode == FillMode::Fit ? std::min(col_w / tw, col_h / th) : std::max(col_w / tw, col_h / th);
    float draw_w = tw * scale;
    float draw_h = th * scale;
    out[0] = col_w / draw_w;
    out[1] = col_h / draw_h;
    out[2] = -((col_w - draw_w) / 2.0f) / draw_w;
    out[3] = -((col_h - draw_h) / 2.0f) / draw_h;
}

void transition_render(Renderer &r, WallpaperColumn &col, float x, float y, float w, float h,
                       float progress) {
    GLuint prog = spread_program();
    if (!prog || !col.tex.id || !col.tex_prev.id || !col.tr_map.id)
        return;
    float from_uv[4];
    float to_uv[4];
    transition_uv(col.mode, w, h, col.tex_prev.width, col.tex_prev.height, from_uv);
    transition_uv(col.mode, w, h, col.tex.width, col.tex.height, to_uv);
    const astralia::Color &fill = astralia::palette::base;
    r.draw_custom(prog, x, y, w, h, [&](GLuint p) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, col.tex_prev.id);
        glUniform1i(glGetUniformLocation(p, "u_from"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, col.tex.id);
        glUniform1i(glGetUniformLocation(p, "u_to"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, col.tr_map.id);
        glUniform1i(glGetUniformLocation(p, "u_map"), 2);
        glUniform4fv(glGetUniformLocation(p, "u_from_uv"), 1, from_uv);
        glUniform4fv(glGetUniformLocation(p, "u_to_uv"), 1, to_uv);
        glUniform4f(glGetUniformLocation(p, "u_fill"), fill.r, fill.g, fill.b, fill.a);
        glUniform1f(glGetUniformLocation(p, "u_progress"), progress);
        glUniform2f(glGetUniformLocation(p, "u_map_size"), static_cast<float>(col.tr_map.width),
                    static_cast<float>(col.tr_map.height));
        glUniform4fv(glGetUniformLocation(p, "u_ember"), 1, astralia::rgba(col.tr_ember));
        glUniform1f(glGetUniformLocation(p, "u_seed"), col.tr_seed);
        glActiveTexture(GL_TEXTURE0);
    });
}

} // namespace

void wallpaper_column_draw(const WallpaperColumn &col, GlCanvas &canvas, float x, float column_w,
                           float height) {
    auto video = col.video_texs.find(col.video_surface);
    const VideoTexture *video_tex = video != col.video_texs.end() ? &video->second : nullptr;
    bool zero_copy = col.zero_copy && video_tex;
    const Texture *tex = col.tex.id ? &col.tex : nullptr;
    if (!zero_copy && !tex)
        return;
    int tex_w = zero_copy ? video_tex->width : tex->width;
    int tex_h = zero_copy ? video_tex->height : tex->height;

    float scale = col.mode == FillMode::Fit ? std::min(column_w / tex_w, height / tex_h)
                                            : std::max(column_w / tex_w, height / tex_h);
    float draw_w = tex_w * scale;
    float draw_h = tex_h * scale;

    static constexpr float white[4] = {1, 1, 1, 1};
    astralia::ui::Box box{(column_w - draw_w) / 2.0f, (height - draw_h) / 2.0f, draw_w, draw_h};
    canvas.begin_group({x, 0.0f, column_w, height}, {.clip = true});
    if (zero_copy)
        canvas.video(*video_tex, box);
    else
        canvas.texture(*tex, box, white);
    canvas.end_group();
}

bool wallpaper_column_draw_transition(Renderer &renderer, WallpaperColumn &col, float x, float y,
                                      float column_w, float height) {
    if (!col.transitioning)
        return false;
    float raw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                         col.transition_start)
                    .count() /
                wallpaper_spread_duration_ms;
    if (raw >= 1.0f || col.transition_kind == WallpaperTransition::None) {
        col.tex_prev.reset();
        col.tr_map.reset();
        col.transitioning = false;
        return false;
    }
    transition_render(renderer, col, x, y, column_w, height, raw);
    return true;
}

void wallpaper_column_upload_pending(WallpaperColumn &col, const WallpaperColumnGl &gl) {
    if (!col.pending_pixels || !gl.make_current)
        return;
    auto t0 = std::chrono::steady_clock::now();
    column_make_current(gl);
    bool animated = col.decode.stop_flag != nullptr;
    if (col.pending_transition != WallpaperTransition::None && col.tex.id) {
        Texture fresh = make_texture_rgba(col.pending_width, col.pending_height, col.pending_pixels,
                                          true, col.pending_stride);
        if (fresh.id) {
            if (astralia::animation_instant()) {
                col.tex = std::move(fresh);
            } else {
                col.tex_prev = std::move(col.tex);
                col.tex = std::move(fresh);
                transition_begin(col);
            }
        }
        col.pending_transition = WallpaperTransition::None;
        delete[] col.pending_pixels;
        col.pending_pixels = nullptr;
        if (gl.request_frame)
            gl.request_frame();
        return;
    }
    update_texture_rgba(col.tex, col.pending_width, col.pending_height, col.pending_pixels,
                        !animated, col.pending_stride);
    delete[] col.pending_pixels;
    col.pending_pixels = nullptr;
    if (gl.request_frame)
        gl.request_frame();
    float ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 5.0f)
        klog("wallpaper: cpu upload %.1fms", ms);
}

void wallpaper_column_clear(WallpaperColumn &col, const WallpaperColumnGl &gl) {
    if (col.decode.stop_flag)
        col.decode.stop_flag->store(true);
    media_decode_stop(col.decode);
    col.path.clear();
    if (col.pinned_frame) {
        media_decode_release_drm_frame(col.pinned_frame);
        col.pinned_frame = nullptr;
    }
    if (col.pinned_frame_prev) {
        media_decode_release_drm_frame(col.pinned_frame_prev);
        col.pinned_frame_prev = nullptr;
    }
    if ((!col.video_texs.empty() || col.tex.id || col.tex_prev.id) && gl.make_current)
        column_make_current(gl);
    col.video_texs.clear();
    col.video_surface = 0;
    col.tex.reset();
    col.tex_prev.reset();
    col.tr_map.reset();
    col.pending_spread = {};
    col.transitioning = false;
    col.pending_transition = WallpaperTransition::None;
    col.zero_copy = false;
    delete[] col.pending_pixels;
    col.pending_pixels = nullptr;
    ++col.generation;
    if (gl.request_frame)
        gl.request_frame();
}

void wallpaper_column_set_static(WallpaperColumn &col, const WallpaperColumnGl &gl,
                                 const std::string &path, int target_w, int target_h,
                                 FillMode mode) {
    if (col.path == path && col.mode == mode && col.target_w == target_w &&
        col.target_h == target_h && col.tex.id)
        return;
    col.pending_transition = col.tex.id ? WallpaperTransition::Spread : WallpaperTransition::None;
    col.path = path;
    col.mode = mode;
    col.target_w = target_w;
    col.target_h = target_h;
    uint64_t gen = ++col.generation;
    std::weak_ptr<int> life = col.life;
    bool spread = col.pending_transition == WallpaperTransition::Spread;
    auto seed = static_cast<uint32_t>(rand_range(0.0f, 1.0e9f));
    std::thread([&col, &gl, path, gen, target_w, target_h, life, spread, seed] {
        int w = 0, h = 0;
        unsigned char *data = animate_decode_scaled(path, target_w, target_h, w, h);
        astralia::SpreadMap map;
        astralia::Color ember{1.0f, 1.0f, 1.0f, 1.0f};
        if (spread && data) {
            map = astralia::make_spread_map(seed);
            ember = astralia::ember_colour(astralia::extract_seeds(data, w, h, false));
        }
        astralia::DeferredCall::call_later(
            [&col, &gl, data, w, h, gen, life, map = std::move(map), ember]() mutable {
                auto keep = life.lock();
                if (!keep) {
                    delete[] data;
                    return;
                }
                if (gen != col.generation) {
                    delete[] data;
                    return;
                }
                if (!data)
                    return;
                delete[] col.pending_pixels;
                col.pending_pixels = data;
                col.pending_width = w;
                col.pending_height = h;
                col.pending_stride = 0;
                col.pending_spread = std::move(map);
                col.tr_ember = ember;
                wallpaper_column_upload_pending(col, gl);
            });
    }).detach();
}

void wallpaper_column_set_animated(WallpaperColumn &col, const WallpaperColumnGl &gl,
                                   const std::string &path, int target_w, int target_h,
                                   FillMode mode) {
    wallpaper_column_clear(col, gl);
    col.path = path;
    col.mode = mode;
    col.target_w = target_w;
    col.target_h = target_h;
    if (target_w <= 0 || target_h <= 0)
        return;
    uint64_t gen = ++col.generation;
    std::weak_ptr<int> life = col.life;

    klog("wallpaper: animated column start '%s' zero_copy_supported=%d "
         "surface=%d",
         path.c_str(), video_texture_import_supported() ? 1 : 0, gl.make_current ? 1 : 0);

    std::string filter = animate_scale_filter(target_w, target_h, to_fit(mode));

    col.decode = media_decode_stream(
        path, filter, animate_wallpaper_fps, texture_row_length_supported(),
        [&col, &gl, gen, life](unsigned char *rgba, int w, int h, int stride_px) {
            astralia::DeferredCall::call_later([&col, &gl, rgba, w, h, stride_px, gen, life] {
                auto keep = life.lock();
                if (!keep) {
                    delete[] rgba;
                    return;
                }
                if (gen != col.generation) {
                    delete[] rgba;
                    return;
                }
                delete[] col.pending_pixels;
                col.pending_pixels = rgba;
                col.pending_width = w;
                col.pending_height = h;
                col.pending_stride = stride_px;
                wallpaper_column_upload_pending(col, gl);
            });
        },
        video_texture_import_supported()
            ? MediaDrmCallback([&col, &gl, gen, life](MediaDrmFrame frame) {
                  astralia::DeferredCall::call_later([&col, &gl, frame, gen, life] {
                      auto keep = life.lock();
                      if (!keep) {
                          media_decode_release_drm_frame(frame.avframe_handle);
                          return;
                      }
                      if (gen != col.generation || !gl.make_current) {
                          media_decode_release_drm_frame(frame.avframe_handle);
                          return;
                      }
                      DrmFrameImport import;
                      import.plane_count = frame.plane_count;
                      import.width = frame.width;
                      import.height = frame.height;
                      for (int i = 0; i < frame.plane_count; ++i)
                          import.planes[i] = {frame.planes[i].fd, frame.planes[i].modifier,
                                              frame.planes[i].offset, frame.planes[i].pitch};
                      auto t0 = std::chrono::steady_clock::now();
                      column_make_current(gl);
                      auto [slot, miss] = col.video_texs.try_emplace(frame.surface_id);
                      bool ok = !miss || video_texture_import(slot->second, gl.display, import);
                      if (!ok)
                          col.video_texs.erase(slot);
                      float ms = std::chrono::duration<float, std::milli>(
                                     std::chrono::steady_clock::now() - t0)
                                     .count();
                      if (ms > 5.0f)
                          klog("wallpaper: zero-copy import %.1fms", ms);
                      if (ok) {
                          col.video_surface = frame.surface_id;
                          col.zero_copy = true;
                          if (col.pinned_frame_prev)
                              media_decode_release_drm_frame(col.pinned_frame_prev);
                          col.pinned_frame_prev = col.pinned_frame;
                          col.pinned_frame = frame.avframe_handle;
                          if (gl.request_frame)
                              gl.request_frame();
                      } else {
                          klog("wallpaper: video_texture_import failed "
                               "(%dx%d planes=%d), falling back to CPU upload",
                               frame.width, frame.height, frame.plane_count);
                          if (col.decode.egl_import_failed)
                              col.decode.egl_import_failed->store(true);
                          media_decode_release_drm_frame(frame.avframe_handle);
                      }
                  });
              })
            : MediaDrmCallback());
}

MediaDecodeStatus wallpaper_column_decode_status(const WallpaperColumn &col) {
    return media_decode_status(col.decode);
}
