#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "core/spawn.h"
#include "wayland/core/log.h"

#include "modules/logout/layout.h"
#include "modules/logout/view.h"

#include "modules/logout/wayland/logout.h"

#include "render/tokens.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"
#include "wayland/render/node.h"

void thunder_burst_draw(ThunderBurst &tb, Renderer &renderer, const ThunderParams &p) {
    if (!tb.bolt_tried) {
        tb.bolt_tried = true;
        tb.bolt_program = gl_compile_program_files("renderer/quad.vert", "logout/thunder_burst.frag", "thunder_bolt");
        klog("logout: thunder bolt_program=%u", tb.bolt_program);
    }
    if (!tb.bolt_program || !p.core || !p.glow)
        return;

    float pad = p.pad + p.amp * 3.0f + p.thick * 30.0f;
    float min_x = std::min(p.ax, p.bx) - pad;
    float min_y = std::min(p.ay, p.by) - pad;
    float w = std::fabs(p.bx - p.ax) + pad * 2.0f;
    float h = std::fabs(p.by - p.ay) + pad * 2.0f;

    float ax = p.ax - min_x;
    float ay = p.ay - min_y;
    float bx = p.bx - min_x;
    float by = p.by - min_y;

    if (!tb.bolt_logged) {
        tb.bolt_logged = true;
        klog("logout: bolt thick=%.3f amp=%.2f intensity=%.3f progress=%.3f "
             "seed=%.2f quad=%.0fx%.0f a=(%.0f,%.0f) b=(%.0f,%.0f) time=%.2f",
             p.thick, p.amp, p.intensity, p.progress, p.seed, w, h, ax, ay, bx, by, p.time_s);
    }

    renderer.draw_custom(tb.bolt_program, min_x, min_y, w, h, [&](GLuint prog) {
        static bool loc_logged = false;
        if (!loc_logged) {
            loc_logged = true;
            klog("logout: bolt loc a_pos=%d size=%d a=%d b=%d seed=%d "
                 "time=%d",
                 glGetAttribLocation(prog, "a_pos"), glGetUniformLocation(prog, "u_size"), glGetUniformLocation(prog, "u_a"), glGetUniformLocation(prog, "u_b"), glGetUniformLocation(prog, "u_seed"), glGetUniformLocation(prog, "u_time"));
        }
        glUniform2f(glGetUniformLocation(prog, "u_size"), w, h);
        glUniform2f(glGetUniformLocation(prog, "u_a"), ax, ay);
        glUniform2f(glGetUniformLocation(prog, "u_b"), bx, by);
        glUniform1f(glGetUniformLocation(prog, "u_time"), p.time_s);
        glUniform1f(glGetUniformLocation(prog, "u_progress"), p.progress);
        glUniform1f(glGetUniformLocation(prog, "u_intensity"), p.intensity);
        glUniform1f(glGetUniformLocation(prog, "u_seed"), p.seed);
        glUniform1f(glGetUniformLocation(prog, "u_amp"), p.amp);
        glUniform1f(glGetUniformLocation(prog, "u_thick"), p.thick);
        glUniform4fv(glGetUniformLocation(prog, "u_core"), 1, p.core);
        glUniform4fv(glGetUniformLocation(prog, "u_glow"), 1, p.glow);
    });
}

void thunder_shock_draw(ThunderBurst &tb, Renderer &renderer, const ThunderShockParams &p) {
    if (!tb.shock_tried) {
        tb.shock_tried = true;
        tb.shock_program = gl_compile_program_files("renderer/quad.vert", "logout/thunder_shock.frag", "thunder_shock");
        klog("logout: thunder shock_program=%u", tb.shock_program);
    }
    if (!tb.shock_program || !p.core || !p.glow || p.radius <= 0.0f)
        return;

    if (!tb.shock_logged) {
        tb.shock_logged = true;
        klog("logout: thunder shock draw radius=%.2f intensity=%.3f "
             "progress=%.3f",
             p.radius, p.intensity, p.progress);
    }

    float min_x = p.cx - p.radius;
    float min_y = p.cy - p.radius;
    float side = p.radius * 2.0f;

    float lcx = p.cx - min_x;
    float lcy = p.cy - min_y;

    renderer.draw_custom(tb.shock_program, min_x, min_y, side, side, [&](GLuint prog) {
        glUniform2f(glGetUniformLocation(prog, "u_size"), side, side);
        glUniform2f(glGetUniformLocation(prog, "u_center"), lcx, lcy);
        glUniform1f(glGetUniformLocation(prog, "u_time"), p.time_s);
        glUniform1f(glGetUniformLocation(prog, "u_progress"), p.progress);
        glUniform1f(glGetUniformLocation(prog, "u_radius"), p.radius);
        glUniform1f(glGetUniformLocation(prog, "u_intensity"), p.intensity);
        glUniform4fv(glGetUniformLocation(prog, "u_core"), 1, p.core);
        glUniform4fv(glGetUniformLocation(prog, "u_glow"), 1, p.glow);
    });
}

