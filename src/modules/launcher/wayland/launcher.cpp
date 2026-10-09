#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "core/deferred_call.h"
#include "wayland/core/log.h"

#include "modules/launcher/view.h"
#include "modules/launcher/wayland/launcher.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

namespace {

void launcher_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
    auto *state = static_cast<LauncherState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->width = static_cast<int32_t>(width);
    state->height = static_cast<int32_t>(height);
    int32_t scale = state->output_scale.scale;
    if (state->egl_window)
        wl_egl_window_resize(state->egl_window, state->width * scale, state->height * scale, 0, 0);
    state->configured = true;
}

void launcher_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener launcher_layer_surface_listener = {
    .configure = launcher_layer_surface_configure,
    .closed = launcher_layer_surface_closed,
};

void launcher_update_input_region(LauncherState &state) {
    if (state.open) {
        wl_surface_set_input_region(state.surface, nullptr);
        return;
    }
    wl_region *empty_region = wl_compositor_create_region(state.compositor);
    wl_surface_set_input_region(state.surface, empty_region);
    wl_region_destroy(empty_region);
}

} // namespace

bool launcher_create_surface(LauncherState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    state.compositor = compositor;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "astralia-shell-launcher",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    state.layer_surface =
        layer_surface_create(state.surface, compositor, layer_shell, cfg, &launcher_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale, state.height * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);

    return true;
}

bool launcher_init_egl(LauncherState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    state.egl_display = display;
    state.egl_context = context;
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(state.surface, state.width * scale, state.height * scale);
    state.egl_surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { launcher_paint(state); };
    return true;
}

void launcher_request_frame(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE || !state.open)
        return;
    request_frame(state.frame_clock);
}

void launcher_destroy_surface(LauncherState &state) {
    if (state.frame_clock.callback) {
        wl_callback_destroy(state.frame_clock.callback);
        state.frame_clock.callback = nullptr;
    }
    state.frame_clock.surface = nullptr;
    state.frame_clock.redraw_requested = false;
    state.frame_clock.mapped = false;
    if (state.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(state.egl_display, state.egl_surface);
        state.egl_surface = EGL_NO_SURFACE;
    }
    if (state.egl_window) {
        wl_egl_window_destroy(state.egl_window);
        state.egl_window = nullptr;
    }
    if (state.layer_surface) {
        zwlr_layer_surface_v1_destroy(state.layer_surface);
        state.layer_surface = nullptr;
    }
    if (state.surface) {
        wl_surface_destroy(state.surface);
        state.surface = nullptr;
    }
    state.configured = false;
}

void launcher_retarget(LauncherState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name) {
    wl_output *previous_output = state.bound_output;
    klog("panel: launcher retargeting from output=%p to '%s'", static_cast<void *>(previous_output), target_name);

    launcher_destroy_surface(state);
    state.open = false;
    state.opacity = 0.0f;

    auto bind_to = [&](wl_output *out) -> bool {
        if (!launcher_create_surface(state, compositor, layer_shell, out))
            return false;
        while (!state.configured)
            wl_display_dispatch(display);
        return launcher_init_egl(state, renderer, egl_display, egl_config, egl_context);
    };

    if (bind_to(target_output)) {
        state.bound_output = target_output;
        return;
    }
    if (previous_output && bind_to(previous_output)) {
        state.bound_output = previous_output;
        return;
    }
    klog("panel: launcher retarget fallback also failed");
}

void launcher_toggle(LauncherState &state, bool global) {
    if (!state.layer_surface || state.egl_surface == EGL_NO_SURFACE || !state.model)
        return;

    if (state.open) {
        klog("launcher: CLOSE");
        state.model->field().preedit.clear();
        if (state.sync_text_input_focus)
            state.sync_text_input_focus(false);

        launcher_request_frame(state);
        state.animations.animate(state.opacity, 0.0f, kOverlayFadeMs, astralia::Easing::EaseOutCubic, [&state](float v) { state.opacity = v; }, [&state] {
                state.open = false;
                state.model->close();
                zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
                launcher_update_input_region(state);
                wl_surface_commit(state.surface);
                astralia::DeferredCall::call_later([&state] {
                    if (!state.open)
                        launcher_destroy_surface(state);
                }); }, kOverlayFadeOwner);
        return;
    }

    klog("launcher: OPEN (global=%d)", global);
    state.model->open(global);
    state.open = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);
    state.animations.animate(state.opacity, 1.0f, kOverlayFadeMs, astralia::Easing::EaseOutCubic, [&state](float v) { state.opacity = v; }, {}, kOverlayFadeOwner);
    launcher_request_frame(state);
    if (state.sync_text_input_focus)
        state.sync_text_input_focus(true);
}

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event) {
    if (state.model)
        state.model->key(to_neutral(event));
}

namespace {

int launcher_row_at(const LauncherState &state, double px, double py) {
    for (const auto &[box, index] : state.frame.rows) {
        if (px >= box.x && px < box.x + box.w && py >= box.y && py < box.y + box.h)
            return index;
    }
    return -1;
}

} // namespace

void launcher_handle_click(LauncherState &state, double px, double py) {
    if (!state.model)
        return;
    int row = launcher_row_at(state, px, py);
    if (row >= 0) {
        state.model->click_row(row);
        return;
    }
    const astralia::ui::Box &r = state.frame.box;
    bool inside = px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    if (!inside)
        launcher_toggle(state, false);
}

