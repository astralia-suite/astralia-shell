#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <malloc.h>
#include <xcb/xcb_ewmh.h>

#include "config/bar_config.h"
#include "config/bar_layout.h"

#include "modules/bar/panel/panel_set.h"
#include "modules/bar/view.h"
#include "modules/bar/x11/bar.h"
#include "modules/bar/x11/frame.h"

#include "render/app_fonts.h"
#include "render/draw.h"

namespace astralia {

namespace {

namespace layout_cfg = bar_layout;

constexpr int bar_workspace_slots = 10;

std::optional<PanelId> panel_for(BarItem item) {
    switch (item) {
    case BarItem::tray:
        return PanelId::tray;
    case BarItem::network:
        return PanelId::network;
    case BarItem::bluetooth:
        return PanelId::bluetooth;
    case BarItem::volume:
        return PanelId::volume;
    case BarItem::brightness:
        return PanelId::brightness;
    case BarItem::battery:
        return PanelId::battery;
    case BarItem::media:
        return PanelId::media;
    case BarItem::clock:
        return PanelId::clock;
    case BarItem::logout:
    case BarItem::resource:
    case BarItem::count:
        break;
    }
    return std::nullopt;
}

std::optional<BarItem> item_for(PanelId panel) {
    for (size_t i = 0; i < bar_item_count; ++i) {
        if (panel_for(static_cast<BarItem>(i)) == panel) {
            return static_cast<BarItem>(i);
        }
    }
    return std::nullopt;
}

BarSources make_sources(Services &services, const std::string &output) {
    BarSources sources;
    sources.audio = &services.audio;
    sources.battery = &services.battery;
    sources.bluetooth = &services.bluetooth;
    sources.network = &services.network;
    sources.brightness = &services.brightness;
    sources.compositor = [&services]() -> const CompositorState & { return services.compositor->state(); };
    sources.output = output;
    sources.workspace_slots = bar_workspace_slots;
    return sources;
}

} // namespace

Bar::Bar(XConnection &x, EventLoop &loop, Shell &shell, Services &services, const Output &output)
    : x_(x), loop_(loop), shell_(shell), services_(services), output_name_(output.name),
      window_(x, "astralia-shell", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW, false),
      model_(make_sources(services, output.name), BarModel::Clock::now()) {
    register_app_fonts();
    refresh_style();
    panels_ = std::make_unique<PanelHost>(x_, loop, services_);
    panels_->changed.connect([this] { sync_panels(); });
    apply_output(output.geometry);
    refresh_clock();

    auto redraw = [this] { draw(); };
    services_.compositor->active_changed.connect(redraw);
    services_.compositor->structure_changed.connect(redraw);
    services_.bluetooth.changed.connect(redraw);
    services_.network.changed.connect(redraw);
    services_.audio.changed.connect([this](AudioKind kind) {
        if (kind == AudioKind::sink) {
            draw();
        }
    });
    services_.battery.changed.connect(redraw);
    services_.brightness.changed.connect(redraw);
    draw();

    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) {
        switch (event.response_type & ~0x80) {
        case XCB_EXPOSE: {
            const auto &expose = reinterpret_cast<const xcb_expose_event_t &>(event);
            window_.present(expose.x, expose.y, expose.width, expose.height);
            break;
        }
        case XCB_BUTTON_PRESS:
            click(reinterpret_cast<const xcb_button_press_event_t &>(event));
            break;
        case XCB_MOTION_NOTIFY: {
            const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
            hover(motion.event_x, motion.event_y);
            break;
        }
        case XCB_LEAVE_NOTIFY:
            if (reinterpret_cast<const xcb_leave_notify_event_t &>(event).mode == XCB_NOTIFY_MODE_NORMAL) {
                hover(std::nullopt, std::nullopt);
            }
            break;
        default:
            break;
        }
    });
    loop.add_timer([] { return ms_until_next_second(std::chrono::system_clock::now()); }, [this] { redraw_clock(); });
    loop.add_timer([] { return bar_config::trim_interval; }, [] { malloc_trim(0); });
    linger_timer_ = loop.add_timer([this] { return until_linger_end(); }, [this] { sync_hover(); });

    window_.show(false);
}

