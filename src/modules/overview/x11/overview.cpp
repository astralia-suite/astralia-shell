#include <malloc.h>

#include "config/overview_config.h"

#include "core/log.h"
#include "core/pointer.h"

#include "modules/overview/view.h"
#include "modules/overview/x11/overview.h"

namespace astralia {

namespace cfg = overview_config;

Overview::Overview(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), services_(services),
      window_(x, "astralia-overview", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE),
      keyboard_(x.conn()), model_(*services.compositor) {
    model_.on_close_requested = [this] { toggle(); };
    model_.on_closed = [this] { closed(); };
    model_.on_compositor_action = [this] { expect_focus_loss(); };
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    services_.compositor->structure_changed.connect([this] { paint(); });
    services_.compositor->active_changed.connect([this] { paint(); });
    shell.track("overview", [this] { return window_.mapped(); });
    shell.bind(ShellVerb::overview, [this] { toggle(); });
}

void Overview::toggle() {
    if (model_.is_open()) {
        model_.close();
    } else {
        open();
    }
}

void Overview::open() {
    const Output &target = services_.outputs.at_pointer();
    window_.place(target.geometry);
    keyboard_.reload();
    services_.compositor->refresh();
    model_.open(target.name, static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height));
    paint();
    window_.show(true);
    log::info("overview: open");
}

void Overview::closed() {
    window_.hide();
    canvas_.release_images();
    window_.release();
    malloc_trim(0);
}

void Overview::expect_focus_loss() {
    focus_grace_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg::focus_grace_ms);
}

void Overview::handle(const xcb_generic_event_t &event) {
    bool open = model_.is_open();
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        if (open) {
            window_.present();
        }
        break;
    case XCB_KEY_PRESS:
        if (open) {
            const auto &pressed = reinterpret_cast<const xcb_key_press_event_t &>(event);
            if (model_.key(to_neutral(keyboard_.press(pressed.detail, pressed.state)))) {
                paint();
            }
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &pressed = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (open && pressed.detail == XCB_BUTTON_INDEX_1) {
            model_.press(pressed.event_x, pressed.event_y);
            paint();
        }
        break;
    }
    case XCB_BUTTON_RELEASE: {
        const auto &released = reinterpret_cast<const xcb_button_release_event_t &>(event);
        if (open && released.detail == XCB_BUTTON_INDEX_1) {
            model_.release();
            paint();
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (open && model_.dragging()) {
            model_.move(motion.event_x, motion.event_y);
            paint();
        }
        break;
    }
    case XCB_FOCUS_OUT: {
        const auto &focus = reinterpret_cast<const xcb_focus_out_event_t &>(event);
        if (!open || focus.mode != XCB_NOTIFY_MODE_NORMAL || focus.detail == XCB_NOTIFY_DETAIL_POINTER) {
            break;
        }
        if (std::chrono::steady_clock::now() < focus_grace_until_) {
            window_.focus();
        } else {
            model_.close();
        }
        break;
    }
    default:
        break;
    }
}

void Overview::paint() {
    if (!model_.is_open()) {
        return;
    }
    cairo_t *cr = window_.cr();
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    model_.sync(static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height));
    canvas_.bind(cr);
    paint_overview(canvas_, model_, {});
    window_.present();
}

} // namespace astralia
