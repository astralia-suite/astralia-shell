#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <poll.h>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

#include "core/log.h"

#include "core/event_loop.h"

namespace astralia {

namespace {

struct FreeEvent {
    void operator()(xcb_generic_event_t *event) const { std::free(event); }
};

using EventPtr = std::unique_ptr<xcb_generic_event_t, FreeEvent>;

std::optional<xcb_window_t> target_window(const xcb_generic_event_t &event) {
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        return reinterpret_cast<const xcb_expose_event_t &>(event).window;
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        return reinterpret_cast<const xcb_key_press_event_t &>(event).event;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
        return reinterpret_cast<const xcb_focus_in_event_t &>(event).event;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        return reinterpret_cast<const xcb_button_press_event_t &>(event).event;
    case XCB_MOTION_NOTIFY:
        return reinterpret_cast<const xcb_motion_notify_event_t &>(event).event;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        return reinterpret_cast<const xcb_enter_notify_event_t &>(event).event;
    case XCB_CONFIGURE_NOTIFY:
        return reinterpret_cast<const xcb_configure_notify_event_t &>(event).window;
    case XCB_VISIBILITY_NOTIFY:
        return reinterpret_cast<const xcb_visibility_notify_event_t &>(event).window;
    case XCB_PROPERTY_NOTIFY:
        return reinterpret_cast<const xcb_property_notify_event_t &>(event).window;
    default:
        return std::nullopt;
    }
}

} // namespace

EventLoop::EventLoop(XConnection &x, PollReactor reactor) : x_(&x), reactor_(std::move(reactor)) {}

std::expected<EventLoop, std::string> EventLoop::create(XConnection &x) {
    auto reactor = PollReactor::create();
    if (!reactor) {
        return std::unexpected(reactor.error());
    }
    return EventLoop(x, std::move(*reactor));
}

void EventLoop::on_window(xcb_window_t window, EventHandler handler) {
    handlers_.insert_or_assign(window, std::move(handler));
}

void EventLoop::on_event(uint8_t type, EventHandler handler) {
    type_handlers_.insert_or_assign(type, std::move(handler));
}

int EventLoop::run() {
    xcb_connection_t *conn = x_->conn();
    reactor_.add_poll_source(
        [conn](std::vector<pollfd> &fds) {
            fds.push_back({xcb_get_file_descriptor(conn), POLLIN, 0});
            return -1;
        },
        [](std::span<const pollfd>) {});
    while (!reactor_.stopping()) {
        while (EventPtr event = EventPtr(xcb_poll_for_event(conn))) {
            dispatch(*event);
        }
        if (int error = xcb_connection_has_error(conn); error != 0) {
            log::error("lost the X server connection (xcb error {})", error);
            return EXIT_FAILURE;
        }
        if (reactor_.stopping()) {
            break;
        }
        xcb_flush(conn);
        if (!reactor_.run_once(-1)) {
            return EXIT_FAILURE;
        }
    }
    xcb_flush(conn);
    return reactor_.exit_code();
}

void EventLoop::dispatch(const xcb_generic_event_t &event) {
    if (event.response_type == 0) {
        const auto &error = reinterpret_cast<const xcb_generic_error_t &>(event);
        log::error("X error {} on request {}", error.error_code, error.major_code);
        return;
    }
    if (auto it = type_handlers_.find(event.response_type & ~0x80); it != type_handlers_.end()) {
        it->second(event);
        return;
    }
    std::optional<xcb_window_t> window = target_window(event);
    if (!window) {
        return;
    }
    if (auto it = handlers_.find(*window); it != handlers_.end()) {
        it->second(event);
    }
}

} // namespace astralia
