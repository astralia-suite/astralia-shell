#include <GLES3/gl32.h>
#include <chrono>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/render/gl.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/renderer.h"

#include "modules/wallpaper_select/model.h"
#include "modules/wallpaper_select/view.h"
#include "modules/wallpaper_select/wayland/wallpaper_select.h"

#include "config/wallpaper_select_config.h"

namespace {

namespace cfg = astralia::wallpaper_select_config;

constexpr const char *kNamespace = "astralia-shell-wallpaper-select";

class WallpaperSelectModule final : public Module {
  public:
    const char *name() const override { return "wallpaper-select"; }
    bool is_open() const override { return base_.open; }

    void on_output_removed(WaylandState &, wl_output *out) override {
        if (!out || bound_output_ != out)
            return;
        overlay_panel_release_output(base_, bound_output_, out);
        enabled_ = false;
        if (model_)
            model_->close();
    }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = overlay_panel_create_surface(base_, app.compositor, app.layer_shell, kNamespace,
                                             output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        renderer_ = &app.renderer;
        canvas_.bind(app.renderer);
        if (!overlay_panel_init_egl(base_, app.egl_display, app.egl_config, app.egl_context))
            return false;
        base_.frame_clock.draw = [this] { paint(); };
        canvas_.bind_context(
            [this] { gl_make_current(base_.egl_display, base_.egl_surface, base_.egl_context); });
        canvas_.on_image_ready = [this] { request_frame(); };

        if (!model_) {
            astralia::WallpaperSelectHooks hooks;
            hooks.config = [&app]() -> const Config & { return app.cfg; };
            hooks.update = [&app](const std::function<void(Config &)> &edit) {
                Config updated = app.cfg;
                edit(updated);
                app.suppress_wallpaper_transition = true;
                app_detail::save_and_apply_config_update(app, updated);
                app.suppress_wallpaper_transition = false;
            };
            hooks.settled = [] { return false; };
            model_ = std::make_unique<astralia::WallpaperSelectModel>(
                std::move(hooks), app.capabilities.animated_wallpaper);
            model_->on_close_requested = [this] { toggle(); };
            model_->on_closed = [this] { overlay_panel_release(base_); };
            model_->on_changed = [this] { request_frame(); };
        }
        bound_output_ = output_;
        enabled_ = true;
        app.on_wallpaper_uploaded = [this](const std::string &monitor, int column) {
            if (model_)
                model_->wallpaper_settled(monitor, column);
        };
        app.open_wallpaper_select = [this, &app](const std::string &monitor, int column) {
            for (auto &mon : app.outputs)
                if (mon->output.name == monitor) {
                    open_on(app, *mon, column);
                    return;
                }
        };
        return true;
    }

    bool configured() const override { return !want_ || base_.configured; }
    wl_surface *surface() const override { return base_.surface; }
    void request_frame() override { overlay_panel_request_frame(base_); }

    void handle_click(WaylandState &, double x, double y) override {
        if (model_)
            model_->click(x, y);
    }
    void handle_pointer_move(WaylandState &, wl_surface *focused_surface, double x,
                             double y) override {
        hovering_card_ =
            model_ && base_.open && focused_surface == base_.surface && model_->clickable(x, y);
    }
    bool wants_pointing_hand_cursor() const override { return hovering_card_; }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        if (model_ && model_->key(to_neutral(event)))
            request_frame();
    }
    void handle_scroll(WaylandState &, double dy) override {
        if (model_)
            model_->scroll(static_cast<float>(dy) / cfg::wayland_wheel_unit);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return {
            {astralia::ShellVerb::wallpaper,
             [this, &app] {
                 if (base_.open) {
                     toggle();
                     return;
                 }
                 if (MonitorOutput *target = app_detail::active_target_monitor(app))
                     open_on(app, *target, 0);
             }},
        };
    }

  private:
    void open_on(WaylandState &app, MonitorOutput &target, int column) {
        if (!enabled_ || !model_ || base_.open || !model_->available())
            return;
        if (target.output.wl != bound_output_ || !base_.layer_surface) {
            wl_output *bound = overlay_panel_retarget(
                base_, app.display, bound_output_, target.output.wl, target.output.name.c_str(),
                [&](wl_output *out) {
                    output_ = out;
                    want_ = overlay_panel_create_surface(base_, app.compositor, app.layer_shell,
                                                         kNamespace, out);
                    return want_;
                },
                [&] { return init_egl(app); });
            if (!bound) {
                enabled_ = false;
                app_detail::rest_egl_current(app);
                return;
            }
            bound_output_ = bound;
            app_detail::rest_egl_current(app);
        }
        model_->open(target.output.name, column);
        overlay_panel_toggle(base_, false);
        request_frame();
    }

    void toggle() {
        if (!model_ || !base_.open)
            return;
        request_frame();
        model_->close();
    }

    void paint() {
        if (base_.egl_surface == EGL_NO_SURFACE)
            return;
        auto now = std::chrono::steady_clock::now();
        base_.animations.tick(now);
        if (model_)
            model_->tick(now);
        gl_make_current(base_.egl_display, base_.egl_surface, base_.egl_context);
        int32_t scale = base_.output_scale.scale;
        renderer_->begin_frame(base_.width, base_.height, scale);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        canvas_.begin(scale);
        if (model_ && base_.open)
            astralia::paint_wallpaper_select(canvas_, *model_, static_cast<float>(base_.width),
                                             static_cast<float>(base_.height));
        canvas_.flush();
        eglSwapBuffers(base_.egl_display, base_.egl_surface);

        if (base_.animations.hasActive() || (model_ && model_->animating()))
            overlay_panel_request_frame(base_);
    }

    OverlayPanelBase base_;
    Renderer *renderer_ = nullptr;
    GlCanvas canvas_;
    std::unique_ptr<astralia::WallpaperSelectModel> model_;
    wl_output *output_ = nullptr;
    wl_output *bound_output_ = nullptr;
    bool want_ = false;
    bool enabled_ = false;
    bool hovering_card_ = false;
};

} // namespace

std::unique_ptr<Module> make_wallpaper_select_module() {
    return std::make_unique<WallpaperSelectModule>();
}
