#pragma once

#include <EGL/egl.h>
#include <memory>
#include <wayland-client.h>

#include "wayland/app/module.h"

#include "modules/polkit/model.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "service/wayland/input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

struct PolkitState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    astralia::PolkitModel model;
    wl_output *bound_output = nullptr;
};

bool polkit_create_surface(PolkitState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool polkit_init_egl(PolkitState &state, Renderer &renderer, WaylandState &app, EGLDisplay display, EGLConfig config, EGLContext context);

void polkit_retarget(PolkitState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, WaylandState &app, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name);

void polkit_request_frame(PolkitState &state);

void polkit_sync_open_state(PolkitState &state, WaylandState &app);

void polkit_handle_key_event(PolkitState &state, WaylandState &app, const KeyEvent &event);

void polkit_paint(PolkitState &state, WaylandState &app);

std::unique_ptr<Module> make_polkit_module();

void polkit_notify_state_changed(WaylandState &app);
