#pragma once

#include <EGL/egl.h>
#include <memory>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/per_monitor_module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/renderer.h"

#include "modules/osd/model.h"
#include "modules/osd/wayland/osd_style_config.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

namespace astralia::osd {

struct State {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    Model model;
};

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context);

void request_frame(State &state);

void show(State &state, OsdKind kind, int percent, bool muted);

void hide(State &state);

std::unique_ptr<PerMonitorModule> make_per_monitor_module();

void show_on_monitors(WaylandState &app, OsdKind kind, int percent, bool muted);

} // namespace astralia::osd
