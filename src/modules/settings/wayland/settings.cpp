#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>

#include "wayland/app/monitor_output.h"
#include "wayland/app/user_info.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/settings/wayland/settings.h"

#include "render/tokens.h"

#include "config/settings_config.h"

#include "wayland/render/gl.h"
#include "wayland/render/renderer.h"

namespace {

namespace cfg = astralia::settings_config;

astralia::SettingsCaps wayland_caps() {
    astralia::SettingsCaps caps = astralia::settings_caps(astralia::wayland_capabilities());
    caps.logout = true;
    caps.wallpaper_columns = true;
    caps.default_wallpaper = true;
    caps.bar_toggle = false;
    caps.autohide = true;
    caps.remember_tab = false;
    return caps;
}

} // namespace

bool settings_create_surface(SettingsState &state, wl_compositor *compositor, zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell, "astralia-shell-settings", output);
}

bool settings_init_egl(SettingsState &state, WaylandState &app) {
    state.renderer = &app.renderer;
    state.canvas.bind(app.renderer);
    if (!overlay_panel_init_egl(state.base, app.egl_display, app.egl_config, app.egl_context))
        return false;
    state.base.frame_clock.draw = [&state] { settings_paint(state); };
    state.canvas.bind_context([&state] { gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context); });
    state.canvas.on_image_ready = [&state] { settings_request_frame(state); };

    if (!state.model) {
        astralia::SettingsHooks hooks;
        hooks.config = [&app]() -> const Config & { return app.cfg; };
        hooks.update = [&app](const std::function<void(Config &)> &edit) {
            Config updated = app.cfg;
            edit(updated);
            app_detail::save_and_apply_config_update(app, updated);
        };
        hooks.monitors = [&app] {
            std::vector<std::string> names;
            for (const auto &mon : app.outputs)
                names.push_back(mon->output.name);
            return names;
        };
        hooks.focused_monitor = [&app] { return app.desktop->state().focused_monitor; };
        hooks.cpu_fallback = [&state, &app](const std::string &monitor, int column) {
            return state.decode_status_source && state.decode_status_source(app, monitor, column) == MediaDecodeStatus::CpuFallback;
        };
        state.model = std::make_unique<astralia::SettingsModel>(wayland_caps(), std::move(hooks));
        state.model->on_close_requested = [&state, &app] { settings_toggle(state, app); };
        state.model->on_changed = [&state] { settings_request_frame(state); };
        state.model->on_text_focus = [&state](bool focused) {
            if (state.sync_text_input_focus)
                state.sync_text_input_focus(focused);
        };
    }

    AnimatedImageStyle pfp_style;
    pfp_style.size = cfg::avatar_size;
    pfp_style.circular = true;
    pfp_style.ring_fill = astralia::rgba(astralia::palette::text_alpha04);
    pfp_style.border_color = astralia::rgba(astralia::palette::accent);
    pfp_style.border_width = cfg::avatar_border;
    pfp_style.decode = {15, static_cast<int>(cfg::avatar_size) * 2};
    animated_image_set_source(state.profile_pic, user_info::profile_media_path(), pfp_style);
    return true;
}

void settings_request_frame(SettingsState &state) {
    overlay_panel_request_frame(state.base);
}

void settings_toggle(SettingsState &state, WaylandState &app) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE || !state.model) {
        klog("settings: toggle ignored, surface not ready (layer_surface=%p egl_surface_ready=%d)", static_cast<void *>(state.base.layer_surface), state.base.egl_surface != EGL_NO_SURFACE);
        return;
    }
    bool opening = !state.base.open;
    klog("settings: toggle called (was_open=%d opacity=%.2f)", state.base.open, static_cast<double>(state.base.opacity));
    if (opening) {
        state.model->open();
    } else {
        state.model->close();
    }
    overlay_panel_toggle(state.base);
    if (opening)
        animated_image_show(state.profile_pic, [&state] { settings_request_frame(state); });
    settings_request_frame(state);
    (void)app;
}

namespace {

void settings_retarget(WaylandState &app, SettingsState &s, MonitorOutput &target) {
    wl_output *bound = overlay_panel_retarget(s.base, app.display, s.bound_output, target.output.wl, target.output.name.c_str(), [&](wl_output *out) { return settings_create_surface(s, app.compositor, app.layer_shell, out); }, [&] { return settings_init_egl(s, app); });
    if (bound)
        s.bound_output = bound;
    else
        s.enabled = false;
    app_detail::rest_egl_current(app);
}

} // namespace

std::vector<astralia::ShellBinding> settings_shell_bindings(SettingsState &settings, WaylandState &state) {
    return {
        {astralia::ShellVerb::settings,
         [&settings, &state] {
             if (!settings.base.open && settings.enabled) {
                 MonitorOutput *target = app_detail::active_target_monitor(state);
                 if (target && (target->output.wl != settings.bound_output || !settings.base.layer_surface))
                     settings_retarget(state, settings, *target);
             }
             settings_toggle(settings, state);
         }},
    };
}

