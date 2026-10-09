#include <GLES3/gl32.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <vector>

#include "wayland/core/log.h"

#include "config/bar_layout.h"

#include "modules/bar/model.h"
#include "modules/bar/panel/panel_set.h"
#include "modules/bar/view.h"
#include "modules/bar/wayland/bar.h"

#include "wayland/render/gl.h"
#include "wayland/render/layer_surface.h"

#include "service/wayland/output_service.h"

namespace {

namespace layout_cfg = astralia::bar_layout;
using astralia::BarItem;
using astralia::PanelId;

constexpr int32_t kAutoHideStripPx = 1;
constexpr float kAutoHideRevealMs = 150.0f;
constexpr float kAutoHideHideMs = 150.0f;
constexpr uint64_t kAutoHideAnimOwner = 1000;

PanelId panel_of(BarItem item) {
    switch (item) {
    case BarItem::tray:
        return PanelId::tray;
    case BarItem::resource:
        return PanelId::resource;
    case BarItem::network:
        return PanelId::network;
    case BarItem::bluetooth:
        return PanelId::bluetooth;
    case BarItem::volume:
        return PanelId::volume;
    case BarItem::battery:
        return PanelId::battery;
    case BarItem::media:
        return PanelId::media;
    case BarItem::brightness:
        return PanelId::brightness;
    case BarItem::clock:
        return PanelId::clock;
    case BarItem::logout:
    case BarItem::count:
        break;
    }
    return PanelId::count;
}

std::optional<BarItem> item_of(PanelId panel) {
    for (size_t i = 0; i < astralia::bar_item_count; ++i) {
        if (panel_of(static_cast<BarItem>(i)) == panel)
            return static_cast<BarItem>(i);
    }
    return std::nullopt;
}

std::string clock_text() {
    char buf[32];
    time_t now = time(nullptr);
    strftime(buf, sizeof(buf), layout_cfg::clock_format, localtime(&now));
    return buf;
}

void set_surface_geometry(zwlr_layer_surface_v1 *layer_surface, wl_surface *surface, wl_egl_window *egl_window, int32_t width, int32_t height_px, int32_t margin_top, int32_t margin_side, int32_t exclusive_zone, int32_t output_scale) {
    zwlr_layer_surface_v1_set_size(layer_surface, 0, height_px);
    zwlr_layer_surface_v1_set_margin(layer_surface, margin_top, margin_side, 0, margin_side);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, exclusive_zone);
    wl_surface_commit(surface);
    if (egl_window)
        wl_egl_window_resize(egl_window, width * output_scale, height_px * output_scale, 0, 0);
}

void bar_layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t) {
    auto *mon = static_cast<MonitorOutput *>(data);
    BarPerMonitorState &bs = bar_state(*mon);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    bs.width = static_cast<int32_t>(width);
    if (bs.egl_window) {
        int32_t scale = bs.output_scale.scale;
        wl_egl_window_resize(bs.egl_window, bs.width * scale, bar_detail::bar_current_height(*mon) * scale, 0, 0);
    }
    bs.configured = true;
}

void bar_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

const zwlr_layer_surface_v1_listener bar_layer_surface_listener = {
    .configure = bar_layer_surface_configure,
    .closed = bar_layer_surface_closed,
};

astralia::BarSources bar_sources(WaylandState &app, const std::string &output) {
    astralia::BarSources sources;
    sources.audio = app.audio.get();
    sources.battery = app.battery.get();
    sources.bluetooth = app.bluetooth.get();
    sources.network = app.network.get();
    sources.brightness = app.brightness_service.get();
    sources.compositor = [&app]() -> const astralia::CompositorState & { return app.desktop->state(); };
    sources.output = output;
    sources.resource = true;
    return sources;
}

void adjust_volume(WaylandState &app, double dy) {
    astralia::AudioService &audio = *app.audio;
    if (audio.sink_id() == 0)
        return;
    int next = std::clamp(audio.sink().percent + (dy < 0 ? 5 : -5), 0, 150);
    audio.set_volume(audio.sink_id(), next);
}

