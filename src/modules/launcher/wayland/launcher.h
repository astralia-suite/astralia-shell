#pragma once

#include <EGL/egl.h>
#include <functional>
#include <memory>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/module.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

#include "core/animation.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/input_service.h"
#include "service/wayland/output_service.h"
#include "service/wayland/text_input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct LauncherState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    OutputScale output_scale;
    FrameClock frame_clock;
    GlCanvas canvas;
    bool configured = false;
    bool open = false;
    astralia::AnimationManager animations;
    float opacity = 0.0f;
    std::unique_ptr<astralia::LauncherModel> model;
    astralia::LauncherFrame frame;
    std::function<void(bool)> sync_text_input_focus;
    wl_compositor *compositor = nullptr;
    int32_t width = 0, height = 0;
    wl_output *bound_output = nullptr;
};

bool launcher_create_surface(LauncherState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool launcher_init_egl(LauncherState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context);

void launcher_destroy_surface(LauncherState &state);

void launcher_retarget(LauncherState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name);

void launcher_request_frame(LauncherState &state);

void launcher_toggle(LauncherState &state, bool global);

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event);

void launcher_handle_click(LauncherState &state, double px, double py);

void launcher_handle_pointer_move(LauncherState &state, wl_surface *focused_surface, double px, double py);

void launcher_paint(LauncherState &state);

TextInputState launcher_text_input_state(const LauncherState &state);

void launcher_text_input_apply_edit(LauncherState &state, const TextInputEdit &edit);

std::unique_ptr<Module> make_launcher_module();
