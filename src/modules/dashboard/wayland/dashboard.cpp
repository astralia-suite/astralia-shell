#include <GLES3/gl32.h>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/dashboard/wayland/dashboard.h"

#include "render/tokens.h"
#include "wayland/render/gl.h"
#include "wayland/render/node.h"
#include "wayland/render/overlay_panel.h"

void dashboard_request_frame(DashboardState &state) {
    toplevel_window_request_frame(state.base);
}

void dashboard_toggle(DashboardState &state, WaylandState &app) {
    bool opening = !state.base.open;
    if (opening) {
        if (state.base.egl_surface == EGL_NO_SURFACE) {
            if (!toplevel_window_create_surface(state.base, app.compositor, app.wm_base, kDashboardTitle, kDashboardAppId, kDashboardDefaultWidth, kDashboardDefaultHeight))
                return;
            while (!state.base.configured)
                wl_display_dispatch(app.display);
            if (!toplevel_window_init_egl(state.base, app.egl_display, app.egl_config, app.egl_context)) {
                toplevel_window_destroy_surface(state.base);
                return;
            }
            state.renderer = &app.renderer;
            state.base.frame_clock.draw = [&state] { dashboard_paint(state); };
            state.base.on_close_request = [&state, &app] {
                dashboard_toggle(state, app);
            };
        }
        state.base.open = true;
        state.base.animations.animate(state.base.opacity, 1.0f, kOverlayFadeMs, astralia::Easing::EaseOutCubic, [&state](float v) { state.base.opacity = v; }, {}, kOverlayFadeOwner);
        toplevel_window_request_frame(state.base);
    } else {
        state.base.animations.cancelForOwner(kOverlayFadeOwner);
        state.base.open = false;
        toplevel_window_destroy_surface(state.base);
        app_detail::rest_egl_current(app);
    }
}

void dashboard_handle_key_event(DashboardState &state, WaylandState &app, const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        dashboard_toggle(state, app);
}

std::vector<astralia::ShellBinding> dashboard_shell_bindings(DashboardState &dashboard, WaylandState &state) {
    return {
        {astralia::ShellVerb::dashboard, [&dashboard, &state] { dashboard_toggle(dashboard, state); }},
    };
}

void dashboard_paint(DashboardState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.animations.tick(std::chrono::steady_clock::now());

    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    node_add_rect(&state.scene.root, 0.0f, 0.0f, static_cast<float>(state.base.width), static_cast<float>(state.base.height), astralia::rgba(astralia::palette::window_backdrop));

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        toplevel_window_request_frame(state.base);
}

namespace {

class DashboardModule final : public Module {
  public:
    const char *name() const override { return "dashboard"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }
    bool init_egl(WaylandState &) override { return true; }
    bool configured() const override { return true; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { dashboard_request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        dashboard_handle_key_event(state_, app, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return dashboard_shell_bindings(state_, app);
    }

  private:
    DashboardState state_;
};

} // namespace

std::unique_ptr<Module> make_dashboard_module() {
    return std::make_unique<DashboardModule>();
}