void adjust_brightness(WaylandState &app, double dy) {
    astralia::BrightnessService &service = *app.brightness_service;
    if (!service.available())
        return;
    service.set(std::clamp(service.percent() + (dy < 0 ? 1 : -1), 0, 100));
}

} // namespace

BarPerMonitorState &bar_state(MonitorOutput &mon) {
    return mon.module<BarPerMonitorModule>()->state;
}

const BarPerMonitorState &bar_state(const MonitorOutput &mon) {
    return mon.module<BarPerMonitorModule>()->state;
}

namespace bar_detail {

void close_other_overlays(MonitorOutput &mon, BarItem keep) {
    BarPerMonitorState &bs = bar_state(mon);
    if (keep != BarItem::logout) {
        if (Module *m = find_overlay_by_name(*mon.app, "logout"); m && m->is_open())
            m->toggle_from_widget(*mon.app);
    }
    if (bs.panels)
        bs.panels->panels().close_except(panel_of(keep));
}

void toggle_panel(MonitorOutput &mon, BarItem item) {
    BarPerMonitorState &bs = bar_state(mon);
    if (!bs.panels)
        return;
    close_other_overlays(mon, item);
    bs.panels->panels().toggle(panel_of(item));
}

int32_t bar_current_height(const MonitorOutput &mon) {
    return astralia::bar_autohide_geometry(bar_state(mon).autohide.enabled, bar_state(mon).autohide.collapsed, static_cast<int>(layout_cfg::height), bar_style_of(mon).top_margin, bar_hug_radius_px(mon)).height;
}

void bar_autohide_apply_geometry(MonitorOutput &mon, bool autohide, bool collapsed, const astralia::BarStyleSpec &style) {
    astralia::BarGeometry g = astralia::bar_autohide_geometry(autohide, collapsed, static_cast<int>(layout_cfg::height), style.top_margin, bar_hug_radius_px(mon));
    set_surface_geometry(bar_state(mon).layer_surface, bar_state(mon).surface, bar_state(mon).egl_window, bar_state(mon).width, g.height, g.margin_top, style.side_margin, g.exclusive_zone, bar_state(mon).output_scale.scale);
}

void monitor_autohide_apply(MonitorOutput &mon, bool enabled, const astralia::BarStyleSpec &style) {
    bar_state(mon).autohide.enabled = enabled;
    bar_state(mon).autohide.hidden = false;
    bar_state(mon).autohide.collapsed = enabled && bar_state(mon).autohide.collapsed;
    bar_state(mon).autohide.opacity = bar_state(mon).autohide.collapsed ? 0.0f : 1.0f;
    bar_autohide_apply_geometry(mon, enabled, bar_state(mon).autohide.collapsed, style);
}

} // namespace bar_detail

bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display, EGLConfig config, EGLContext context) {
    BarPerMonitorState &bs = bar_state(mon);
    int32_t scale = bs.output_scale.scale;
    bs.egl_window = wl_egl_window_create(bs.surface, bs.width * scale, bar_detail::bar_current_height(mon) * scale);
    bs.egl_surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(bs.egl_window), nullptr);
    if (bs.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!gl_make_current(display, bs.egl_surface, context))
        return false;

    const char *renderer_name = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    klog("egl: renderer=%s output='%s'", renderer_name ? renderer_name : "(unknown)", mon.output.name.c_str());

    bs.canvas.bind(renderer);
    bs.frame_clock.surface = bs.surface;
    bs.frame_clock.draw = [&mon] { bar_paint(mon); };
    return true;
}

void dispatch_bar_click(MonitorOutput &mon, double click_x, double click_y) {
    BarPerMonitorState &bs = bar_state(mon);
    if (!bs.model)
        return;
    if (bs.autohide.enabled)
        click_y -= bar_style_of(mon).top_margin;
    astralia::BarAction action = bs.model->press(click_x, click_y);
    switch (action.kind) {
    case astralia::BarActionKind::toggle:
        if (action.item == BarItem::logout) {
            bar_detail::close_other_overlays(mon, BarItem::logout);
            if (Module *logout = find_overlay_by_name(*mon.app, "logout"))
                logout->toggle_from_widget(*mon.app);
        } else {
            bar_detail::toggle_panel(mon, action.item);
        }
        break;
    case astralia::BarActionKind::overview:
        if (Module *m = find_overlay_by_name(*mon.app, "overview"))
            m->toggle_from_widget(*mon.app);
        break;
    case astralia::BarActionKind::workspace:
        mon.app->desktop->focus_workspace(action.workspace, true);
        break;
    case astralia::BarActionKind::none:
        break;
    }
}

