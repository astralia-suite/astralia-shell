#include <malloc.h>

#include "core/pointer.h"

#include "render/tokens.h"

#include "modules/rain/x11/rain.h"

namespace astralia::rain {

namespace {

constexpr std::chrono::milliseconds idle_interval{std::chrono::hours(1)};

}

Rain::Rain(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services),
      window_(x, "Rain",
              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
              false),
      keyboard_(x.conn()), params_(services.settings.config().rain) {
    window_.make_managed(rain_config::default_window_width, rain_config::default_window_height);
    timer_ = loop.add_timer(
        [this] {
            return open_
                       ? std::chrono::milliseconds(static_cast<int>(rain_config::fall_interval_ms))
                       : idle_interval;
        },
        [this] { step(); });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    services.settings.changed.connect([this] { apply_params(services_.settings.config().rain); });
    shell.track("rain", [this] { return open_; });
    shell.bind(ShellVerb::rain, [this] { toggle(); });
}

void Rain::toggle() {
    if (open_) {
        close();
    } else {
        open();
    }
}

void Rain::open() {
    const OutputGeometry &target = active_target_output(services_).geometry;
    int16_t x =
        static_cast<int16_t>(target.x + (target.width - rain_config::default_window_width) / 2);
    int16_t y =
        static_cast<int16_t>(target.y + (target.height - rain_config::default_window_height) / 2);
    window_.place({x, y, rain_config::default_window_width, rain_config::default_window_height});
    keyboard_.reload();
    params_ = services_.settings.config().rain;
    open_ = true;
    rebuild();
    paint();
    window_.show(false);
    loop_.reschedule(timer_);
}

void Rain::close() {
    if (!open_) {
        return;
    }
    open_ = false;
    window_.hide();
    window_.release();
    matrix_ = {};
    stiletto_ = {};
    malloc_trim(0);
}

void Rain::apply_params(const RainParams &params) {
    if (params == params_) {
        return;
    }
    params_ = params;
    if (open_) {
        rebuild();
    }
}

void Rain::rebuild() {
    int width = window_.geometry().width;
    int height = window_.geometry().height;
    if (params_.mode == RainMode::Matrix) {
        stiletto_ = {};
        matrix_.rebuild(width, height, params_.async_speed);
    } else {
        matrix_ = {};
        stiletto_.rebuild(width, height, params_.async_speed);
    }
}

void Rain::step() {
    if (!open_) {
        return;
    }
    if (params_.mode == RainMode::Matrix) {
        matrix_.tick();
    } else {
        stiletto_.tick();
    }
    paint();
}

Frame Rain::frame() const {
    return params_.mode == RainMode::Matrix ? matrix_.frame() : stiletto_.frame();
}

void Rain::handle(const xcb_generic_event_t &event) {
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

void Rain::paint() {
    if (!open_) {
        return;
    }
    cairo_t *cr = window_.cr();
    window_.clear();
    canvas_.bind(cr);
    canvas_.rect({0.0f, 0.0f, static_cast<float>(window_.geometry().width),
                  static_cast<float>(window_.geometry().height)},
                 palette::window_backdrop);
    Frame current = frame();
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

} // namespace astralia::rain
