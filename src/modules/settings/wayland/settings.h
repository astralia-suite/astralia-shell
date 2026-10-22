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
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "service/wayland/input_service.h"
#include "service/wayland/media_service.h"
#include "service/wayland/text_input_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

class Renderer;
struct WaylandState;

namespace astralia::settings {

using DecodeStatusFn =
    std::function<MediaDecodeStatus(WaylandState &app, const std::string &monitor, int column)>;

struct State {
    OverlayPanelBase base;
    bool enabled = false;
    wl_output *bound_output = nullptr;
    DecodeStatusFn decode_status_source;
    Renderer *renderer = nullptr;
    GlCanvas canvas;
    std::unique_ptr<Model> model;
    Frame frame;
    std::function<void(bool)> sync_text_input_focus;
};

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output = nullptr);

bool init_egl(State &state, WaylandState &app);

void request_frame(State &state);

void toggle(State &state, WaylandState &app);

std::vector<astralia::ShellBinding> shell_bindings(State &settings, WaylandState &state);

void render(State &state);

TextInputState text_input_state(const State &state);

void text_input_apply_edit(State &state, const TextInputEdit &edit);

std::unique_ptr<Module> make_module(DecodeStatusFn decode_status_source);

} // namespace astralia::settings
