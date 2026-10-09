#include <chrono>
#include <malloc.h>
#include <string>

#include "config/bar_config.h"
#include "config/settings_config.h"

#include "core/log.h"
#include "core/pointer.h"

#include "modules/settings/x11/settings.h"

namespace astralia {

namespace {

namespace cfg = settings_config;

SettingsCaps x11_caps() {
    SettingsCaps caps = settings_caps(x11_capabilities());
    caps.bar_toggle = true;
    caps.autohide = false;
    caps.remember_tab = true;
    caps.bar_styles.assign(bar_config::supported_styles.begin(), bar_config::supported_styles.end());
    return caps;
}

constexpr std::chrono::milliseconds repaint_batch{50};

} // namespace

Settings::Settings(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services),
      window_(x, "astralia-settings", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE),
      keyboard_(x.conn()),
      model_(x11_caps(), SettingsHooks{
                             [&services]() -> const Config & { return services.settings.config(); },
                             [&services](const std::function<void(Config &)> &edit) { services.settings.update(edit); },
                             [&services] {
                                 std::vector<std::string> names;
                                 for (const Output &output : services.outputs.outputs()) {
                                     names.push_back(output.name);
                                 }
                                 return names;
                             },
                             [&services] { return services.outputs.at_pointer().name; },
                             {}}) {
    repaint_timer_ = loop.add_timer([this] { return repaint_pending_ ? repaint_batch : std::chrono::milliseconds(std::chrono::hours(1)); },
                                    [this] {
                                        if (!repaint_pending_) {
                                            return;
                                        }
                                        repaint_pending_ = false;
                                        paint();
                                    });
    model_.on_close_requested = [this] { close(); };
    model_.on_changed = [this] { schedule_paint(); };
    canvas_.on_image_ready = [this] { schedule_paint(); };
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("settings", [this] { return open_; });
    shell.bind(ShellVerb::settings, [this] { toggle(); });

    services.settings.changed.connect([this] {
        refocus_until_ = std::chrono::steady_clock::now() + cfg::relayout_grace;
        if (open_) {
            model_.sync();
            schedule_paint();
        }
    });
    services.outputs.changed.connect([this] {
        if (open_) {
            model_.sync();
            schedule_paint();
        }
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
    window_.place(x_.pointer_output());
    keyboard_.reload();
    open_ = true;
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
    open_ = false;
    repaint_pending_ = false;
    window_.hide();
    canvas_.release_images();
    window_.release();
    malloc_trim(0);
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
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (press.detail == XCB_BUTTON_INDEX_1) {
            model_.click(press.event_x, press.event_y);
        } else if (press.detail == XCB_BUTTON_INDEX_4) {
            model_.scroll(-60.0f);
        } else if (press.detail == XCB_BUTTON_INDEX_5) {
            model_.scroll(60.0f);
        }
        if (open_) {
            paint();
        }
        break;
    }
    case XCB_FOCUS_OUT: {
        const auto &focus = reinterpret_cast<const xcb_focus_out_event_t &>(event);
        if (focus.mode != XCB_NOTIFY_MODE_NORMAL || focus.detail == XCB_NOTIFY_DETAIL_POINTER) {
            break;
        }
        if (std::chrono::steady_clock::now() < refocus_until_) {
            window_.focus();
        } else {
            close();
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
    SettingsArt art;
    art.user_name = services_.user.name();
    art.uptime = services_.user.uptime();
    paint_settings(canvas_, model_, art, static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height));
    window_.present();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    if (elapsed >= std::chrono::milliseconds(16)) {
        log::info("settings: paint in {} ms", elapsed.count());
    }
}

} // namespace astralia
