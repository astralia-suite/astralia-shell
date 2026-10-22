#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "config/overview_config.h"

#include "modules/overview/model.h"

#include "service/wayland/capture_service.h"
#include "service/wayland/input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

namespace astralia::overview {

struct State {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    ToplevelExportState capture;
    std::unique_ptr<Model> model;
    bool opened_by_widget = false;
    wl_output *bound_output = nullptr;
    WaylandState *app_ptr = nullptr;
};

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context);

void retarget(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
              wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config,
              EGLContext egl_context, wl_output *target_output, const char *target_name);

void request_frame(State &state);

void toggle(State &state, WaylandState &app, bool by_widget = false);

std::vector<astralia::ShellBinding> shell_bindings(State &overview, WaylandState &state);

void handle_click(State &state, WaylandState &app, double px, double py);

void handle_pointer_move(State &state, WaylandState &app, double px, double py);

bool point_is_clickable(State &state, WaylandState &app, double px, double py);

void handle_pointer_release(State &state, WaylandState &app);

void handle_key_event(State &state, WaylandState &app, const KeyEvent &event);

void render(State &state, WaylandState &app);

std::unique_ptr<Module> make_module();

} // namespace astralia::overview
