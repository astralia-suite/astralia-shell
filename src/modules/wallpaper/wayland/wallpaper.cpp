#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <chrono>
#include <linux/input-event-codes.h>
#include <memory>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"
#include "wayland/render/renderer.h"

#include "modules/wallpaper/view.h"
#include "modules/wallpaper/wayland/wallpaper.h"

#include "render/tokens.h"

#include "service/wallpaper_service.h"

namespace {

void wallpaper_paint(WallpaperState &wp);

void wallpaper_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface,
                                       uint32_t serial, uint32_t width, uint32_t height) {
    auto *wp = static_cast<WallpaperState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    bool changed =
        wp->width != static_cast<int32_t>(width) || wp->height != static_cast<int32_t>(height);
    wp->width = static_cast<int32_t>(width);
    wp->height = static_cast<int32_t>(height);
    if (changed && wp->egl_window) {
        int32_t scale = wp->output_scale.scale;
        wl_egl_window_resize(wp->egl_window, wp->width * scale, wp->height * scale, 0, 0);
        if (wp->frame_clock.surface)
            request_frame(wp->frame_clock);
    }
    wp->configured = true;
    if (changed && wp->on_resize)
        wp->on_resize();
}

void wallpaper_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener wallpaper_layer_surface_listener = {
    .configure = wallpaper_layer_surface_configure,
    .closed = wallpaper_layer_surface_closed,
};

void wallpaper_draw_transitions(WallpaperState &wp) {
    if (wp.columns.empty())
        return;
    size_t ncol = std::max<size_t>(wp.columns.size(), 1);
    float column_w = static_cast<float>(wp.width) / static_cast<float>(ncol);
    bool any = false;
    for (size_t i = 0; i < wp.columns.size(); ++i) {
        auto &slot = wp.columns[i];
        if (!slot)
            continue;
        if (wallpaper_column_draw_transition(*wp.renderer, *slot, static_cast<float>(i) * column_w,
                                             0.0f, column_w, static_cast<float>(wp.height)))
            any = true;
    }
    if (any)
        wallpaper_request_frame(wp);
}

void wallpaper_paint(WallpaperState &wp) {
    if (wp.egl_surface == EGL_NO_SURFACE)
        return;
    if (wp.app && wp.app->session_locked && wp.dbg_frame > 0) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            klog("wallpaper: paint suspended while session locked");
        }
        frame_clock_drop_callback(wp.frame_clock);
        return;
    }

    if (!gl_make_current(wp.egl_display, wp.egl_surface, wp.egl_context))
        return;
    wp.renderer->begin_frame(wp.width, wp.height, wp.output_scale.scale);
    glClearColor(astralia::palette::base.r, astralia::palette::base.g, astralia::palette::base.b,
                 astralia::palette::base.a);
    glClear(GL_COLOR_BUFFER_BIT);

    wp.canvas.begin(wp.output_scale.scale);
    wallpaper_draw_columns(wp, wp.canvas, wp.width, wp.height);
    wp.canvas.flush();
    wallpaper_draw_transitions(wp);
    if (wp.menu.is_open()) {
        wp.canvas.begin(wp.output_scale.scale);
        astralia::paint_desktop_menu(wp.canvas, wp.menu, wp.menu.box().x, wp.menu.box().y);
        wp.canvas.flush();
    }
    gl_check("wallpaper_paint");

    auto sw0 = std::chrono::steady_clock::now();
    if (!eglSwapBuffers(wp.egl_display, wp.egl_surface))
        klog("wallpaper: eglSwapBuffers failed, egl error 0x%04x", eglGetError());
    float sw =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - sw0).count();
    if (sw > 5.0f)
        klog("wallpaper: eglSwapBuffers %.1fms", sw);

    ++wp.dbg_frame;
}

} // namespace

void wallpaper_draw_columns(const WallpaperState &wp, GlCanvas &canvas, int32_t width,
                            int32_t height) {
    size_t columns = std::max<size_t>(wp.columns.size(), 1);
    float column_w = static_cast<float>(width) / static_cast<float>(columns);
    for (size_t i = 0; i < wp.columns.size(); ++i)
        if (wp.columns[i])
            wallpaper_column_draw(*wp.columns[i], canvas, static_cast<float>(i) * column_w,
                                  column_w, static_cast<float>(height));
}

bool wallpaper_create_surface(WallpaperState &wp, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        .name_space = kWallpaperLayerNamespace,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    wp.layer_surface = layer_surface_create(wp.surface, compositor, layer_shell, cfg,
                                            &wallpaper_layer_surface_listener, &wp, output);
    if (!wp.layer_surface)
        return false;
    wp.output_scale.on_change = [&wp](int32_t scale) {
        if (wp.egl_window)
            wl_egl_window_resize(wp.egl_window, wp.width * scale, wp.height * scale, 0, 0);
        if (wp.frame_clock.surface)
            request_frame(wp.frame_clock);
        if (wp.on_resize)
            wp.on_resize();
    };
    output_scale_watch(wp.output_scale, wp.surface);
    wl_surface_commit(wp.surface);
    return true;
}

