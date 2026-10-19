#pragma once

#include <string>
#include <GLES3/gl32.h>
#include <chrono>
#include <memory>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "modules/logout/model.h"

#include "modules/logout/wayland/logout_style_config.h"

#include "render/geometry.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"
#include "wayland/render/thunder.h"

#include "service/wayland/input_service.h"

struct WaylandState;

struct LogoutState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    astralia::LogoutModel model{astralia::logout_commands(true)};
    bool opened_by_widget = false;
    std::string logo_path;
    wl_output *bound_output = nullptr;

    ThunderBurst thunder;
};

astralia::ui::Box logout_detail_button_rect(int index, float center_x, float center_y);

bool logout_create_surface(LogoutState &state, wl_compositor *compositor,
                           zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool logout_init_egl(LogoutState &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
                     EGLContext context);

void logout_retarget(LogoutState &state, wl_compositor *compositor,
                     zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer,
                     EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context,
                     wl_output *target_output, const char *target_name);

void logout_request_frame(LogoutState &state);

void logout_toggle(LogoutState &state, bool by_widget = false);

std::vector<astralia::ShellBinding> logout_shell_bindings(LogoutState &logout, WaylandState &state);

void logout_handle_key_event(LogoutState &state, const KeyEvent &event);

void logout_handle_click(LogoutState &state, double px, double py);

void logout_handle_hover(LogoutState &state, double px, double py);

void logout_clear_hover(LogoutState &state);

void logout_paint(LogoutState &state);

std::unique_ptr<Module> make_logout_module();