Bar::~Bar() = default;

bool Bar::refresh_style() {
    BarStyle next = bar_style_resolve(services_.settings.config().bar_style, false);
    if (spec_ != nullptr && next == style_) {
        return false;
    }
    style_ = next;
    spec_ = &bar_style_spec(next, false);
    model_.set_style(*spec_);
    height_ = static_cast<uint16_t>(spec_->top_margin + bar_layout::height);
    return true;
}

void Bar::apply_output(const OutputGeometry &output) {
    width_ = output.width;
    window_.place({output.x, output.y, width_, height_});
    panels_->set_output(output, height_ + bar_layout::panel_gap);
    set_hints(output);
}

void Bar::place(const Output &output) {
    bool restyled = refresh_style();
    if (!restyled && window_.mapped() && window_.geometry() == OutputGeometry{output.geometry.x, output.geometry.y, output.geometry.width, height_}) {
        return;
    }
    panels_->panels().close_all();
    apply_output(output.geometry);
    draw();
    window_.show(false);
}

void Bar::hide() {
    panels_->panels().close_all();
    hover(std::nullopt, std::nullopt);
    window_.hide();
}

void Bar::set_hints(const OutputGeometry &output) {
    xcb_ewmh_connection_t *ewmh = x_.ewmh();
    xcb_ewmh_set_wm_window_type(ewmh, window_.id(), 1, &ewmh->_NET_WM_WINDOW_TYPE_DOCK);
    std::array<xcb_atom_t, 2> states{ewmh->_NET_WM_STATE_STICKY, ewmh->_NET_WM_STATE_ABOVE};
    xcb_ewmh_set_wm_state(ewmh, window_.id(), states.size(), states.data());
    xcb_ewmh_set_wm_desktop(ewmh, window_.id(), 0xFFFFFFFF);

    uint32_t top = output.y + height_;
    xcb_ewmh_wm_strut_partial_t strut{};
    strut.top = top;
    strut.top_start_x = output.x;
    strut.top_end_x = output.x + width_ - 1;
    xcb_ewmh_set_wm_strut_partial(ewmh, window_.id(), strut);
    xcb_ewmh_set_wm_strut(ewmh, window_.id(), 0, 0, top, 0);
}

void Bar::refresh_clock() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[64]{};
    std::size_t length = std::strftime(buffer, sizeof buffer, bar_layout::clock_format, &local);
    clock_label_.assign(buffer, length);
    model_.set_clock_label(clock_label_);
}

void Bar::prepare() {
    auto now = BarModel::Clock::now();
    float width = static_cast<float>(width_) - 2.0f * static_cast<float>(spec_->side_margin);
    model_.refresh(now);
    model_.layout(canvas_, width);
    clock_width_ = model_.item(BarItem::clock).box.w;
}

