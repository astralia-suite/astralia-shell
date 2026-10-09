#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/overview/view.h"
#include "modules/overview/wayland/overview.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

namespace {

std::string bound_output_name(const OverviewState &state, const WaylandState &app) {
    for (auto &mon : app.outputs)
        if (mon->output.wl == state.bound_output)
            return mon->output.name;
    return {};
}

astralia::OverviewModel &ensure_model(OverviewState &state, WaylandState &app) {
    if (!state.model) {
        state.model = std::make_unique<astralia::OverviewModel>(*app.desktop);
        state.model->on_close_requested = [&state, &app] { overview_toggle(state, app); };
    }
    return *state.model;
}

} // namespace

bool overview_create_surface(OverviewState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell, "astralia-shell-overview", output);
}

bool overview_init_egl(OverviewState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    state.canvas.bind(renderer);
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state] {
        overview_paint(state, *state.app_ptr);
    };
    return true;
}

void overview_retarget(OverviewState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(state.base, display, state.bound_output, target_output, target_name, [&](wl_output *out) { return overview_create_surface(state, compositor, layer_shell, out); }, [&] { return overview_init_egl(state, renderer, egl_display, egl_config, egl_context); });
    if (bound)
        state.bound_output = bound;
}

void overview_request_frame(OverviewState &state) {
    overlay_panel_request_frame(state.base);
}

void overview_toggle(OverviewState &state, WaylandState &app, bool by_widget) {
    if (!compositor_available(app))
        return;
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE)
        return;

    astralia::OverviewModel &model = ensure_model(state, app);
    bool opening = !state.base.open;
    if (opening) {
        app.desktop->refresh();
        state.opened_by_widget = by_widget;
        model.open(bound_output_name(state, app), static_cast<float>(state.base.width), static_cast<float>(state.base.height));
    } else {
        model.close();
    }
    overlay_panel_toggle(state.base);
    overview_request_frame(state);
}

std::vector<astralia::ShellBinding> overview_shell_bindings(OverviewState &overview, WaylandState &state) {
    return {
        {astralia::ShellVerb::overview,
         [&overview, &state] {
             if (!overview.base.open) {
                 MonitorOutput *target = app_detail::active_target_monitor(state);
                 if (target && (target->output.wl != overview.bound_output || !overview.base.layer_surface))
                     overview_retarget(overview, state.compositor, state.layer_shell, state.display, state.renderer, state.egl_display, state.egl_config, state.egl_context, target->output.wl, target->output.name.c_str());
             }
             overview_toggle(overview, state);
         }},
    };
}

void overview_handle_click(OverviewState &state, WaylandState &app, double px, double py) {
    ensure_model(state, app).press(px, py);
}

bool overview_point_is_clickable(OverviewState &state, WaylandState &app, double px, double py) {
    return state.base.open && ensure_model(state, app).clickable(px, py);
}

void overview_handle_pointer_move(OverviewState &state, WaylandState &app, double px, double py) {
    ensure_model(state, app).move(px, py);
}

void overview_handle_pointer_release(OverviewState &state, WaylandState &app) {
    ensure_model(state, app).release();
}

void overview_handle_key_event(OverviewState &state, WaylandState &app, const KeyEvent &event) {
    if (!state.base.open)
        return;
    ensure_model(state, app).key(to_neutral(event));
    overview_request_frame(state);
}

void overview_paint(OverviewState &state, WaylandState &app) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);
    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(state.base.output_scale.scale);
    bool animating = false;
    if (state.model) {
        astralia::OverviewModel &model = *state.model;
        model.tick(now);
        if (state.base.open && compositor_available(app)) {
            model.sync(static_cast<float>(state.base.width), static_cast<float>(state.base.height));
            astralia::paint_overview(state.canvas, model, [&](astralia::ui::Canvas &, const astralia::OverviewTile &tile, const astralia::ui::Box &rect, float radius) {
                toplevel_export_request(state.capture, app.toplevel_export_manager, app.shm, tile.address, astralia::overview_config::capture_interval_ms);
                const Texture *tex = toplevel_export_texture(state.capture, tile.address);
                if (!tex || !tex->id)
                    return false;
                static const float white[4] = {1, 1, 1, 1};
                node_add_texture_rect_rounded(state.canvas.group(), rect.x, rect.y, rect.w, rect.h, radius, *tex, white);
                return true;
            });
            std::vector<std::string> live_addresses;
            for (const astralia::CompositorClient &c : app.desktop->state().clients)
                live_addresses.push_back(c.address);
            toplevel_export_prune(state.capture, live_addresses);
        }
        animating = model.animating() || model.dragging();
    }
    state.canvas.set_opacity(state.base.opacity);
    state.canvas.flush();
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive() || animating)
        overlay_panel_request_frame(state.base);
}

namespace {

class OverviewModule final : public Module {
  public:
    const char *name() const override { return "overview"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = overview_create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!overview_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        state_.app_ptr = &app;
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { overview_request_frame(state_); }

    int poll_timeout_ms() const override {
        return state_.base.open ? astralia::overview_config::capture_interval_ms : -1;
    }

    bool tick() override {
        if (!state_.base.open)
            return false;
        auto now = std::chrono::steady_clock::now();
        if (now - last_capture_arm_ < std::chrono::milliseconds(astralia::overview_config::capture_interval_ms))
            return false;
        last_capture_arm_ = now;
        return true;
    }

    void handle_pointer_move(WaylandState &app, wl_surface *, double x, double y) override {
        overview_handle_pointer_move(state_, app, x, y);
        hovering_clickable_ =
            app.pointer.focused_surface == state_.base.surface && overview_point_is_clickable(state_, app, x, y);
    }
    bool wants_pointing_hand_cursor() const override {
        return hovering_clickable_;
    }
    void handle_pointer_release() override {
        if (state_.app_ptr)
            overview_handle_pointer_release(state_, *state_.app_ptr);
    }

    void handle_click(WaylandState &app, double x, double y) override {
        overview_handle_click(state_, app, x, y);
        request_frame();
    }
    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        overview_handle_key_event(state_, app, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return overview_shell_bindings(state_, app);
    }

    bool opened_by_widget() const override { return state_.opened_by_widget; }
    void on_output_removed(WaylandState &, wl_output *out) override {
        if (state_.bound_output != out)
            return;
        overlay_panel_release_output(state_.base, state_.bound_output, out);
        state_.opened_by_widget = false;
    }
    void toggle_from_widget(WaylandState &app) override {
        if (!state_.base.open) {
            MonitorOutput *target = app_detail::active_target_monitor(app);
            if (target && (target->output.wl != state_.bound_output || !state_.base.layer_surface))
                overview_retarget(state_, app.compositor, app.layer_shell, app.display, app.renderer, app.egl_display, app.egl_config, app.egl_context, target->output.wl, target->output.name.c_str());
        }
        overview_toggle(state_, app, true);
    }

  private:
    OverviewState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
    bool hovering_clickable_ = false;
    std::chrono::steady_clock::time_point last_capture_arm_{};
};

} // namespace

std::unique_ptr<Module> make_overview_module() {
    return std::make_unique<OverviewModule>();
}
