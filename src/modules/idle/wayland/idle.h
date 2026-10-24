#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/per_monitor_module.h"
#include "wayland/config/idle_overlay_config.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/renderer.h"
#include "wayland/render/texture.h"

#include "config/idle_config.h"

#include "modules/idle/bounce.h"

#include "core/animation.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct MonitorOutput;

namespace astralia::idle {

struct OverlayState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;

    astralia::AnimationManager animations;
    float ambient_opacity = 0.0f;
    float screensaver_opacity = 0.0f;
    bool ambient_active = false;
    bool screensaver_active = false;
    std::string logo_path;

    Bounce bounce{40.0f, 40.0f, astralia::idle_config::logo_speed,
                  astralia::idle_config::logo_speed * 0.75f};
    std::chrono::steady_clock::time_point last_tick = std::chrono::steady_clock::now();

    std::function<void(GlCanvas &canvas, float w, float h)> draw_ambient;
};

bool overlay_create_surface(OverlayState &state, wl_compositor *compositor,
                            zwlr_layer_shell_v1 *layer_shell, wl_output *output);

bool overlay_init_egl(OverlayState &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
                      EGLContext context);

void overlay_request_frame(OverlayState &state);

void overlay_set_active(OverlayState &state, bool ambient_active, bool screensaver_active);

struct WallpaperHooks {
    std::function<void(MonitorOutput &mon, GlCanvas &canvas, int32_t w, int32_t h)> draw;
    std::function<void(MonitorOutput &mon, bool paused)> set_paused;
};

std::unique_ptr<PerMonitorModule> make_per_monitor_module(WallpaperHooks hooks);

} // namespace astralia::idle