astralia::ui::Box logout_detail_button_rect(int index, float center_x, float center_y) {
    astralia::Point c = astralia::logout_button_center(index, {center_x, center_y});
    float half = kLogoutButtonSize / 2.0f;
    return {static_cast<float>(c.x) - half, static_cast<float>(c.y) - half, kLogoutButtonSize, kLogoutButtonSize};
}

namespace {

AnimatedImage &active_logo(LogoutState &state) {
    return state.logo_animated ? state.logo_gif : state.logo_png;
}

void finish_close(LogoutState &state) {
    animated_image_hide(active_logo(state));
    state.base.open = false;
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
}

int star_vertex(int step_index) {
    return step_index * astralia::logout_config::star_step % kLogoutButtonCount;
}

} // namespace

bool logout_create_surface(LogoutState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell, "astralia-shell-logout", output);
}

bool logout_init_egl(LogoutState &state, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.canvas.bind(renderer);
    state.base.frame_clock.draw = [&state] { logout_paint(state); };
    state.model.on_execute = [](const std::string &command) { astralia::spawn_detached(command); };
    state.model.on_closed = [&state] { finish_close(state); };
    return true;
}

void logout_retarget(LogoutState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_display *display, Renderer &renderer, EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context, wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(state.base, display, state.bound_output, target_output, target_name, [&](wl_output *out) { return logout_create_surface(state, compositor, layer_shell, out); }, [&] { return logout_init_egl(state, renderer, egl_display, egl_config, egl_context); });
    if (bound)
        state.bound_output = bound;
}

void logout_request_frame(LogoutState &state) {
    overlay_panel_request_frame(state.base);
}

void logout_apply_logo_config(LogoutState &state, bool animated) {
    if (!state.logo_source_set) {
        auto resolve = [](const char *installed, const char *dev) {
            return std::string(std::filesystem::exists(installed) ? installed : dev);
        };

        AnimatedImageStyle gif_style;
        gif_style.size = kLogoutLogoSize;
        gif_style.decode = {30, static_cast<int>(kLogoutLogoSize)};
        gif_style.circular = true;
        gif_style.border_width = kLogoutBorderWidth;
        gif_style.border_color = astralia::rgba(astralia::palette::accent);
        animated_image_set_source(state.logo_gif, resolve(ASTRALIA_SHELL_LOGOUT_LOGO, "assets/logout/logo.gif"), gif_style);

        AnimatedImageStyle png_style;
        png_style.size = kLogoutLogoSize;
        png_style.decode = {1, static_cast<int>(kLogoutLogoSize), AnimateFit::Fit};
        animated_image_set_source(state.logo_png, resolve(ASTRALIA_SHELL_LOGOUT_LOGO_STATIC, "assets/logout/logo.png"), png_style);
        state.logo_source_set = true;
    }

    state.logo_animated = animated;
    animated_image_hide(animated ? state.logo_png : state.logo_gif);
}

void logout_toggle(LogoutState &state, bool by_widget) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE)
        return;

    bool opening = !state.base.open;
    klog("logout: toggle open=%d by_widget=%d", opening, by_widget);
    if (opening) {
        state.base.open = true;
        state.base.opacity = 1.0f;
        zwlr_layer_surface_v1_set_keyboard_interactivity(state.base.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        overlay_panel_update_input_region(state.base);
        wl_surface_commit(state.base.surface);
        state.opened_by_widget = by_widget;
        animated_image_show(active_logo(state), [&state] { logout_request_frame(state); });
        state.model.open();
    } else {
        state.model.toggle();
    }
    overlay_panel_request_frame(state.base);
}

std::vector<astralia::ShellBinding> logout_shell_bindings(LogoutState &logout, WaylandState &state) {
    return {
        {astralia::ShellVerb::logout,
         [&logout, &state] {
             if (!logout.base.open) {
                 MonitorOutput *target = app_detail::active_target_monitor(state);
                 if (target && (target->output.wl != logout.bound_output || !logout.base.layer_surface))
                     logout_retarget(logout, state.compositor, state.layer_shell, state.display, state.renderer, state.egl_display, state.egl_config, state.egl_context, target->output.wl, target->output.name.c_str());
             }
             logout_apply_logo_config(logout, state.cfg.logout_animated_logo);
             logout_toggle(logout);
         }},
    };
}

void logout_handle_key_event(LogoutState &state, const KeyEvent &event) {
    state.model.key(to_neutral(event));
    overlay_panel_request_frame(state.base);
}

void logout_handle_click(LogoutState &state, double px, double py) {
    state.model.click(px, py, state.base.width, state.base.height);
    overlay_panel_request_frame(state.base);
}

void logout_handle_hover(LogoutState &state, double px, double py) {
    state.model.hover(px, py, state.base.width, state.base.height);
}

void logout_clear_hover(LogoutState &state) {
    state.model.clear_hover();
}

