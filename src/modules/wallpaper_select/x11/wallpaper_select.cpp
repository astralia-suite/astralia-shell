#include <algorithm>
#include <chrono>
#include <malloc.h>
#include <optional>
#include <string>

#include "app/active_target.h"

#include "config/wallpaper_select_config.h"

#include "core/log.h"
#include "core/pointer.h"

#include "modules/wallpaper_select/view.h"
#include "modules/wallpaper_select/x11/wallpaper_select.h"

namespace astralia {

namespace {

namespace cfg = wallpaper_select_config;

constexpr std::chrono::milliseconds repaint_batch{50};

} // namespace

WallpaperSelect::WallpaperSelect(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services),
      window_(x, "astralia-wallpaper-select", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS),
      keyboard_(x.conn()),
      model_(WallpaperSelectHooks{
                 [&services]() -> const Config & { return services.settings.config(); },
                 [&services](const std::function<void(Config &)> &edit) { services.settings.update(edit); }},
             x11_capabilities().animated_wallpaper) {
    repaint_timer_ = loop.add_timer([this] { return repaint_pending_ ? repaint_batch : std::chrono::milliseconds(std::chrono::hours(1)); },
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
    shell.track("wallpaper-select", [this] { return open_; });
    shell.bind(ShellVerb::wallpaper, [this] { toggle(); });

    services.outputs.changed.connect([this] {
        if (!open_) {
            return;
        }
        const std::vector<Output> &outputs = services_.outputs.outputs();
        if (std::ranges::none_of(outputs, [this](const Output &output) { return output.name == bound_output_; })) {
            close();
        }
    });
}

void WallpaperSelect::toggle() {
    if (open_) {
        close();
    } else {
        open_on(active_target_output(services_).name, 0);
    }
}

void WallpaperSelect::open_on(const std::string &monitor, int column) {
    if (open_ || !model_.available()) {
        return;
    }
    const std::vector<Output> &outputs = services_.outputs.outputs();
    auto found = std::ranges::find_if(outputs, [&](const Output &output) { return output.name == monitor; });
    if (found == outputs.end()) {
        return;
    }
    bound_output_ = found->name;
    window_.place(found->geometry);
    keyboard_.reload();
    open_ = true;
    model_.open(found->name, column);
    paint();
    window_.show(true);
    log::info("wallpaper-select: open on {}", found->name);
}

void WallpaperSelect::close() {
    if (!open_) {
        return;
    }
    model_.close();
}

void WallpaperSelect::closed() {
    open_ = false;
    repaint_pending_ = false;
    window_.set_cursor(CursorShape::arrow);
    window_.hide();
    canvas_.release_images();
    window_.release();
    malloc_trim(0);
}

void WallpaperSelect::handle(const xcb_generic_event_t &event) {
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
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        window_.set_cursor(model_.clickable(motion.event_x, motion.event_y) ? CursorShape::hand : CursorShape::arrow);
        break;
    }
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (press.detail == XCB_BUTTON_INDEX_1) {
            model_.click(press.event_x, press.event_y);
        } else if (std::optional<input::ScrollEvent> wheel = scroll_event(press.detail)) {
            model_.scroll(static_cast<float>(wheel->dy) * cfg::wheel_notch);
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

void WallpaperSelect::schedule_paint() {
    if (!open_ || repaint_pending_) {
        return;
    }
    repaint_pending_ = true;
    loop_.reschedule(repaint_timer_);
}

void WallpaperSelect::paint() {
    if (!open_) {
        return;
    }
    repaint_pending_ = false;
    window_.clear();
    canvas_.bind(window_.cr());
    paint_wallpaper_select(canvas_, model_, static_cast<float>(window_.geometry().width), static_cast<float>(window_.geometry().height));
    window_.present();
}

} // namespace astralia
