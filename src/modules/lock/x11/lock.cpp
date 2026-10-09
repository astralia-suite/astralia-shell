#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <malloc.h>
#include <thread>
#include <unistd.h>

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/pointer.h"

#include "config/lock_config.h"

#include "modules/lock/pam_authenticator.h"
#include "modules/lock/x11/lock.h"

#include "service/wallpaper_service.h"

#include "render/cover_cache.h"
#include "render/draw.h"

#ifndef ASTRALIA_PAM_DIR
#define ASTRALIA_PAM_DIR ""
#endif

namespace astralia {

namespace {

constexpr uint32_t window_events = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_VISIBILITY_CHANGE;

std::string clock_text(const char *format, size_t capacity) {
    std::time_t now = std::time(nullptr);
    std::string text(capacity, '\0');
    text.resize(std::strftime(text.data(), capacity, format, std::localtime(&now)));
    return text;
}

std::string date_text() {
    std::string text = clock_text("%a %Y-%m-%d", 64);
    for (char &c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

bool contains(const ui::Box &box, double x, double y) {
    return box.w > 0.0f && x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
}

} // namespace

Lock::Surface::Surface(XConnection &x, size_t index)
    : window(x, "astralia-lock-" + std::to_string(index), window_events) {}

Lock::Lock(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services), keyboard_(x.conn()), user_(services.user.name()) {
    cpu_temp_init(cpu_temp_);
    gpu_temp_init(gpu_temp_, loop);
    timer_ = loop.add_timer([this] { return next_tick(); }, [this] { poll(); });
    shell.track("lock", [this] { return locked_; });
    shell.bind(ShellVerb::lock, [this] { lock(); });
    auto repaint = [this] {
        if (locked_) {
            paint_all(true);
        }
    };
    services.media.changed.connect(repaint);
    services.battery.changed.connect(repaint);
    services.notifications.changed.connect(repaint);
}

std::chrono::milliseconds Lock::next_tick() const {
    if (!locked_) {
        return std::chrono::hours(1);
    }
    auto tick = std::chrono::milliseconds(kLockTickMs);
    auto fail = std::chrono::duration_cast<std::chrono::milliseconds>(model_.until_fail_clear(LockModel::Clock::now()));
    return model_.failed() ? std::min(tick, std::max(fail, std::chrono::milliseconds(1))) : tick;
}

void Lock::lock() {
    if (locked_) {
        return;
    }
    std::vector<Output> outputs = services_.outputs.outputs();
    if (outputs.empty()) {
        outputs.push_back({"", x_.primary_output()});
    }
    while (surfaces_.size() < outputs.size()) {
        size_t index = surfaces_.size();
        surfaces_.push_back(std::make_unique<Surface>(x_, index));
        surfaces_.back()->canvas.on_image_ready = [this] {
            if (locked_) {
                paint_all(true);
            }
        };
        loop_.on_window(surfaces_.back()->window.id(), [this, index](const xcb_generic_event_t &event) { handle(index, event); });
    }
    keyboard_.reload();
    for (size_t i = 0; i < outputs.size(); ++i) {
        surfaces_[i]->output = outputs[i];
        surfaces_[i]->window.place(outputs[i].geometry);
    }
    for (size_t i = outputs.size(); i < surfaces_.size(); ++i) {
        surfaces_[i]->window.hide();
    }
    for (size_t i = 0; i < outputs.size(); ++i) {
        paint(*surfaces_[i], false);
        surfaces_[i]->window.show(i == 0);
    }
    if (!grab()) {
        log::error("lock: could not grab the keyboard, not locking");
        release_grabs();
        for (auto &surface : surfaces_) {
            surface->window.hide();
        }
        return;
    }
    locked_ = true;
    model_.reset();
    cpu_temp_poll(cpu_temp_);
    gpu_temp_poll(gpu_temp_);
    system_stats_poll(stats_);
    paint_all(true);
    loop_.reschedule(timer_);
    log::info("lock: locked {} output(s)", outputs.size());
}

bool Lock::grab() {
    xcb_connection_t *conn = x_.conn();
    xcb_window_t window = surfaces_.front()->window.id();
    bool keyboard = false;
    bool pointer = false;
    for (int attempt = 0; attempt < kLockGrabAttempts && !(keyboard && pointer); ++attempt) {
        if (!keyboard) {
            xcb_grab_keyboard_reply_t *reply = xcb_grab_keyboard_reply(
                conn, xcb_grab_keyboard(conn, 0, window, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), nullptr);
            keyboard = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
            std::free(reply);
        }
        if (!pointer) {
            xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(
                conn, xcb_grab_pointer(conn, 0, window, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), nullptr);
            pointer = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
            std::free(reply);
        }
        if (!(keyboard && pointer)) {
            usleep(kLockGrabRetryMs * 1000);
        }
    }
    if (!pointer) {
        log::error("lock: could not grab the pointer");
    }
    return keyboard && pointer;
}

void Lock::release_grabs() {
    xcb_ungrab_keyboard(x_.conn(), XCB_CURRENT_TIME);
    xcb_ungrab_pointer(x_.conn(), XCB_CURRENT_TIME);
    xcb_flush(x_.conn());
}

void Lock::unlock() {
    release_grabs();
    locked_ = false;
    model_.reset();
    for (auto &surface : surfaces_) {
        surface->window.hide();
        surface->canvas.release_images();
        surface->window.release();
        surface->wallpaper.reset();
        surface->wallpaper_key.clear();
    }
    malloc_trim(0);
    log::info("lock: unlocked");
}

void Lock::handle(size_t index, const xcb_generic_event_t &event) {
    if (!locked_ || index >= surfaces_.size()) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        surfaces_[index]->window.present();
        break;
    case XCB_VISIBILITY_NOTIFY:
        if (reinterpret_cast<const xcb_visibility_notify_event_t &>(event).state != XCB_VISIBILITY_UNOBSCURED) {
            surfaces_[index]->window.show(true);
        }
        break;
    case XCB_KEY_PRESS:
        press(reinterpret_cast<const xcb_key_press_event_t &>(event));
        break;
    case XCB_BUTTON_PRESS:
        click(reinterpret_cast<const xcb_button_press_event_t &>(event));
        break;
    default:
        break;
    }
}

void Lock::press(const xcb_key_press_event_t &event) {
    switch (model_.key(to_neutral(keyboard_.press(event.detail, event.state)))) {
    case LockKey::changed:
        paint_all(true);
        break;
    case LockKey::submit:
        submit();
        break;
    case LockKey::none:
        break;
    }
}

void Lock::click(const xcb_button_press_event_t &event) {
    if (event.detail != XCB_BUTTON_INDEX_1) {
        return;
    }
    for (auto &surface : surfaces_) {
        const OutputGeometry &area = surface->output.geometry;
        if (!surface->window.mapped() || event.root_x < area.x || event.root_y < area.y || event.root_x >= area.x + area.width || event.root_y >= area.y + area.height) {
            continue;
        }
        double x = event.root_x - area.x;
        double y = event.root_y - area.y;
        if (contains(surface->hits.pill_button, x, y)) {
            if (!model_.password().empty() && !model_.authenticating()) {
                submit();
            }
        } else if (contains(surface->hits.media_prev, x, y)) {
            services_.media.previous();
        } else if (contains(surface->hits.media_play, x, y)) {
            services_.media.play_pause();
        } else if (contains(surface->hits.media_next, x, y)) {
            services_.media.next();
        }
        return;
    }
}

void Lock::submit() {
    std::string password = model_.begin_auth();
    uint64_t generation = model_.generation();
    std::thread([this, generation, password = std::move(password)]() mutable {
        pam_auth::Result result = pam_auth::authenticate(user_, password, ASTRALIA_PAM_DIR);
        pam_auth::secure_clear(password);
        DeferredCall::call_later([this, generation, success = result.success] { finished(generation, success); });
    }).detach();
    paint_all(true);
}

void Lock::finished(uint64_t generation, bool success) {
    if (!locked_) {
        return;
    }
    if (model_.finish_auth(generation, success)) {
        unlock();
        return;
    }
    paint_all(true);
    loop_.reschedule(timer_);
}

void Lock::poll() {
    if (!locked_) {
        return;
    }
    model_.tick(LockModel::Clock::now());
    cpu_temp_poll(cpu_temp_);
    gpu_temp_poll(gpu_temp_);
    system_stats_poll(stats_);
    paint_all(true);
}

LockInfo Lock::info() const {
    return {user_,
            services_.user.os_name(),
            services_.compositor->name(),
            services_.user.uptime(),
            clock_text("%H", 8),
            clock_text("%M", 8),
            date_text(),
            &services_.battery.status(),
            &services_.media.status(),
            &stats_,
            &cpu_temp_,
            &gpu_temp_,
            &services_.notifications.list()};
}

void Lock::paint_all(bool with_wallpaper) {
    for (auto &surface : surfaces_) {
        if (surface->window.mapped()) {
            paint(*surface, with_wallpaper);
        }
    }
}

void Lock::paint(Surface &surface, bool with_wallpaper) {
    const OutputGeometry &area = surface.output.geometry;
    cairo_t *cr = surface.window.cr();
    cairo_reset_clip(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_source(cr, palette::base);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (with_wallpaper) {
        std::string path = wallpaper_image_for(services_.settings.config(), surface.output.name);
        std::string key = path + ":" + std::to_string(area.width) + "x" + std::to_string(area.height);
        if (!path.empty() && key != surface.wallpaper_key) {
            auto image = load_cover(path, area.width, area.height);
            surface.wallpaper.reset();
            if (image) {
                surface.wallpaper = std::move(*image);
            } else {
                log::error("lock: wallpaper {}: {}", path, image.error());
            }
            surface.wallpaper_key = key;
        }
        if (surface.wallpaper) {
            cairo_set_source_surface(cr, surface.wallpaper.get(), 0, 0);
            cairo_paint(cr);
        }
        surface.canvas.bind(cr);
        paint_lock(surface.canvas, model_, info(), area.width, area.height);
        surface.hits = model_.hits();
    }
    surface.window.present();
}

} // namespace astralia
