#include <algorithm>
#include <cmath>
#include <xcb/shape.h>

#include "config/notification_config.h"

#include "modules/notification/view.h"
#include "modules/notification/x11/notification.h"

namespace astralia {

namespace {

namespace cfg = notification_config;

constexpr uint32_t view_events = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW;

std::optional<uint32_t> close_hit(const std::vector<std::pair<uint32_t, ui::Box>> &boxes, double x, double y) {
    for (const auto &[id, box] : boxes) {
        if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
            return id;
        }
    }
    return std::nullopt;
}

} // namespace

Notifications::View::View(XConnection &x) : window(x, "astralia-notification", view_events) {}

Notifications::Notifications(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), loop_(loop), services_(services), service_(services.notifications) {
    service_.changed.connect([this] { sync(); });
    services.outputs.changed.connect([this] { sync(); });
    services.settings.changed.connect([this] { sync(); });
}

Notifications::View &Notifications::view_for(const Output &output) {
    auto it = views_.find(output.name);
    if (it == views_.end()) {
        it = views_.emplace(output.name, std::make_unique<View>(x_)).first;
        View *view = it->second.get();
        loop_.on_window(view->window.id(), [this, view](const xcb_generic_event_t &event) { handle(*view, event); });
    }
    return *it->second;
}

void Notifications::sync() {
    model_.sync(service_);
    const std::vector<Output> &outputs = services_.outputs.outputs();
    for (auto &[name, view] : views_) {
        if (std::ranges::none_of(outputs, [&name](const Output &output) { return output.name == name; })) {
            view->window.hide();
        }
    }
    for (const Output &output : outputs) {
        bool wanted = !model_.entries().empty() && notifications_effective_enabled(services_.settings.config(), output.name);
        auto existing = views_.find(output.name);
        if (!wanted) {
            if (existing != views_.end()) {
                existing->second->layout = {};
                existing->second->window.hide();
            }
            continue;
        }
        View &view = view_for(output);
        view.state.forget_missing(model_.entries());
        view.layout = layout_notifications(view.canvas, model_, view.state, cfg::max_stack_height);
        if (view.layout.cards.empty()) {
            view.window.hide();
            continue;
        }
        auto height = static_cast<int>(std::ceil(view.layout.height));
        const OutputGeometry &area = output.geometry;
        view.window.place({static_cast<int16_t>(area.x + area.width - cfg::margin_right - static_cast<int>(cfg::card_width)),
                           static_cast<int16_t>(area.y + area.height - cfg::margin_bottom - height),
                           static_cast<uint16_t>(cfg::card_width), static_cast<uint16_t>(height)});
        paint(view);
        view.window.show(false);
    }
}

void Notifications::apply_input_region(View &view) {
    std::vector<std::pair<uint32_t, ui::Box>> boxes = notification_close_boxes(view.layout, view.state);
    if (boxes == view.close_boxes) {
        return;
    }
    view.close_boxes = std::move(boxes);
    std::vector<xcb_rectangle_t> rectangles;
    for (const auto &[id, box] : view.close_boxes) {
        rectangles.push_back({static_cast<int16_t>(box.x), static_cast<int16_t>(box.y), static_cast<uint16_t>(std::ceil(box.w)), static_cast<uint16_t>(std::ceil(box.h))});
    }
    xcb_shape_rectangles(x_.conn(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED, view.window.id(), 0, 0, static_cast<uint32_t>(rectangles.size()), rectangles.data());
    xcb_flush(x_.conn());
}

void Notifications::handle(View &view, const xcb_generic_event_t &event) {
    if (!view.window.mapped()) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        view.window.present();
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (press.detail != XCB_BUTTON_INDEX_1) {
            break;
        }
        if (auto id = close_hit(view.close_boxes, press.event_x, press.event_y); id && view.state.hide_locally(*id)) {
            sync();
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (view.state.set_hover(close_hit(view.close_boxes, motion.event_x, motion.event_y))) {
            view.window.set_cursor(view.state.hovered() != 0 ? CursorShape::hand : CursorShape::arrow);
            paint(view);
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (view.state.set_hover(std::nullopt)) {
            view.window.set_cursor(CursorShape::arrow);
            paint(view);
        }
        break;
    default:
        break;
    }
}

void Notifications::paint(View &view) {
    view.window.clear();
    view.canvas.bind(view.window.cr());
    paint_notifications(view.canvas, view.state, view.layout);
    apply_input_region(view);
    view.window.present();
}

} // namespace astralia
