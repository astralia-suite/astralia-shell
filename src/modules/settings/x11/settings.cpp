#include <algorithm>
#include <chrono>
#include <malloc.h>
#include <optional>
#include <string>

#include "config/bar_config.h"
#include "config/settings_config.h"

#include "core/log.h"
#include "core/pointer.h"

#include "modules/settings/x11/settings.h"

namespace astralia::settings {

namespace {

namespace cfg = settings_config;

Caps x11_caps() {
    Caps caps = settings_caps(x11_capabilities());
    caps.autohide = true;
    caps.wallpaper_columns = true;
    caps.default_wallpaper = true;
    caps.bar_styles.assign(bar_config::supported_styles.begin(),
                           bar_config::supported_styles.end());
    return caps;
}

constexpr std::chrono::milliseconds repaint_batch{50};
constexpr std::chrono::milliseconds caret_interval{1000};
constexpr float wheel_notch = 10.0f;

} // namespace

Settings::Settings(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services),
      window_(x, "astralia-settings",
              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
                  XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS |
                  XCB_EVENT_MASK_FOCUS_CHANGE),
      keyboard_(x.conn()),
      model_(x11_caps(),
             Hooks{[&services]() -> const Config & { return services.settings.config(); },
                   [&services](const std::function<void(Config &)> &edit) {
                       services.settings.update(edit);
                   },
                   [&services] {
                       std::vector<std::string> names;
                       for (const Output &output : services.outputs.outputs()) {
                           names.push_back(output.name);
                       }
                       return names;
                   },
                   [this, &services] {
                       return bound_output_.empty() ? services.outputs.at_pointer().name
                                                    : bound_output_;
                   },
                   {}}) {
    repaint_timer_ = loop.add_timer(
        [this] {
            return repaint_pending_ ? repaint_batch
                                    : std::chrono::milliseconds(std::chrono::hours(1));
        },
        [this] {
            if (!repaint_pending_) {
                return;
            }
            repaint_pending_ = false;
            paint();
        });
    model_.on_close_requested = [this] { close(); };
    model_.on_closed = [this] { closed(); };
    model_.on_changed = [this] { schedule_paint(); };
    canvas_.on_image_ready = [this] { schedule_paint(); };
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    caret_timer_ = loop.add_timer(
        [this] {
            return open_ ? caret_interval : std::chrono::milliseconds(std::chrono::hours(1));
        },
        [this] {
            if (open_) {
                model_.toggle_caret();
                paint();
            }
        });
    shell.track("settings", [this] { return open_; });
    shell.bind(ShellVerb::settings, [this] { toggle(); });

    services.settings.changed.connect([this] {
        expect_focus_loss();
        if (open_) {
            model_.sync();
            schedule_paint();
        }
    });
    services.outputs.changed.connect([this] {
        if (!open_) {
            return;
        }
        const std::vector<Output> &outputs = services_.outputs.outputs();
        if (std::ranges::none_of(
                outputs, [this](const Output &output) { return output.name == bound_output_; })) {
            close();
            return;
        }
        model_.sync();
        schedule_paint();
    });
}

void Settings::toggle() {
    if (open_) {
        close();
    } else {
        open();
    }
}

void Settings::open() {
    const Output &target = active_target_output(services_);
    bound_output_ = target.name;
    window_.place(target.geometry);
    keyboard_.reload();
    open_ = true;
    loop_.reschedule(caret_timer_);
    model_.open();
    paint();
    window_.show(true);
    log::info("settings: open");
}

void Settings::close() {
    if (!open_) {
        return;
    }
    model_.close();
}

void Settings::closed() {
    open_ = false;
    repaint_pending_ = false;
    window_.set_cursor(CursorShape::arrow);
    window_.hide();
    canvas_.release_images();
    window_.release();
    malloc_trim(0);
}

void Settings::expect_focus_loss() {
    refocus_until_ = std::chrono::steady_clock::now() + cfg::relayout_grace;
}

void Settings::handle(const xcb_generic_event_t &event) {
    if (!open_) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        window_.present();
        break;
    case XCB_KEY_PRESS: {
        const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
        if (model_.key(to_neutral(keyboard_.press(press.detail, press.state)))) {
            paint();
        }
        break;
    }
    case XCB_FOCUS_OUT: {
        const auto &focus = reinterpret_cast<const xcb_focus_out_event_t &>(event);
        if (focus.mode == XCB_NOTIFY_MODE_NORMAL && focus.detail != XCB_NOTIFY_DETAIL_POINTER &&
            std::chrono::steady_clock::now() < refocus_until_) {
            window_.focus();
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        window_.set_cursor(model_.clickable(motion.event_x, motion.event_y) ? CursorShape::hand
                                                                            : CursorShape::arrow);
        break;
    }
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (press.detail == XCB_BUTTON_INDEX_1) {
            model_.click(press.event_x, press.event_y);
        } else if (std::optional<input::ScrollEvent> wheel = scroll_event(press.detail)) {
            model_.scroll(static_cast<float>(wheel->dy) * wheel_notch * cfg::scroll_speed);
        }
        if (open_) {
            paint();
        }
        break;
    }
    default:
        break;
    }
}

void Settings::schedule_paint() {
    if (!open_ || repaint_pending_) {
        return;
    }
    repaint_pending_ = true;
    loop_.reschedule(repaint_timer_);
}

void Settings::paint() {
    if (!open_) {
        return;
    }
    repaint_pending_ = false;
    auto start = std::chrono::steady_clock::now();
    cairo_t *cr = window_.cr();
    window_.clear();
    canvas_.bind(cr);
    model_.sync();
    Art art;
    art.user_name = services_.user.name();
    art.uptime = services_.user.uptime();
    art.face = services_.user.face_path();
    settings::paint(canvas_, model_, art, static_cast<float>(window_.geometry().width),
                    static_cast<float>(window_.geometry().height));
    window_.present();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed >= std::chrono::milliseconds(16)) {
        log::info("settings: paint in {} ms", elapsed.count());
    }
}

} // namespace astralia::settings