void bar_paint(MonitorOutput &mon) {
    WaylandState &app = *mon.app;
    BarPerMonitorState &bs = bar_state(mon);
    const astralia::BarStyleSpec &style = bar_style_of(mon);
    auto now = std::chrono::steady_clock::now();

    bs.animations.tick(now);
    if (!bs.model)
        return;
    astralia::BarModel &model = *bs.model;
    model.set_style(style);
    model.set_clock_label(bs.clock_label);

    Module *logout_m = find_overlay_by_name(app, "logout");
    Module *overview_m = find_overlay_by_name(app, "overview");
    bool overview_here = overview_m && overview_m->is_open() && overview_m->opened_by_widget() && overview_m->bound_output() == mon.output.wl;
    bool logout_here = logout_m && logout_m->is_open() && logout_m->opened_by_widget() && logout_m->bound_output() == mon.output.wl;
    std::optional<BarItem> open;
    bool clock_open = false;
    if (bs.panels) {
        const astralia::PanelSet &set = bs.panels->panels();
        if (astralia::PanelId id = set.active_id(); id != astralia::PanelId::count)
            open = item_of(id);
        if (!open) {
            for (size_t i = 0; i < static_cast<size_t>(astralia::PanelId::count); ++i) {
                const astralia::Panel *panel = set.find(static_cast<astralia::PanelId>(i));
                if (panel != nullptr && panel->is_open()) {
                    open = item_of(static_cast<astralia::PanelId>(i));
                    break;
                }
            }
        }
        const astralia::Panel *clock = set.find(astralia::PanelId::clock);
        clock_open = clock != nullptr && clock->is_open();
    }
    if (!open && logout_here)
        open = BarItem::logout;
    model.set_open(open, now);

    if (bs.autohide.enabled) {
        bool want_shown = app.pointer.focused_surface == bs.surface || open.has_value() || clock_open || overview_here;
        if (want_shown == bs.autohide.hidden) {
            bs.autohide.hidden = !want_shown;
            if (want_shown && bs.autohide.collapsed) {
                bs.autohide.collapsed = false;
                bar_detail::bar_autohide_apply_geometry(mon, true, false, style);
            }
            float target = want_shown ? 1.0f : 0.0f;
            float duration = want_shown ? kAutoHideRevealMs : kAutoHideHideMs;
            bs.animations.animate(bs.autohide.opacity, target, duration, astralia::Easing::EaseOutCubic, [&bs](float v) { bs.autohide.opacity = v; }, [&mon, &bs] {
                    if (bs.autohide.hidden && !bs.autohide.collapsed) {
                        bs.autohide.collapsed = true;
                        bar_detail::bar_autohide_apply_geometry(mon, true, true, bar_style_of(mon));
                    } }, kAutoHideAnimOwner);
        }
    }

    int32_t surface_height = bar_detail::bar_current_height(mon);
    float content_y_offset = bs.autohide.enabled ? static_cast<float>(style.top_margin) : 0.0f;
    float height = layout_cfg::height;
    float width = static_cast<float>(bs.width);

    gl_make_current(app.egl_display, bs.egl_surface, app.egl_context);
    app.renderer.begin_frame(bs.width, surface_height, bs.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    bs.canvas.begin(bs.output_scale.scale);
    bs.canvas.set_opacity(bs.autohide.enabled ? bs.autohide.opacity : 1.0f);

    bool pointer_here = app.pointer.focused_surface == bs.surface;
    model.refresh(now);
    model.hover(pointer_here ? std::optional<double>(app.pointer.x) : std::nullopt, pointer_here ? std::optional<double>(app.pointer.y - content_y_offset) : std::nullopt, now);
    model.tick(now);
    model.layout(bs.canvas, width);

    bs.canvas.begin_group({0.0f, content_y_offset, width, height}, {});
    astralia::BarFrame frame = model.frame();
    Node *content = bs.canvas.group();
    bar_frame_base(content, style, frame, width, height);
    astralia::paint_bar(bs.canvas, model);
    bar_frame_overlay(content, bs.decor, style, frame, width, height, bs.output_scale.scale, bar_hug_radius_px(mon), model.layout().left_end.has_value(), model.layout().right_start.has_value());
    bs.canvas.end_group();
    bs.canvas.flush();
    eglSwapBuffers(app.egl_display, bs.egl_surface);

    if (bs.animations.hasActive() || model.animating())
        bar_request_frame(mon);
}

void bar_request_frame(MonitorOutput &mon) {
    if (bar_state(mon).egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(bar_state(mon).frame_clock);
}

bool BarPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) {
    mon_ = &mon;
    state.autohide.enabled = autohide_effective_enabled(app.cfg, mon.output.name);
    state.model = std::make_unique<astralia::BarModel>(bar_sources(app, mon.output.name), std::chrono::steady_clock::now());
    state.clock_label = clock_text();
    const astralia::BarStyleSpec &style = astralia::bar_style_spec(app.cfg.bar_style, true);
    astralia::BarGeometry geometry = astralia::bar_autohide_geometry(state.autohide.enabled, state.autohide.collapsed, static_cast<int>(layout_cfg::height), style.top_margin, bar_hug_radius_px(mon));
    LayerSurfaceConfig bar_cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "astralia-shell",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .height = geometry.height,
        .margin_top = geometry.margin_top,
        .margin_right = style.side_margin,
        .margin_left = style.side_margin,
        .exclusive_zone = geometry.exclusive_zone,
    };
    state.layer_surface = layer_surface_create(state.surface, app.compositor, app.layer_shell, bar_cfg, &bar_layer_surface_listener, &mon, output);
    state.output_scale.on_change = [this, &mon](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale, bar_detail::bar_current_height(mon) * scale, 0, 0);
        if (state.frame_clock.surface)
            ::request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);

    state.panels = std::make_unique<PanelSurface>(app, mon);
    state.panels->create_surface(output);
    return true;
}

