#include <algorithm>
#include <chrono>
#include <malloc.h>

#include "core/pointer.h"

#include "modules/launcher/x11/launcher.h"

namespace astralia {

namespace {

constexpr std::chrono::milliseconds caret_interval{1000};

}

Launcher::Launcher(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services), window_(x, "astralia-launcher", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_KEY_PRESS),
      keyboard_(x.conn()), model_(loop) {
    model_.on_changed = [this] { paint(); };
    model_.on_close_requested = [this] { close(); };
    caret_timer_ = loop.add_timer([this] { return model_.is_open() ? caret_interval : std::chrono::milliseconds(std::chrono::hours(1)); }, [this] {
        if (model_.is_open()) {
            model_.toggle_caret();
            paint();
        } });
    services.outputs.changed.connect([this] {
        if (!model_.is_open()) {
            return;
        }
        const std::vector<Output> &outputs = services_.outputs.outputs();
        if (std::ranges::none_of(outputs, [this](const Output &output) { return output.name == bound_output_; })) {
            close();
        }
    });
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
    const Output &target = active_target_output(services_);
    bound_output_ = target.name;
    window_.place(target.geometry);
    keyboard_.reload();
    model_.open(global);
    paint();
    window_.show(true);
    loop_.reschedule(caret_timer_);
}

void Launcher::close() {
    if (!model_.is_open()) {
        return;
    }
    model_.close();
    window_.set_cursor(CursorShape::arrow);
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
            window_.set_cursor(model_.hovered() >= 0 ? CursorShape::hand : CursorShape::arrow);
            paint();
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (model_.hover(-1) && model_.is_open()) {
            window_.set_cursor(CursorShape::arrow);
            paint();
        }
        break;
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
