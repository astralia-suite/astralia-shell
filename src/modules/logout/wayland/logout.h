#pragma once

#include <GLES3/gl32.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"
#include "wayland/render/thunder.h"

#include "modules/logout/model.h"
#include "modules/logout/wayland/logout_style_config.h"

#include "render/geometry.h"

#include "service/wayland/input_service.h"

struct WaylandState;

namespace astralia::logout {

struct State {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    Model model{logout_commands(true)};
    bool opened_by_widget = false;
    std::string logo_path;
    wl_output *bound_output = nullptr;

    ThunderBurst thunder;
};

namespace detail {
astralia::ui::Box button_rect(int index, float center_x, float center_y);
} // namespace detail

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context);

void retarget(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
              wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config,
              EGLContext egl_context, wl_output *target_output, const char *target_name);

void request_frame(State &state);

void toggle(State &state, bool by_widget = false);

std::vector<astralia::ShellBinding> shell_bindings(State &logout, WaylandState &state);

void handle_key_event(State &state, const KeyEvent &event);

void handle_click(State &state, double px, double py);

void handle_hover(State &state, double px, double py);

void clear_hover(State &state);

void render(State &state);

std::unique_ptr<Module> make_module();

} // namespace astralia::logout
