#include <malloc.h>

#include "core/pointer.h"

#include "modules/launcher/x11/launcher.h"

namespace astralia {

Launcher::Launcher(XConnection &x, EventLoop &loop, Shell &shell)
    : x_(x), window_(x, "astralia-launcher", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE),
      keyboard_(x.conn()), model_(loop) {
    model_.on_changed = [this] { paint(); };
    model_.on_close_requested = [this] { close(); };
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("launcher", [this] { return window_.mapped(); });
    shell.bind(ShellVerb::launcher, [this] { toggle(false); });
    shell.bind(ShellVerb::launcher_global, [this] { toggle(true); });
}

void Launcher::toggle(bool global) {
    if (model_.is_open()) {
        close();
    } else {
        open(global);
    }
}

void Launcher::open(bool global) {
    window_.place(x_.pointer_output());
    keyboard_.reload();
    model_.open(global);
    paint();
    window_.show(true);
}

void Launcher::close() {
    if (!model_.is_open()) {
        return;
    }
    model_.close();
    window_.hide();
    frame_ = {};
    malloc_trim(0);
}

int Launcher::row_at(double x, double y) const {
    for (const auto &[box, index] : frame_.rows) {
        if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
            return index;
        }
    }
    return -1;
}

void Launcher::handle(const xcb_generic_event_t &event) {
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        if (model_.is_open()) {
            window_.present();
        }
        break;
    case XCB_KEY_PRESS:
        if (model_.is_open()) {
            const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
            model_.key(to_neutral(keyboard_.press(press.detail, press.state)));
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (!model_.is_open() || press.detail != XCB_BUTTON_INDEX_1) {
            break;
        }
        if (int row = row_at(press.event_x, press.event_y); row >= 0) {
            model_.click_row(row);
        } else if (const ui::Box &box = frame_.box; !(press.event_x >= box.x && press.event_x < box.x + box.w && press.event_y >= box.y && press.event_y < box.y + box.h)) {
            close();
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (model_.hover(row_at(motion.event_x, motion.event_y)) && model_.is_open()) {
            paint();
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (model_.hover(-1) && model_.is_open()) {
            paint();
        }
        break;
    case XCB_FOCUS_OUT: {
        const auto &focus = reinterpret_cast<const xcb_focus_out_event_t &>(event);
        if (model_.is_open() && focus.mode == XCB_NOTIFY_MODE_NORMAL &&
            focus.detail != XCB_NOTIFY_DETAIL_POINTER) {
            close();
        }
        break;
    }
    default:
        break;
    }
}

void Launcher::paint() {
    if (!model_.is_open()) {
        return;
    }
    cairo_t *cr = window_.cr();
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    model_.sync_layout();
    canvas_.bind(cr);
    frame_ = paint_launcher(canvas_, model_, static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height));
    window_.present();
}

} // namespace astralia