bool wallpaper_init_egl(WallpaperState &wp, Renderer &renderer, EGLDisplay display,
                        EGLConfig config, EGLContext context) {
    wp.egl_display = display;
    wp.egl_context = context;
    wp.renderer = &renderer;
    wp.canvas.bind(renderer);
    int32_t scale = wp.output_scale.scale;
    wp.egl_window = wl_egl_window_create(wp.surface, wp.width * scale, wp.height * scale);
    wp.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(wp.egl_window), nullptr);
    if (wp.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, wp.egl_surface, context))
        return false;
    wp.gl.display = display;
    wp.gl.make_current = [&wp] { gl_make_current(wp.egl_display, wp.egl_surface, wp.egl_context); };
    wp.gl.request_frame = [&wp] { wallpaper_request_frame(wp); };
    wp.frame_clock.surface = wp.surface;
    wp.frame_clock.draw = [&wp] { wallpaper_paint(wp); };
    return true;
}

void wallpaper_request_frame(WallpaperState &wp) {
    if (wp.egl_surface == EGL_NO_SURFACE || (wp.app && wp.app->session_locked))
        return;
    request_frame(wp.frame_clock);
}

void wallpaper_wake(WallpaperState &wp) {
    if (wp.egl_surface == EGL_NO_SURFACE)
        return;
    frame_clock_drop_callback(wp.frame_clock);
    request_frame(wp.frame_clock);
}

void wallpaper_columns_stop_all(WallpaperState &wp) {
    for (auto &col : wp.columns)
        if (col && col->decode.stop_flag)
            col->decode.stop_flag->store(true);
    for (auto &col : wp.columns)
        if (col)
            wallpaper_column_clear(*col, wp.gl);
}

void wallpaper_columns_pause_all(WallpaperState &wp) {
    for (auto &col : wp.columns)
        if (col)
            media_decode_pause(col->decode);
}

void wallpaper_columns_resume_all(WallpaperState &wp) {
    for (auto &col : wp.columns)
        if (col)
            media_decode_resume(col->decode);
}

MediaDecodeStatus wallpaper_column_status(const WallpaperState &wp, int column_index) {
    if (column_index < 0 || static_cast<size_t>(column_index) >= wp.columns.size() ||
        !wp.columns[static_cast<size_t>(column_index)])
        return MediaDecodeStatus::Idle;
    return wallpaper_column_decode_status(*wp.columns[static_cast<size_t>(column_index)]);
}

void wallpaper_sync_from_config(WallpaperState &wp, const Config &cfg,
                                const std::string &monitor_name, bool animated) {
    int count = astralia::wallpaper_column_count(cfg, monitor_name, animated);

    if (!animated) {
        bool any_animated = false;
        for (auto &col : wp.columns)
            if (col && col->decode.stop_flag)
                any_animated = true;
        if (any_animated)
            wallpaper_columns_stop_all(wp);
    }

    for (size_t i = static_cast<size_t>(count); i < wp.columns.size(); ++i)
        if (wp.columns[i])
            wallpaper_column_clear(*wp.columns[i], wp.gl);
    wp.columns.resize(static_cast<size_t>(count));

    int target_w = (wp.width / std::max(1, count)) * std::max(1, wp.output_scale.scale);
    int target_h = wp.height * std::max(1, wp.output_scale.scale);
    AnimateSize sz = animated ? animate_decode_size(target_w, target_h, kAnimateMaxDecodeDim)
                              : AnimateSize{target_w, target_h};

    for (int i = 0; i < count; ++i) {
        auto &slot = wp.columns[static_cast<size_t>(i)];
        if (!slot)
            slot = std::make_unique<WallpaperColumn>();
        WallpaperColumn &col = *slot;

        std::string path = astralia::wallpaper_column_path(cfg, monitor_name, i, animated);
        FillMode mode = astralia::wallpaper_fill_mode(cfg, monitor_name, i, animated) == "fit"
                            ? FillMode::Fit
                            : FillMode::Crop;

        if (path.empty()) {
            wallpaper_column_clear(col, wp.gl);
            continue;
        }
        if (animated) {
            if (col.decode.stop_flag && col.path == path && col.mode == mode &&
                col.target_w == sz.w && col.target_h == sz.h)
                continue;
            wallpaper_column_set_animated(col, wp.gl, path, sz.w, sz.h, mode);
        } else {
            col.on_uploaded = [&wp, i] {
                if (wp.app && wp.app->on_wallpaper_uploaded)
                    wp.app->on_wallpaper_uploaded(wp.output_name, i);
            };
            wallpaper_column_set_static(col, wp.gl, path, sz.w, sz.h, mode);
            if (wp.app && wp.app->suppress_wallpaper_transition)
                col.pending_transition = WallpaperTransition::None;
        }
    }
}

