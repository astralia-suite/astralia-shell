#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "core/spawn.h"

#include "modules/logout/layout.h"
#include "modules/logout/view.h"
#include "modules/logout/wayland/logout.h"

#include "render/tokens.h"

namespace astralia::logout {

astralia::ui::Box detail::button_rect(int index, float center_x, float center_y) {
    Point c = button_center(index, {center_x, center_y});
    float half = kButtonSize / 2.0f;
    return {static_cast<float>(c.x) - half, static_cast<float>(c.y) - half, kButtonSize,
            kButtonSize};
}

namespace {

void finish_close(State &state) {
    state.base.open = false;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
}

int star_vertex(int step_index) {
    return step_index * astralia::logout_config::star_step % kButtonCount;
}

} // namespace

bool create_surface(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
                    wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "astralia-shell-logout", output);
}

bool init_egl(State &state, Renderer &renderer, EGLDisplay display, EGLConfig config,
              EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.canvas.bind(renderer);
    state.base.frame_clock.draw = [&state] { render(state); };
    state.model.on_execute = [](const std::string &command) { astralia::spawn_detached(command); };
    state.model.on_closed = [&state] { finish_close(state); };
    return true;
}

void retarget(State &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell,
              wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config,
              EGLContext egl_context, wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(
        state.base, display, state.bound_output, target_output, target_name,
        [&](wl_output *out) { return logout::create_surface(state, compositor, layer_shell, out); },
        [&] { return logout::init_egl(state, renderer, egl_display, egl_config, egl_context); });
    if (bound)
        state.bound_output = bound;
}

void request_frame(State &state) { overlay_panel_request_frame(state.base); }

void toggle(State &state, bool by_widget) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE)
        return;

    bool opening = !state.base.open;
    klog("logout: toggle open=%d by_widget=%d", opening, by_widget);
    if (opening) {
        state.base.open = true;
        state.base.opacity = 1.0f;
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        overlay_panel_update_input_region(state.base);
        wl_surface_commit(state.base.surface);
        state.opened_by_widget = by_widget;
        state.model.open();
    } else {
        state.model.toggle();
    }
    overlay_panel_request_frame(state.base);
}

std::vector<astralia::ShellBinding> shell_bindings(State &logout, WaylandState &state) {
    return {
        {astralia::ShellVerb::logout,
         [&logout, &state] {
             if (!logout.base.open) {
                 MonitorOutput *target = app_detail::active_target_monitor(state);
                 if (target &&
                     (target->output.wl != logout.bound_output || !logout.base.layer_surface))
                     retarget(logout, state.compositor, state.layer_shell, state.display,
                              state.renderer, state.egl_display, state.egl_config,
                              state.egl_context, target->output.wl, target->output.name.c_str());
             }
             logout.logo_path = state.cfg.logout_logo_path;
             logout::toggle(logout);
         }},
    };
}

void handle_key_event(State &state, const KeyEvent &event) {
    state.model.key(to_neutral(event));
    overlay_panel_request_frame(state.base);
}

void handle_click(State &state, double px, double py) {
    state.model.click(px, py, state.base.width, state.base.height);
    overlay_panel_request_frame(state.base);
}

void handle_hover(State &state, double px, double py) {
    state.model.hover(px, py, state.base.width, state.base.height);
}

void clear_hover(State &state) { state.model.clear_hover(); }