void Bar::draw(const ui::Box *dirty) {
    cairo_t *cr = window_.cr();
    canvas_.bind(cr);
    prepare();
    float width = static_cast<float>(width_) - 2.0f * static_cast<float>(spec_->side_margin);
    float left = static_cast<float>(spec_->side_margin);
    float top = static_cast<float>(spec_->top_margin);
    int clip_x = 0;
    int clip_w = width_;
    if (dirty != nullptr) {
        clip_x = std::max(0, static_cast<int>(std::floor(left + dirty->x)));
        clip_w = std::min(static_cast<int>(width_), static_cast<int>(std::ceil(left + dirty->x + dirty->w))) - clip_x;
    }
    cairo_save(cr);
    if (dirty != nullptr) {
        cairo_rectangle(cr, clip_x, 0.0, clip_w, height_);
        cairo_clip(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_save(cr);
    cairo_translate(cr, left, top);
    paint_bar_frame(cr, *spec_, model_.frame(), width, bar_layout::height);
    cairo_restore(cr);
    canvas_.begin_group({left, top, width, bar_layout::height}, {});
    paint_bar(canvas_, model_, dirty);
    canvas_.end_group();
    cairo_restore(cr);
    if (dirty != nullptr) {
        window_.present(clip_x, 0, clip_w, height_);
    } else {
        window_.present(0, 0, width_, height_);
    }
    if (linger_timer_ >= 0 && model_.until_idle(BarModel::Clock::now()) < std::chrono::hours(1)) {
        loop_.reschedule(linger_timer_);
    }
}

void Bar::redraw_clock() {
    std::string before = clock_label_;
    refresh_clock();
    if (before == clock_label_) {
        return;
    }
    if (model_.item(BarItem::clock).expand == 0.0f) {
        return;
    }
    float previous = clock_width_;
    canvas_.bind(window_.cr());
    prepare();
    if (model_.item(BarItem::clock).box.w != previous) {
        draw();
        return;
    }
    const ui::Box &box = model_.item(BarItem::clock).box;
    float pad = bar_layout::capsule_gap / 2.0f;
    ui::Box dirty{box.x - pad, 0.0f, box.w + 2.0f * pad, bar_layout::height};
    draw(&dirty);
}

void Bar::click(const xcb_button_press_event_t &event) {
    if (event.detail != XCB_BUTTON_INDEX_1) {
        return;
    }
    start_linger();
    double x = event.event_x - spec_->side_margin;
    double y = event.event_y - spec_->top_margin;
    BarAction action = model_.press(x, y);
    switch (action.kind) {
    case BarActionKind::toggle:
        if (std::optional<PanelId> panel = panel_for(action.item)) {
            panels_->panels().toggle(*panel);
        } else if (action.item == BarItem::logout) {
            panels_->panels().close_all();
            shell_.run(ShellVerb::logout);
        }
        break;
    case BarActionKind::overview:
        panels_->panels().close_all();
        shell_.run(ShellVerb::overview);
        break;
    case BarActionKind::workspace:
        panels_->panels().close_all();
        services_.compositor->focus_workspace(action.workspace);
        break;
    case BarActionKind::none:
        panels_->panels().close_all();
        break;
    }
}

void Bar::hover(std::optional<int> x, std::optional<int> y) {
    std::optional<double> lx;
    std::optional<double> ly;
    if (x && y) {
        lx = *x - spec_->side_margin;
        ly = *y - spec_->top_margin;
    }
    if (model_.hover(lx, ly, BarModel::Clock::now())) {
        draw();
    }
}

void Bar::sync_panels() {
    std::optional<BarItem> open;
    if (PanelId id = panels_->panels().active_id(); id != PanelId::count) {
        open = item_for(id);
    }
    if (!open) {
        for (size_t i = 0; i < static_cast<size_t>(PanelId::count); ++i) {
            const Panel *panel = panels_->panels().find(static_cast<PanelId>(i));
            if (panel != nullptr && panel->is_open()) {
                open = item_for(static_cast<PanelId>(i));
                break;
            }
        }
    }
    auto now = BarModel::Clock::now();
    model_.set_open(open, now);
    if (!open) {
        start_linger();
    }
    draw();
}

void Bar::start_linger() {
    loop_.reschedule(linger_timer_);
}

void Bar::sync_hover() {
    auto now = BarModel::Clock::now();
    bool changed = model_.tick(now);
    xcb_query_pointer_reply_t *reply =
        xcb_query_pointer_reply(x_.conn(), xcb_query_pointer(x_.conn(), window_.id()), nullptr);
    if (reply == nullptr) {
        if (changed) {
            draw();
        }
        return;
    }
    bool inside = reply->same_screen && reply->win_x >= 0 && reply->win_y >= 0 && reply->win_x < width_ && reply->win_y < height_;
    int px = reply->win_x;
    int py = reply->win_y;
    free(reply);
    hover(inside ? std::optional<int>(px) : std::nullopt, inside ? std::optional<int>(py) : std::nullopt);
    if (changed) {
        draw();
    }
}

std::chrono::milliseconds Bar::until_linger_end() const {
    return model_.until_idle(BarModel::Clock::now());
}

} // namespace astralia
