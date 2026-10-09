#pragma once

#include <EGL/egl.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/app/config.h"
#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "wayland/render/animated_image.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"

#include "service/wayland/input_service.h"
#include "service/wayland/media_service.h"
#include "service/wayland/text_input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

class Renderer;
struct WaylandState;

using SettingsDecodeStatusFn = std::function<MediaDecodeStatus(WaylandState &app, const std::string &monitor, int column)>;

struct SettingsState {
    OverlayPanelBase base;
    bool enabled = false;
    wl_output *bound_output = nullptr;
    SettingsDecodeStatusFn decode_status_source;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    std::unique_ptr<astralia::SettingsModel> model;
    astralia::SettingsFrame frame;
    AnimatedImage profile_pic;
    std::function<void(bool)> sync_text_input_focus;
};

bool settings_create_surface(SettingsState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output = nullptr);

bool settings_init_egl(SettingsState &state, WaylandState &app);

void settings_request_frame(SettingsState &state);

void settings_toggle(SettingsState &state, WaylandState &app);

std::vector<astralia::ShellBinding> settings_shell_bindings(SettingsState &settings, WaylandState &state);

void settings_paint(SettingsState &state);

TextInputState settings_text_input_state(const SettingsState &state);

void settings_text_input_apply_edit(SettingsState &state, const TextInputEdit &edit);

std::unique_ptr<Module> make_settings_module(SettingsDecodeStatusFn decode_status_source);
