#include <malloc.h>

#include "core/pointer.h"

#include "render/tokens.h"

#include "modules/bamboo/x11/bamboo.h"

namespace astralia::bamboo {

namespace {

constexpr std::chrono::milliseconds idle_interval{std::chrono::hours(1)};

}

Bamboo::Bamboo(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services),
      window_(x, "Bamboo",
              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
              false, "astralia-shell-bamboo"),
      keyboard_(x.conn()) {
    window_.make_managed(bamboo_config::kDefaultWindowWidth, bamboo_config::kDefaultWindowHeight);
    timer_ = loop.add_timer(
        [this] {
            return open_
                       ? std::chrono::milliseconds(static_cast<int>(bamboo_config::kTickIntervalMs))
                       : idle_interval;
        },
        [this] { step(); });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("bamboo", [this] { return open_; });
    shell.bind(ShellVerb::bamboo, [this] { toggle(); });
}

void Bamboo::toggle() {
    if (open_) {
        close();
    } else {
        open();
    }
}

void Bamboo::open() {
    const OutputGeometry &target = active_target_output(services_).geometry;
    int16_t x =
        static_cast<int16_t>(target.x + (target.width - bamboo_config::kDefaultWindowWidth) / 2);
    int16_t y =
        static_cast<int16_t>(target.y + (target.height - bamboo_config::kDefaultWindowHeight) / 2);
    window_.place({x, y, bamboo_config::kDefaultWindowWidth, bamboo_config::kDefaultWindowHeight});
    keyboard_.reload();
    open_ = true;
    rebuild();
    paint();
    window_.show(false);
    loop_.reschedule(timer_);
}

void Bamboo::close() {
    if (!open_) {
        return;
    }
    open_ = false;
    window_.hide();
    window_.release();
    grow_ = {};
    malloc_trim(0);
}

void Bamboo::rebuild() { grow_.rebuild(window_.geometry().width, window_.geometry().height); }

void Bamboo::step() {
    if (!open_) {
        return;
    }
    grow_.tick();
    paint();
}

void Bamboo::handle(const xcb_generic_event_t &event) {
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
        if (window_.apply_configure(
                reinterpret_cast<const xcb_configure_notify_event_t &>(event))) {
            rebuild();
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

void Bamboo::paint() {
    if (!open_) {
        return;
    }
    cairo_t *cr = window_.cr();
    window_.clear();
    canvas_.bind(cr);
    canvas_.rect({0.0f, 0.0f, static_cast<float>(window_.geometry().width),
                  static_cast<float>(window_.geometry().height)},
                 palette::window_backdrop);
    Frame current = grow_.frame();
    if (current.ready()) {
        cairo_surface_t *surface = cairo_image_surface_create_for_data(
            const_cast<uint8_t *>(current.pixels), CAIRO_FORMAT_ARGB32, current.width,
            current.height, current.stride);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_surface_destroy(surface);
    }
    window_.present();
}

} // namespace astralia::bamboo
