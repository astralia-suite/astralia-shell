#pragma once

#include <EGL/egl.h>
#include <memory>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/per_monitor_module.h"

#include "modules/osd/model.h"

#include "modules/osd/wayland/osd_style_config.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/renderer.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/output_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct OsdState {
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
    astralia::OsdModel model;
};

bool osd_create_surface(OsdState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool osd_init_egl(OsdState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context);

void osd_request_frame(OsdState &state);

void osd_show(OsdState &state, astralia::OsdKind kind, int percent, bool muted);

void osd_hide(OsdState &state);

struct WaylandState;

std::unique_ptr<PerMonitorModule> make_osd_per_monitor_module();

void osd_show_on_monitors(WaylandState &app, astralia::OsdKind kind, int percent, bool muted);