void logout_paint(LogoutState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.model.tick(now);
    animated_image_tick(active_logo(state), now);
    if (!gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context))
        return;
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(state.base.output_scale.scale);
    if (state.base.open) {
        astralia::paint_logout(state.canvas, state.model, static_cast<float>(state.base.width), static_cast<float>(state.base.height), [&state](astralia::ui::Canvas &, const astralia::ui::Box &area, float alpha) {
            animated_image_draw(active_logo(state), state.canvas.group(), area.x, area.y, area.w, area.h, alpha);
        });
    }
    state.canvas.set_opacity(state.base.opacity);
    state.canvas.flush();

    if (state.base.open) {
        float cx = static_cast<float>(state.base.width) / 2.0f;
        float cy = static_cast<float>(state.base.height) / 2.0f;
        float time_s =
            std::chrono::duration<float>(now - state.model.burst_started()).count();
        astralia::Color core = astralia::with_alpha(astralia::palette::text, 1.0f);
        astralia::Color glow = astralia::with_alpha(astralia::palette::electro, 1.0f);

        ThunderParams p;
        p.time_s = time_s;
        p.core = astralia::rgba(core);
        p.glow = astralia::rgba(glow);
        p.amp = kLogoutBoltAmp;

        for (int e = 0; e < kLogoutButtonCount; ++e) {
            float s = state.model.slash()[static_cast<size_t>(e)];
            if (s <= 0.002f)
                continue;

            astralia::ui::Box va = logout_detail_button_rect(star_vertex(e), cx, cy);
            astralia::ui::Box vb = logout_detail_button_rect(star_vertex(e + 1), cx, cy);
            float ax = va.x + va.w / 2.0f;
            float ay = va.y + va.h / 2.0f;
            float bx = vb.x + vb.w / 2.0f;
            float by = vb.y + vb.h / 2.0f;
            float ox = (bx - ax) * kLogoutSlashOvershoot;
            float oy = (by - ay) * kLogoutSlashOvershoot;

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
            s.radius = kLogoutBurstRingMax;
            s.time_s = time_s;
            s.progress = burst;
            s.intensity = 1.3f;
            s.core = astralia::rgba(core);
            s.glow = astralia::rgba(glow);
            thunder_shock_draw(state.thunder, *state.renderer, s);

            p.ax = cx - kLogoutFinishSpan / 2.0f;
            p.ay = cy + kLogoutFinishRise / 2.0f;
            p.bx = cx + kLogoutFinishSpan / 2.0f;
            p.by = cy - kLogoutFinishRise / 2.0f;
            float sweep = burst * kLogoutFinishSweep;
            p.progress = sweep < 1.0f ? sweep : 1.0f;
            p.intensity = (1.0f - burst) * kLogoutFinishIntensity;
            p.seed = 21.0f;
            p.amp = kLogoutBoltAmp * 2.4f;
            p.thick = kLogoutFinishThick;
            thunder_burst_draw(state.thunder, *state.renderer, p);

            if (sweep >= 1.0f) {
                float head_end = 1.0f / kLogoutFinishSweep;
                float linger =
                    1.0f - (burst - head_end) / (1.0f - head_end);
                linger = linger < 0.0f ? 0.0f : linger;
                float crackle = 0.55f + 0.45f * std::sin(time_s * 71.0f) *
                                            std::sin(time_s * 127.0f);
                p.progress = 1.0f;
                p.intensity =
                    linger * linger * kLogoutFinishLingerIntensity * crackle;
                p.seed = 53.0f;
                p.amp = kLogoutBoltAmp * 1.5f;
                p.thick = kLogoutFinishThick * 0.55f;
                thunder_burst_draw(state.thunder, *state.renderer, p);
            }
        }
    }

    gl_check("logout_paint");
    if (!eglSwapBuffers(state.base.egl_display, state.base.egl_surface))
        klog("logout: eglSwapBuffers failed, egl error 0x%04x", eglGetError());

    if (state.model.animating() || animated_image_animating(active_logo(state)))
        overlay_panel_request_frame(state.base);
}

namespace {

class LogoutModule final : public Module {
  public:
    const char *name() const override { return "logout"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = logout_create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!logout_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        request_frame();

        logout_apply_logo_config(state_, app.cfg.logout_animated_logo);
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { logout_request_frame(state_); }

    bool timer_tick(WaylandState &) override { return false; }

    void handle_pointer_move(WaylandState &, wl_surface *focused_surface, double x, double y) override {
        if (!state_.base.open)
            return;
        if (focused_surface == state_.base.surface)
            logout_handle_hover(state_, x, y);
        else
            logout_clear_hover(state_);
        request_frame();
    }

    void handle_click(WaylandState &, double x, double y) override {
        logout_handle_click(state_, x, y);
    }
    bool wants_pointing_hand_cursor() const override {
        return state_.base.open && state_.model.hovered() >= 0;
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        logout_handle_key_event(state_, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return logout_shell_bindings(state_, app);
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
                logout_retarget(state_, app.compositor, app.layer_shell, app.display, app.renderer, app.egl_display, app.egl_config, app.egl_context, target->output.wl, target->output.name.c_str());
        }
        logout_apply_logo_config(state_, app.cfg.logout_animated_logo);
        logout_toggle(state_, true);
    }

  private:
    LogoutState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

} // namespace

std::unique_ptr<Module> make_logout_module() {
    return std::make_unique<LogoutModule>();
}