void launcher_handle_pointer_move(LauncherState &state, wl_surface *focused_surface, double px, double py) {
    if (!state.model)
        return;
    int row = !state.open || focused_surface != state.surface ? -1 : launcher_row_at(state, px, py);
    if (state.model->hover(row))
        launcher_request_frame(state);
}

void launcher_paint(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    auto now = std::chrono::steady_clock::now();
    state.animations.tick(now);
    if (state.model) {
        state.model->tick(now);
        if (state.open)
            state.model->sync_layout();
    }

    gl_make_current(state.egl_display, state.egl_surface, state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(state.width, state.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(scale);
    state.canvas.set_opacity(state.opacity);
    state.frame = {};
    if (state.open && state.model)
        state.frame = astralia::paint_launcher(state.canvas, *state.model, static_cast<float>(state.width), static_cast<float>(state.height));
    state.canvas.flush();
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive() || (state.model && state.model->animating()))
        launcher_request_frame(state);
}

TextInputState launcher_text_input_state(const LauncherState &state) {
    TextInputState s;
    s.purpose = TextInputPurpose::Normal;
    s.cursor_rect_x = static_cast<int32_t>(state.frame.caret.x);
    s.cursor_rect_y = static_cast<int32_t>(state.frame.caret.y);
    s.cursor_rect_w = static_cast<int32_t>(state.frame.caret.w);
    s.cursor_rect_h = static_cast<int32_t>(state.frame.caret.h);
    return s;
}

void launcher_text_input_apply_edit(LauncherState &state, const TextInputEdit &edit) {
    if (!state.model)
        return;
    if (edit.has_delete)
        state.model->delete_before(edit.delete_before_length);
    if (edit.has_commit_text)
        state.model->commit_text(edit.commit_text);
    if (edit.has_preedit)
        state.model->set_preedit(edit.preedit_text);
}

namespace {

class LauncherModule final : public Module, public TextInputClient {
  public:
    const char *name() const override { return "launcher"; }
    bool is_open() const override { return state_.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = launcher_create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!launcher_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        if (!state_.model) {
            state_.model = std::make_unique<astralia::LauncherModel>(*app.reactor);
            state_.model->on_changed = [this, &app] {
                request_frame();
                app_detail::rest_egl_current(app);
            };
            state_.model->on_close_requested = [this] { launcher_toggle(state_, false); };
        }
        state_.sync_text_input_focus = [this, &app](bool focused) {
            if (focused)
                app.text_input.set_focused_client(state_.surface, this);
            else
                app.text_input.clear_focused_client(this);
        };
        request_frame();
        return true;
    }

    TextInputState text_input_state() const override {
        return launcher_text_input_state(state_);
    }
    void text_input_apply_edit(const TextInputEdit &edit) override {
        launcher_text_input_apply_edit(state_, edit);
        request_frame();
    }
    void text_input_reset_preedit() override {
        state_.model->field().preedit.clear();
        request_frame();
    }
    void text_input_activated(TextInputService &) override {}
    void text_input_deactivated(TextInputService &) override {
        if (state_.model)
            state_.model->field().preedit.clear();
    }

    bool configured() const override { return !want_ || state_.configured; }
    wl_surface *surface() const override { return state_.surface; }
    void request_frame() override { launcher_request_frame(state_); }

    bool timer_tick(WaylandState &) override {
        if (!state_.open)
            return false;
        state_.model->toggle_caret();
        request_frame();
        return true;
    }

    void handle_click(WaylandState &, double x, double y) override {
        launcher_handle_click(state_, x, y);
    }
    void handle_pointer_move(WaylandState &, wl_surface *focused_surface, double x, double y) override {
        launcher_handle_pointer_move(state_, focused_surface, x, y);
    }
    bool wants_pointing_hand_cursor() const override {
        return state_.open && state_.model && state_.model->hovered() >= 0;
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        launcher_handle_key_event(state_, event);
    }

    void on_output_removed(WaylandState &, wl_output *out) override {
        if (!out || state_.bound_output != out)
            return;
        if (state_.sync_text_input_focus)
            state_.sync_text_input_focus(false);
        launcher_destroy_surface(state_);
        state_.open = false;
        state_.opacity = 0.0f;
        state_.animations = {};
        state_.bound_output = nullptr;
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        auto toggle_retargeted = [this, &app](bool global) {
            if (!state_.open) {
                MonitorOutput *target = app_detail::active_target_monitor(app);
                if (target && (target->output.wl != state_.bound_output || !state_.layer_surface))
                    launcher_retarget(state_, app.compositor, app.layer_shell, app.display, app.renderer, app.egl_display, app.egl_config, app.egl_context, target->output.wl, target->output.name.c_str());
            }
            launcher_toggle(state_, global);
        };
        return {
            {astralia::ShellVerb::launcher, [toggle_retargeted] { toggle_retargeted(false); }},
            {astralia::ShellVerb::launcher_global, [toggle_retargeted] { toggle_retargeted(true); }},
        };
    }

  private:
    LauncherState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

} // namespace

std::unique_ptr<Module> make_launcher_module() {
    return std::make_unique<LauncherModule>();
}
