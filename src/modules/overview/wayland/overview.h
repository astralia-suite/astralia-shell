#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "config/overview_config.h"

#include "modules/overview/model.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "service/wayland/capture_service.h"
#include "service/wayland/input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

struct OverviewState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    ToplevelExportState capture;
    std::unique_ptr<astralia::OverviewModel> model;
    bool opened_by_widget = false;
    wl_output *bound_output = nullptr;
    WaylandState *app_ptr = nullptr;
};

bool overview_create_surface(OverviewState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool overview_init_egl(OverviewState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context);

void overview_retarget(OverviewState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name);

void overview_request_frame(OverviewState &state);

void overview_toggle(OverviewState &state, WaylandState &app, bool by_widget = false);

std::vector<astralia::ShellBinding> overview_shell_bindings(OverviewState &overview, WaylandState &state);

void overview_handle_click(OverviewState &state, WaylandState &app, double px, double py);

void overview_handle_pointer_move(OverviewState &state, WaylandState &app, double px, double py);

bool overview_point_is_clickable(OverviewState &state, WaylandState &app, double px, double py);

void overview_handle_pointer_release(OverviewState &state, WaylandState &app);

void overview_handle_key_event(OverviewState &state, WaylandState &app, const KeyEvent &event);

void overview_paint(OverviewState &state, WaylandState &app);

std::unique_ptr<Module> make_overview_module();
