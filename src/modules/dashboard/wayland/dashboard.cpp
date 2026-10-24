#include <GLES3/gl32.h>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/render/gl.h"
#include "wayland/render/overlay_panel.h"

#include "modules/dashboard/wayland/dashboard.h"

#include "render/tokens.h"

namespace astralia::dashboard {

void request_frame(State &state) { toplevel_window_request_frame(state.base); }

void toggle(State &state, WaylandState &app) {
    bool opening = !state.base.open;
    if (opening) {
        if (state.base.egl_surface == EGL_NO_SURFACE) {
            if (!toplevel_window_create_surface(
                    state.base, app.compositor, app.wm_base, astralia::dashboard_config::title,
                    astralia::dashboard_config::app_id, astralia::dashboard_config::default_width,
                    astralia::dashboard_config::default_height))
                return;
            while (!state.base.configured)
                wl_display_dispatch(app.display);
            if (!toplevel_window_init_egl(state.base, app.egl_display, app.egl_config,
                                          app.egl_context)) {
                toplevel_window_destroy_surface(state.base);
                return;
            }
            state.renderer = &app.renderer;
            state.base.frame_clock.draw = [&state] { render(state); };
            state.base.on_close_request = [&state, &app] { toggle(state, app); };
        }
        state.base.open = true;
        state.base.animations.animate(
            state.base.opacity, 1.0f, overlay_fade_ms, astralia::Easing::EaseOutCubic,
            [&state](float v) { state.base.opacity = v; }, {}, overlay_fade_owner);
        toplevel_window_request_frame(state.base);
    } else {
        state.base.animations.cancel_owner(overlay_fade_owner);
        state.base.open = false;
        toplevel_window_destroy_surface(state.base);
        app_detail::rest_egl_current(app);
    }
}

void handle_key_event(State &state, WaylandState &app, const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        toggle(state, app);
}

std::vector<astralia::ShellBinding> shell_bindings(State &dashboard, WaylandState &state) {
    return {
        {astralia::ShellVerb::dashboard, [&dashboard, &state] { toggle(dashboard, state); }},
    };
}

void render(State &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.animations.tick(std::chrono::steady_clock::now());

    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.renderer->set_opacity(state.base.opacity);
    state.renderer->draw_rect(0.0f, 0.0f, static_cast<float>(state.base.width),
                              static_cast<float>(state.base.height),
                              astralia::rgba(astralia::palette::window_backdrop));
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.has_active())
        toplevel_window_request_frame(state.base);
}

namespace {

class Adapter final : public Module {
  public:
    const char *name() const override { return "dashboard"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }
    bool init_egl(WaylandState &) override { return true; }
    bool configured() const override { return true; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { dashboard::request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        dashboard::handle_key_event(state_, app, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return dashboard::shell_bindings(state_, app);
    }

  private:
    State state_;
};

} // namespace

std::unique_ptr<Module> make_module() { return std::make_unique<Adapter>(); }

} // namespace astralia::dashboard