void render(State &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.model.tick(now);
    if (!gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context))
        return;
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(state.base.output_scale.scale);
    state.canvas.set_opacity(state.base.opacity);
    if (state.base.open) {
        logout::paint(state.canvas, state.model, static_cast<float>(state.base.width),
                      static_cast<float>(state.base.height), state.logo_path);
    }
    state.canvas.flush();

    if (state.base.open) {
        float cx = static_cast<float>(state.base.width) / 2.0f;
        float cy = static_cast<float>(state.base.height) / 2.0f;
        float time_s = std::chrono::duration<float>(now - state.model.burst_started()).count();
        astralia::Color core = astralia::with_alpha(astralia::palette::white, 1.0f);
        astralia::Color glow = astralia::with_alpha(astralia::palette::electro, 1.0f);

        ThunderParams p;
        p.time_s = time_s;
        p.core = astralia::rgba(core);
        p.glow = astralia::rgba(glow);
        p.amp = kBoltAmp;

        for (int e = 0; e < kButtonCount; ++e) {
            float s = state.model.slash()[static_cast<size_t>(e)];
            if (s <= 0.002f)
                continue;

            astralia::ui::Box va = detail::button_rect(star_vertex(e), cx, cy);
            astralia::ui::Box vb = detail::button_rect(star_vertex(e + 1), cx, cy);
            float ax = va.x + va.w / 2.0f;
            float ay = va.y + va.h / 2.0f;
            float bx = vb.x + vb.w / 2.0f;
            float by = vb.y + vb.h / 2.0f;
            float ox = (bx - ax) * kSlashOvershoot;
            float oy = (by - ay) * kSlashOvershoot;

            p.ax = ax - ox;
            p.ay = ay - oy;
            p.bx = bx + ox;
            p.by = by + oy;
            p.seed = static_cast<float>(e) * 1.7f + 1.0f;
            p.progress = s * 2.0f < 1.0f ? s * 2.0f : 1.0f;
            p.intensity = 4.0f * s * (1.0f - s) * 1.2f;
            thunder_burst_draw(state.thunder, *state.renderer, p);
        }

        float burst = state.model.burst();
        if (burst > 0.002f && burst < 0.999f) {
            ThunderShockParams s;
            s.cx = cx;
            s.cy = cy;
            s.radius = kBurstRingMax;
            s.time_s = time_s;
            s.progress = burst;
            s.intensity = 1.3f;
            s.core = astralia::rgba(core);
            s.glow = astralia::rgba(glow);
            thunder_shock_draw(state.thunder, *state.renderer, s);

            p.ax = cx - kFinishSpan / 2.0f;
            p.ay = cy + kFinishRise / 2.0f;
            p.bx = cx + kFinishSpan / 2.0f;
            p.by = cy - kFinishRise / 2.0f;
            float sweep = burst * kFinishSweep;
            p.progress = sweep < 1.0f ? sweep : 1.0f;
            p.intensity = (1.0f - burst) * kFinishIntensity;
            p.seed = 21.0f;
            p.amp = kBoltAmp * 2.4f;
            p.thick = kFinishThick;
            thunder_burst_draw(state.thunder, *state.renderer, p);

            if (sweep >= 1.0f) {
                float head_end = 1.0f / kFinishSweep;
                float linger = 1.0f - (burst - head_end) / (1.0f - head_end);
                linger = linger < 0.0f ? 0.0f : linger;
                float crackle =
                    0.55f + 0.45f * std::sin(time_s * 71.0f) * std::sin(time_s * 127.0f);
                p.progress = 1.0f;
                p.intensity = linger * linger * kFinishLingerIntensity * crackle;
                p.seed = 53.0f;
                p.amp = kBoltAmp * 1.5f;
                p.thick = kFinishThick * 0.55f;
                thunder_burst_draw(state.thunder, *state.renderer, p);
            }
        }
    }

    gl_check("logout_paint");
    if (!eglSwapBuffers(state.base.egl_display, state.base.egl_surface))
        klog("logout: eglSwapBuffers failed, egl error 0x%04x", eglGetError());

    if (state.model.animating())
        overlay_panel_request_frame(state.base);
}

namespace {

class Adapter final : public Module {
  public:
    const char *name() const override { return "logout"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = logout::create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!logout::init_egl(state_, app.renderer, app.egl_display, app.egl_config,
                              app.egl_context))
            return false;
        state_.bound_output = output_;
        request_frame();
        return true;
    }

    bool configured() const override { return !want_ || state_.base.configured; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { logout::request_frame(state_); }

    bool timer_tick(WaylandState &) override { return false; }

    void handle_pointer_move(WaylandState &, wl_surface *focused_surface, double x,
                             double y) override {
        if (!state_.base.open)
            return;
        if (focused_surface == state_.base.surface)
            handle_hover(state_, x, y);
        else
            logout::clear_hover(state_);
        request_frame();
    }

    void handle_click(WaylandState &, double x, double y) override {
        logout::handle_click(state_, x, y);
    }
    bool wants_hand_cursor() const override {
        return state_.base.open && state_.model.hovered() >= 0;
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        logout::handle_key_event(state_, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return logout::shell_bindings(state_, app);
    }

    bool opened_by_widget() const override { return state_.opened_by_widget; }
    wl_output *bound_output() const override { return state_.bound_output; }
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
                retarget(state_, app.compositor, app.layer_shell, app.display, app.renderer,
                         app.egl_display, app.egl_config, app.egl_context, target->output.wl,
                         target->output.name.c_str());
        }
        state_.logo_path = app.cfg.logout_logo_path;
        logout::toggle(state_, true);
    }

  private:
    State state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

} // namespace

std::unique_ptr<Module> make_module() { return std::make_unique<Adapter>(); }

} // namespace astralia::logout