bool BarPerMonitorModule::configured() const {
    return state.configured && (!state.panels || state.panels->configured());
}

bool BarPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (!bar_init_egl(mon, app.renderer, app.egl_display, app.egl_config, app.egl_context))
        return false;
    if (state.panels)
        state.panels->init_egl();

    if (state.autohide.enabled) {
        state.autohide.hidden = true;
        state.autohide.collapsed = true;
        state.autohide.opacity = 0.0f;
    }
    bar_request_frame(mon);
    return true;
}

void BarPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    if (state.panels)
        state.panels->destroy();
    EGLDisplay d = app.egl_display;
    destroy_layer_surface(d, state.surface, state.layer_surface, state.egl_window, state.egl_surface, &state.frame_clock);
}

bool BarPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state.surface || (state.panels && state.panels->owns(surface));
}

void BarPerMonitorModule::request_frame() {
    if (!mon_)
        return;
    bar_request_frame(*mon_);
    if (state.panels)
        state.panels->request_frame();
}

void BarPerMonitorModule::apply_config(WaylandState &app, MonitorOutput &mon, const Config &new_cfg) {
    bool new_autohide = autohide_effective_enabled(new_cfg, mon.output.name);
    const astralia::BarStyleSpec &new_style = astralia::bar_style_spec(new_cfg.bar_style, true);
    if (new_autohide != state.autohide.enabled)
        bar_detail::monitor_autohide_apply(mon, new_autohide, new_style);
    else if (new_cfg.bar_style != app.cfg.bar_style)
        bar_detail::bar_autohide_apply_geometry(mon, state.autohide.enabled, state.autohide.collapsed, new_style);
}

