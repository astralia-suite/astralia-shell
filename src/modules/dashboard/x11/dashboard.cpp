#include <malloc.h>

#include "config/dashboard_config.h"

#include "core/pointer.h"

#include "render/tokens.h"

#include "modules/dashboard/x11/dashboard.h"

namespace astralia {

Dashboard::Dashboard(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), services_(services), window_(x, kDashboardTitle, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY, false), keyboard_(x.conn()) {
    window_.make_managed(kDashboardDefaultWidth, kDashboardDefaultHeight);
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("dashboard", [this] { return open_; });
    shell.bind(ShellVerb::dashboard, [this] { toggle(); });
}

void Dashboard::toggle() {
    if (open_) {
        close();
    } else {
        open();
    }
}

void Dashboard::open() {
    const OutputGeometry &target = active_target_output(services_).geometry;
    int16_t x = static_cast<int16_t>(target.x + (target.width - kDashboardDefaultWidth) / 2);
    int16_t y = static_cast<int16_t>(target.y + (target.height - kDashboardDefaultHeight) / 2);
    window_.place({x, y, kDashboardDefaultWidth, kDashboardDefaultHeight});
    keyboard_.reload();
    open_ = true;
    paint();
    window_.show(false);
}

void Dashboard::close() {
    if (!open_) {
        return;
    }
    open_ = false;
    window_.hide();
    window_.release();
    malloc_trim(0);
}

void Dashboard::handle(const xcb_generic_event_t &event) {
    if (!open_) {
        return;
    }
    if (window_.is_delete_message(event)) {
        close();
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        window_.present();
        break;
    case XCB_CONFIGURE_NOTIFY:
        if (window_.apply_configure(reinterpret_cast<const xcb_configure_notify_event_t &>(event))) {
            paint();
        }
        break;
    case XCB_KEY_PRESS: {
        const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
        if (to_neutral(keyboard_.press(press.detail, press.state)).kind == input::KeyKind::Escape) {
            close();
        }
        break;
    }
    default:
        break;
    }
}

void Dashboard::paint() {
    if (!open_) {
        return;
    }
    cairo_t *cr = window_.cr();
    window_.clear();
    canvas_.bind(cr);
    canvas_.rect({0.0f, 0.0f, static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height)}, palette::window_backdrop);
    window_.present();
}

} // namespace astralia
