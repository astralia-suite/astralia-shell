#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "modules/polkit/wayland/polkit.h"

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/polkit/view.h"

#include "modules/polkit/wayland/polkit_style_config.h"

#include "wayland/render/gl.h"

namespace {

void release_keyboard(PolkitState &state) {
    state.base.open = false;
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
}

void open_card(PolkitState &state) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.open = true;
    state.base.opacity = 1.0f;
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
    state.model.sync(true);
    overlay_panel_request_frame(state.base);
}

void close_card(PolkitState &state) {
    if (!state.base.open)
        return;
    state.model.sync(false);
    overlay_panel_request_frame(state.base);
}

} // namespace

bool polkit_create_surface(PolkitState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    state.model.reset();
    return overlay_panel_create_surface(state.base, compositor, layer_shell, kPolkitNamespace, output);
}

bool polkit_init_egl(PolkitState &state, Renderer &renderer, WaylandState &app, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &app] { polkit_paint(state, app); };
    state.model.on_closed = [&state] { release_keyboard(state); };
    return true;
}

void polkit_retarget(PolkitState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, WaylandState &app, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(state.base, display, state.bound_output, target_output, target_name, [&](wl_output *out) { return polkit_create_surface(state, compositor, layer_shell, out); }, [&] { return polkit_init_egl(state, renderer, app, egl_display, egl_config, egl_context); });
    if (bound)
        state.bound_output = bound;
}

void polkit_request_frame(PolkitState &state) {
    overlay_panel_request_frame(state.base);
}

void polkit_sync_open_state(PolkitState &state, WaylandState &app) {
    bool pending = app.polkit->pending();
    if (pending && !state.base.open) {
        MonitorOutput *target = app_detail::active_target_monitor(app);
        if (target && (target->output.wl != state.bound_output || !state.base.layer_surface))
            polkit_retarget(state, app.compositor, app.layer_shell, app.display, app.renderer, app, app.egl_display, app.egl_config, app.egl_context, target->output.wl, target->output.name.c_str());
        open_card(state);
        return;
    }
    if (pending && state.model.closing()) {
        state.model.sync(true);
    } else if (!pending && state.base.open) {
        close_card(state);
        return;
    }
    polkit_request_frame(state);
}

void polkit_handle_key_event(PolkitState &state, WaylandState &app, const KeyEvent &event) {
    switch (state.model.key(to_neutral(event))) {
    case astralia::PolkitKey::changed:
        polkit_request_frame(state);
        break;
    case astralia::PolkitKey::submit: {
        std::string password = state.model.take_password();
        app.polkit->respond(password);
        explicit_bzero(password.data(), password.size());
        polkit_request_frame(state);
        break;
    }
    case astralia::PolkitKey::cancel:
        app.polkit->cancel();
        break;
    case astralia::PolkitKey::none:
        break;
    }
}

void polkit_paint(PolkitState &state, WaylandState &app) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.model.tick(std::chrono::steady_clock::now());
    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(scale);
    if (state.model.open()) {
        state.model.set_prompt(astralia::polkit_prompt(*app.polkit));
        astralia::paint_polkit(state.canvas, state.model, static_cast<float>(state.base.width), static_cast<float>(state.base.height));
    }
    state.canvas.flush();
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.model.animating())
        overlay_panel_request_frame(state.base);
}

namespace {

class PolkitModule final : public Module {
  public:
    const char *name() const override { return "polkit"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = polkit_create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!polkit_init_egl(state_, app.renderer, app, app.egl_display, app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { polkit_request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        polkit_handle_key_event(state_, app, event);
    }

    wl_output *bound_output() const override { return state_.bound_output; }
    void on_output_removed(WaylandState &, wl_output *out) override {
        if (state_.bound_output != out)
            return;
        overlay_panel_release_output(state_.base, state_.bound_output, out);
    }

    void sync_state(WaylandState &app) { polkit_sync_open_state(state_, app); }

  private:
    PolkitState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

} // namespace

std::unique_ptr<Module> make_polkit_module() {
    return std::make_unique<PolkitModule>();
}

void polkit_notify_state_changed(WaylandState &app) {
    for (auto &m : app.overlays)
        if (auto *pm = dynamic_cast<PolkitModule *>(m.get()))
            pm->sync_state(app);
}