namespace {

void wallpaper_sync_active_mode(WallpaperState &wp, const Config &cfg,
                                const std::string &monitor_name) {
    wallpaper_sync_from_config(wp, cfg, monitor_name, cfg.wallpaper_animated_enabled);
}

} // namespace

bool WallpaperPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon,
                                               wl_output *output) {
    if (!wallpaper_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("wallpaper: failed to create layer surface on '%s'", mon.output.name.c_str());
    return true;
}

bool WallpaperPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool WallpaperPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (!state_.layer_surface)
        return true;
    state_.app = &app;
    state_.output_name = mon.output.name;
    if (!wallpaper_init_egl(state_, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        return true;
    wallpaper_sync_active_mode(state_, app.cfg, mon.output.name);
    state_.on_resize = [&app, &mon, this] {
        if (!app.cfg.wallpaper_animated_enabled)
            return;
        wallpaper_sync_from_config(state_, app.cfg, mon.output.name, true);
    };
    wallpaper_request_frame(state_);
    app_detail::rest_egl_current(app);
    return true;
}

void WallpaperPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    wallpaper_columns_stop_all(state_);
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface, state_.egl_window,
                          state_.egl_surface, &state_.frame_clock);
}

bool WallpaperPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void WallpaperPerMonitorModule::pause_animation() { wallpaper_columns_pause_all(state_); }

void WallpaperPerMonitorModule::resume_animation() { wallpaper_columns_resume_all(state_); }

void WallpaperPerMonitorModule::request_frame() { wallpaper_wake(state_); }

void WallpaperPerMonitorModule::handle_click(WaylandState &app, MonitorOutput &mon,
                                             wl_surface *surface, int button, double x, double y,
                                             uint32_t) {
    if (surface != state_.surface || state_.width <= 0 || state_.height <= 0)
        return;
    astralia::DesktopMenuModel &menu = state_.menu;
    if (button == BTN_RIGHT) {
        size_t columns = std::max<size_t>(state_.columns.size(), 1);
        int column = std::clamp(static_cast<int>(x / (static_cast<double>(state_.width) /
                                                      static_cast<double>(columns))),
                                0, static_cast<int>(columns) - 1);
        menu.toggle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(state_.width),
                    static_cast<float>(state_.height), column);
        if (menu.is_open())
            menu.hover(static_cast<float>(x) - menu.box().x, static_cast<float>(y) - menu.box().y);
        wallpaper_request_frame(state_);
        return;
    }
    if (button != BTN_LEFT || !menu.is_open())
        return;
    int entry =
        menu.entry_at(static_cast<float>(x) - menu.box().x, static_cast<float>(y) - menu.box().y);
    int column = menu.column();
    menu.close();
    wallpaper_request_frame(state_);
    if (entry < 0)
        return;
    if (astralia::DesktopMenuModel::entries()[static_cast<size_t>(entry)].action ==
        astralia::DesktopMenuAction::settings) {
        if (app.run_verb)
            app.run_verb(astralia::ShellVerb::settings);
    } else if (app.open_wallpaper_select) {
        app.open_wallpaper_select(mon.output.name, column);
    }
}

void WallpaperPerMonitorModule::handle_pointer_move(WaylandState &app, MonitorOutput &, double x,
                                                    double y) {
    if (!state_.menu.is_open())
        return;
    bool changed = false;
    if (app.pointer.focused_surface == state_.surface)
        changed = state_.menu.hover(static_cast<float>(x) - state_.menu.box().x,
                                    static_cast<float>(y) - state_.menu.box().y);
    else if (state_.menu.hovered() >= 0) {
        state_.menu.clear_hover();
        changed = true;
    }
    if (changed)
        wallpaper_request_frame(state_);
}

bool WallpaperPerMonitorModule::wants_pointing_hand_cursor() const {
    return state_.menu.is_open() && state_.menu.hovered() >= 0;
}

MediaDecodeStatus WallpaperPerMonitorModule::decode_status(int column_index) const {
    return wallpaper_column_status(state_, column_index);
}

void WallpaperPerMonitorModule::apply_config(WaylandState &, MonitorOutput &mon,
                                             const Config &new_cfg) {
    wallpaper_sync_active_mode(state_, new_cfg, mon.output.name);
    wallpaper_request_frame(state_);
}
