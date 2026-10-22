#pragma once

#include <EGL/egl.h>
#include <functional>
#include <memory>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"
#include "wayland/render/thunder.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

#include "core/animation.h"

#include "service/wayland/frame_service.h"
#include "service/wayland/input_service.h"
#include "service/wayland/output_service.h"
#include "service/wayland/text_input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace astralia::launcher {

struct State {
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
    bool lightning = false;
    float entrance_t = 1.0f;
    float entrance_seed = 0.0f;
    bool entrance_active = false;
    ThunderBurst thunder;
    std::unique_ptr<Model> model;
    Frame frame;
    std::function<void(bool)> sync_text_input_focus;
    wl_compositor *compositor = nullptr;
    int32_t width = 0, height = 0;
    wl_output *bound_output = nullptr;
};

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context);

void destroy_surface(State &state);

void retarget(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
              wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config,
              EGLContext egl_context, wl_output *target_output, const char *target_name);

void request_frame(State &state);

void toggle(State &state, bool global);

void handle_key_event(State &state, const KeyEvent &event);

void handle_click(State &state, double px, double py);

void handle_pointer_move(State &state, wl_surface *focused_surface, double px, double py);

void handle_pointer_release(State &state);

void render(State &state);

TextInputState text_input_state(const State &state);

void text_input_apply_edit(State &state, const TextInputEdit &edit);

std::unique_ptr<Module> make_module();

} // namespace astralia::launcher
