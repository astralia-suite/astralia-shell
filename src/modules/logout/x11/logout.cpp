#include <algorithm>
#include <malloc.h>
#include <string>

#include "core/log.h"
#include "core/pointer.h"
#include "core/spawn.h"

#include "modules/logout/view.h"
#include "modules/logout/x11/logout.h"

namespace astralia::logout {

Logout::Logout(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), services_(services),
      window_(x, "astralia-logout",
              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
                  XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW |
                  XCB_EVENT_MASK_KEY_PRESS),
      keyboard_(x.conn()), model_(logout_commands(true)) {
    model_.on_execute = [](const std::string &command) { spawn_detached(command); };
    model_.on_closed = [this] { closed(); };
    services.outputs.changed.connect([this] {
        if (!model_.open_state()) {
            return;
        }
        const std::vector<Output> &outputs = services_.outputs.outputs();
        if (std::ranges::none_of(
                outputs, [this](const Output &output) { return output.name == bound_output_; })) {
            model_.toggle();
        }
    });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("logout", [this] { return window_.mapped(); });
    shell.bind(ShellVerb::logout, [this] {
        if (model_.open_state()) {
            model_.toggle();
        } else {
            open();
        }
    });
}

void Logout::open() {
    const Output &target = active_target_output(services_);
    bound_output_ = target.name;
    window_.place(target.geometry);
    keyboard_.reload();
    model_.open();
    paint();
    window_.show(true);
    log::info("logout: open");
}

void Logout::closed() {
    window_.set_cursor(CursorShape::arrow);
    window_.hide();
    canvas_.release_images();
    window_.release();
    malloc_trim(0);
}

void Logout::handle(const xcb_generic_event_t &event) {
    const OutputGeometry &geometry = window_.geometry();
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        if (model_.open_state()) {
            window_.present();
        }
        break;
    case XCB_KEY_PRESS:
        if (model_.open_state()) {
            const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
            model_.key(to_neutral(keyboard_.press(press.detail, press.state)));
            paint();
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (model_.open_state() && press.detail == XCB_BUTTON_INDEX_1) {
            model_.click(press.event_x, press.event_y, geometry.width, geometry.height);
            paint();
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (model_.hover(motion.event_x, motion.event_y, geometry.width, geometry.height)) {
            window_.set_cursor(model_.hovered() >= 0 ? CursorShape::hand : CursorShape::arrow);
            paint();
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (model_.clear_hover()) {
            window_.set_cursor(CursorShape::arrow);
            paint();
        }
        break;
    default:
        break;
    }
}

void Logout::paint() {
    if (!model_.open_state()) {
        return;
    }
    cairo_t *cr = window_.cr();
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    canvas_.bind(cr);
    logout::paint(canvas_, model_, static_cast<float>(window_.geometry().width),
                  static_cast<float>(window_.geometry().height),
                  services_.settings.config().logout_logo_path);
    window_.present();
}

} // namespace astralia::logout
