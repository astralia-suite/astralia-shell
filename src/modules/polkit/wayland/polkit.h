#pragma once

#include <EGL/egl.h>
#include <memory>
#include <wayland-client.h>

#include "wayland/app/module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "modules/polkit/model.h"

#include "service/wayland/input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

namespace astralia::polkit {

struct State {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    Model model;
    wl_output *bound_output = nullptr;
};

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, Renderer &renderer, WaylandState &app, EGLDisplay display,
              EGLConfig config, EGLContext context);

void retarget(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
              wl_display *display, Renderer &renderer, WaylandState &app, EGLDisplay egl_display,
              EGLConfig egl_config, EGLContext egl_context, wl_output *target_output,
              const char *target_name);

void request_frame(State &state);

void sync_open_state(State &state, WaylandState &app);

void handle_key_event(State &state, WaylandState &app, const KeyEvent &event);

void render(State &state, WaylandState &app);

std::unique_ptr<Module> make_module();

void notify_state_changed(WaylandState &app);

} // namespace astralia::polkit
