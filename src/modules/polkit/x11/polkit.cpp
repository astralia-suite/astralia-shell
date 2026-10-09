#include <cstring>

#include "core/log.h"
#include "core/pointer.h"

#include "modules/polkit/view.h"
#include "modules/polkit/x11/polkit.h"

namespace astralia {

Polkit::Polkit(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), window_(x, "astralia-polkit", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS),
      keyboard_(x.conn()), service_(services.polkit) {
    model_.on_closed = [this] { window_.hide(); };
    service_.changed.connect([this] { sync(); });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
}

void Polkit::sync() {
    bool pending = service_.pending();
    if (pending && !model_.open()) {
        open();
        return;
    }
    if (!pending && model_.open()) {
        model_.sync(false);
        return;
    }
    if (model_.open()) {
        paint();
    }
}

void Polkit::open() {
    window_.place(x_.pointer_output());
    keyboard_.reload();
    model_.sync(true);
    paint();
    window_.show(true);
    log::info("polkit: open");
}

void Polkit::handle(const xcb_generic_event_t &event) {
    if (!model_.open()) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        window_.present();
        break;
    case XCB_KEY_PRESS: {
        const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
        switch (model_.key(to_neutral(keyboard_.press(press.detail, press.state)))) {
        case PolkitKey::changed:
            paint();
            break;
        case PolkitKey::submit: {
            std::string password = model_.take_password();
            service_.respond(password);
            explicit_bzero(password.data(), password.size());
            break;
        }
        case PolkitKey::cancel:
            service_.cancel();
            break;
        case PolkitKey::none:
            break;
        }
        break;
    }
    case XCB_BUTTON_PRESS:
        window_.focus();
        break;
    default:
        break;
    }
}

void Polkit::paint() {
    model_.set_prompt(polkit_prompt(service_));
    const OutputGeometry &geometry = window_.geometry();
    cairo_t *cr = window_.cr();
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    canvas_.bind(cr);
    paint_polkit(canvas_, model_, static_cast<float>(geometry.width), static_cast<float>(geometry.height));
    window_.present();
}

} // namespace astralia