std::vector<astralia::ShellBinding> BarPerMonitorModule::shell_bindings(WaylandState &app) {
    using astralia::ShellVerb;
    constexpr std::pair<ShellVerb, BarItem> entries[] = {
        {ShellVerb::panel_tray, BarItem::tray},
        {ShellVerb::panel_resource, BarItem::resource},
        {ShellVerb::panel_network, BarItem::network},
        {ShellVerb::panel_bluetooth, BarItem::bluetooth},
        {ShellVerb::panel_volume, BarItem::volume},
        {ShellVerb::panel_battery, BarItem::battery},
        {ShellVerb::panel_media, BarItem::media},
        {ShellVerb::panel_brightness, BarItem::brightness},
        {ShellVerb::panel_clock, BarItem::clock},
    };
    std::vector<astralia::ShellBinding> bindings;
    for (const auto &[verb, item] : entries) {
        bindings.push_back({verb, [&app, item] {
                                if (MonitorOutput *target = app_detail::active_target_monitor(app))
                                    bar_detail::toggle_panel(*target, item);
                            }});
    }
    return bindings;
}

void BarPerMonitorModule::tick(WaylandState &, MonitorOutput &mon) {
    int32_t hug = bar_hug_radius_px(mon);
    if (hug != state.applied_hug_radius_px) {
        state.applied_hug_radius_px = hug;
        bar_detail::bar_autohide_apply_geometry(mon, state.autohide.enabled, state.autohide.collapsed, bar_style_of(mon));
        bar_request_frame(mon);
    }
}

void BarPerMonitorModule::timer_tick(WaylandState &, MonitorOutput &mon) {
    state.clock_label = clock_text();
    bar_request_frame(mon);
}

bool BarPerMonitorModule::is_open() const {
    return state.panels && state.panels->is_open();
}

void BarPerMonitorModule::handle_click(WaylandState &app, MonitorOutput &mon, wl_surface *surface, int button, double x, double y, uint32_t) {
    if (state.panels && state.panels->owns(surface)) {
        state.panels->press(button, x, y);
        app_detail::rest_egl_current(app);
        return;
    }
    if (button != BTN_LEFT || surface != state.surface)
        return;
    dispatch_bar_click(mon, x, y);
    request_frame();
    for (auto &m : app.overlays) {
        m->request_frame();
        app_detail::rest_egl_current(app);
    }
}

void BarPerMonitorModule::handle_scroll(WaylandState &app, MonitorOutput &mon, wl_surface *surface, double dy) {
    if (state.panels && state.panels->owns(surface)) {
        state.panels->wheel(pointer_x_, pointer_y_, dy);
        return;
    }
    if (surface != state.surface || !state.model)
        return;
    double y = pointer_y_ - (state.autohide.enabled ? bar_style_of(mon).top_margin : 0);
    astralia::BarAction hit = state.model->press(pointer_x_, y);
    if (hit.kind != astralia::BarActionKind::toggle)
        return;
    if (hit.item == BarItem::volume)
        adjust_volume(app, dy);
    else if (hit.item == BarItem::brightness)
        adjust_brightness(app, dy);
}

void BarPerMonitorModule::handle_key_event(WaylandState &app, MonitorOutput &, const KeyEvent &event) {
    if (state.panels && state.panels->is_open()) {
        state.panels->key(event);
        app_detail::rest_egl_current(app);
    }
}

void BarPerMonitorModule::handle_pointer_move(WaylandState &, MonitorOutput &, double x, double y) {
    pointer_x_ = x;
    pointer_y_ = y;
    if (state.panels)
        state.panels->move(x, y);
}

void BarPerMonitorModule::handle_pointer_release() {
    if (state.panels)
        state.panels->release();
}

bool BarPerMonitorModule::wants_pointing_hand_cursor() const {
    if (!mon_)
        return false;
    wl_surface *focused = mon_->app->pointer.focused_surface;
    if (state.panels && state.panels->owns(focused))
        return state.panels->wants_hand(pointer_x_, pointer_y_);
    if (focused != state.surface || !state.model)
        return false;
    double y = pointer_y_ - (state.autohide.enabled ? bar_style_of(*mon_).top_margin : 0);
    return state.model->clickable(pointer_x_, y);
}