void settings_paint(SettingsState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);
    if (state.model)
        state.model->tick(now);
    if (state.base.open)
        animated_image_tick(state.profile_pic, now);
    else
        animated_image_hide(state.profile_pic);
    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.canvas.begin(scale);
    state.canvas.set_opacity(state.base.opacity);
    state.frame = {};
    if (state.model && state.base.opacity > 0.0f) {
        state.model->sync();
        astralia::SettingsArt art;
        art.user_name = user_info::username();
        art.uptime = user_info::uptime_string();
        art.avatar = [&state](astralia::ui::Canvas &, const astralia::ui::Box &box) {
            if (state.profile_pic.frames.empty())
                return false;
            animated_image_draw(state.profile_pic, state.canvas, box.x, box.y, box.w, box.h, 1.0f);
            return true;
        };
        state.frame = astralia::paint_settings(state.canvas, *state.model, art, static_cast<float>(state.base.width), static_cast<float>(state.base.height));
    }
    state.canvas.flush();
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive() || animated_image_animating(state.profile_pic) || (state.model && state.model->animating()))
        overlay_panel_request_frame(state.base);
}

TextInputState settings_text_input_state(const SettingsState &state) {
    TextInputState s;
    s.purpose = TextInputPurpose::Normal;
    s.cursor_rect_x = static_cast<int32_t>(state.frame.caret.x);
    s.cursor_rect_y = static_cast<int32_t>(state.frame.caret.y);
    s.cursor_rect_w = static_cast<int32_t>(state.frame.caret.w);
    s.cursor_rect_h = static_cast<int32_t>(state.frame.caret.h);
    return s;
}

void settings_text_input_apply_edit(SettingsState &state, const TextInputEdit &edit) {
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

class SettingsModule final : public Module, public TextInputClient {
  public:
    explicit SettingsModule(SettingsDecodeStatusFn decode_status_source) {
        state_.decode_status_source = std::move(decode_status_source);
    }

    const char *name() const override { return "settings"; }
    bool is_open() const override { return state_.base.open; }

    void on_output_removed(WaylandState &, wl_output *out) override {
        if (!out || state_.bound_output != out)
            return;
        if (state_.sync_text_input_focus)
            state_.sync_text_input_focus(false);
        wl_output *bound = state_.bound_output;
        overlay_panel_release_output(state_.base, bound, out);
        state_.bound_output = nullptr;
        state_.enabled = false;
        if (state_.model)
            state_.model->close();
    }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = settings_create_surface(state_, app.compositor, app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!settings_init_egl(state_, app))
            return false;
        state_.bound_output = output_;
        state_.enabled = true;
        state_.sync_text_input_focus = [this, &app](bool focused) {
            if (focused)
                app.text_input.set_focused_client(state_.base.surface, this);
            else
                app.text_input.clear_focused_client(this);
        };
        return true;
    }

    TextInputState text_input_state() const override {
        return settings_text_input_state(state_);
    }
    void text_input_apply_edit(const TextInputEdit &edit) override {
        settings_text_input_apply_edit(state_, edit);
        request_frame();
    }
    void text_input_reset_preedit() override {
        if (state_.model)
            state_.model->field().preedit.clear();
        request_frame();
    }
    void text_input_activated(TextInputService &) override {}
    void text_input_deactivated(TextInputService &) override {
        if (state_.model)
            state_.model->field().preedit.clear();
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { settings_request_frame(state_); }

    bool timer_tick(WaylandState &) override {
        if (!state_.base.open || !state_.model)
            return false;
        state_.model->toggle_caret();
        request_frame();
        return true;
    }

    void handle_click(WaylandState &, double x, double y) override {
        if (state_.model)
            state_.model->click(x, y);
    }
    void handle_pointer_move(WaylandState &, wl_surface *focused_surface, double x, double y) override {
        hovering_clickable_ = state_.model && state_.base.open && focused_surface == state_.base.surface && state_.model->clickable(x, y);
    }
    bool wants_pointing_hand_cursor() const override {
        return hovering_clickable_;
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        if (state_.model && state_.model->key(to_neutral(event)))
            request_frame();
    }
    void handle_scroll(WaylandState &, double dy) override {
        if (state_.model)
            state_.model->scroll(static_cast<float>(dy) * cfg::scroll_speed);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return settings_shell_bindings(state_, app);
    }

  private:
    SettingsState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
    bool hovering_clickable_ = false;
};

} // namespace

std::unique_ptr<Module> make_settings_module(SettingsDecodeStatusFn decode_status_source) {
    return std::make_unique<SettingsModule>(std::move(decode_status_source));
}
