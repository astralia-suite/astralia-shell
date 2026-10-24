#include <GLES3/gl32.h>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/render/gl.h"
#include "wayland/render/overlay_panel.h"
#include "wayland/render/text.h"

#include "modules/bamboo/wayland/bamboo.h"

#include "render/tokens.h"

namespace astralia::bamboo {

namespace {

void rebuild(State &state) {
    state.texture = Texture{};
    state.grow.rebuild(state.base.width, state.base.height);
    state.built_width = state.base.width;
    state.built_height = state.base.height;
    state.last_tick = std::chrono::steady_clock::now();
}

void upload(State &state) {
    Frame frame = state.grow.frame();
    if (!frame.ready())
        return;
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        const_cast<uint8_t *>(frame.pixels), CAIRO_FORMAT_ARGB32, frame.width, frame.height,
        frame.stride);
    RasterizedText raster = surface_to_rgba(surface, frame.width, frame.height);
    cairo_surface_destroy(surface);
    state.texture = make_texture_rgba(raster.width, raster.height, raster.rgba.data());
}

} // namespace

void request_frame(State &state) { toplevel_window_request_frame(state.base); }

void toggle(State &state, WaylandState &app) {
    bool opening = !state.base.open;
    if (opening) {
        if (state.base.egl_surface == EGL_NO_SURFACE) {
            if (!toplevel_window_create_surface(state.base, app.compositor, app.wm_base, "Bamboo",
                                                "astralia-shell-bamboo",
                                                astralia::bamboo_config::default_window_width,
                                                astralia::bamboo_config::default_window_height))
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
        bamboo::rebuild(state);
        upload(state);
    }

    if (opening) {
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

std::vector<astralia::ShellBinding> shell_bindings(State &bamboo, WaylandState &state) {
    return {
        {astralia::ShellVerb::bamboo, [&bamboo, &state] { toggle(bamboo, state); }},
    };
}

void render(State &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);

    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    gl_make_current(state.base.egl_display, state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height, state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.renderer->set_opacity(state.base.opacity);

    float win_w = static_cast<float>(state.base.width);
    float win_h = static_cast<float>(state.base.height);

    state.renderer->draw_rect(0.0f, 0.0f, win_w, win_h,
                              astralia::rgba(astralia::palette::window_backdrop));

    if (state.base.width != state.built_width || state.base.height != state.built_height) {
        bamboo::rebuild(state);
        upload(state);
    }

    float elapsed_ms = std::chrono::duration<float, std::milli>(now - state.last_tick).count();
    if (elapsed_ms >= astralia::bamboo_config::tick_interval_ms) {
        state.grow.tick();
        upload(state);
        state.last_tick = now;
    }

    if (state.texture.id != 0) {
        static constexpr float white[4] = {1, 1, 1, 1};
        state.renderer->draw_texture_rect(0.0f, 0.0f, win_w, win_h, state.texture, white);
    }
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.open || state.base.animations.has_active())
        toplevel_window_request_frame(state.base);
}

namespace {

class Adapter final : public Module {
  public:
    const char *name() const override { return "bamboo"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }
    bool init_egl(WaylandState &) override { return true; }
    bool configured() const override { return true; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { bamboo::request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        bamboo::handle_key_event(state_, app, event);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return bamboo::shell_bindings(state_, app);
    }

  private:
    State state_;
};

} // namespace

std::unique_ptr<Module> make_module() { return std::make_unique<Adapter>(); }

} // namespace astralia::bamboo
