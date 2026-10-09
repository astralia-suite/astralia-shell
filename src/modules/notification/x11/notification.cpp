#include <cmath>

#include "config/notification_config.h"

#include "modules/notification/view.h"
#include "modules/notification/x11/notification.h"

namespace astralia {

namespace {

namespace cfg = notification_config;

} // namespace

Notifications::Notifications(XConnection &x, EventLoop &loop, Services &services)
    : services_(services), window_(x, "astralia-notification", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS),
      service_(services.notifications) {
    service_.changed.connect([this] { sync(); });
    services.outputs.changed.connect([this] { sync(); });
    services.settings.changed.connect([this] { sync(); });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
}

void Notifications::sync() {
    model_.sync(service_);
    const Output &target = services_.outputs.at_pointer();
    if (model_.entries().empty() || !notifications_effective_enabled(services_.settings.config(), target.name)) {
        layout_ = {};
        window_.hide();
        return;
    }
    layout_ = layout_notifications(canvas_, model_, view_, cfg::max_stack_height);
    if (layout_.cards.empty()) {
        window_.hide();
        return;
    }
    auto height = static_cast<int>(std::ceil(layout_.height));
    const OutputGeometry &output = target.geometry;
    window_.place({static_cast<int16_t>(output.x + output.width - cfg::margin_right - static_cast<int>(cfg::card_width)),
                   static_cast<int16_t>(output.y + output.height - cfg::margin_bottom - height),
                   static_cast<uint16_t>(cfg::card_width), static_cast<uint16_t>(height)});
    paint();
    window_.show(false);
}

void Notifications::handle(const xcb_generic_event_t &event) {
    if (!window_.mapped()) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        window_.present();
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (auto id = notification_close_at(layout_, view_, press.event_x, press.event_y)) {
            service_.dismiss(*id);
        }
        break;
    }
    default:
        break;
    }
}

void Notifications::paint() {
    window_.clear();
    canvas_.bind(window_.cr());
    paint_notifications(canvas_, view_, layout_);
    window_.present();
}

} // namespace astralia
