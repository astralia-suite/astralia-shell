#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <unordered_map>
#include <xcb/xcb.h>

#include "core/poll_reactor.h"
#include "core/reactor.h"

#include "core/x_connection.h"

namespace astralia {

class EventLoop final : public Reactor {
  public:
    using EventHandler = std::function<void(const xcb_generic_event_t &)>;

    static std::expected<EventLoop, std::string> create(XConnection &x);

    void on_window(xcb_window_t window, EventHandler handler);
    void on_event(uint8_t type, EventHandler handler);

    int add_timer(NextFire next_fire, TimerCallback callback) override { return reactor_.add_timer(std::move(next_fire), std::move(callback)); }
    void reschedule(int timer) override { reactor_.reschedule(timer); }
    void on_fd(int fd, FdHandler handler) override { reactor_.on_fd(fd, std::move(handler)); }
    void remove_fd(int fd) override { reactor_.remove_fd(fd); }
    int add_poll_source(PollPrepare prepare, PollDispatch dispatch) override { return reactor_.add_poll_source(std::move(prepare), std::move(dispatch)); }
    void remove_poll_source(int id) override { reactor_.remove_poll_source(id); }
    void stop(int exit_code) override { reactor_.stop(exit_code); }
    bool stopping() const override { return reactor_.stopping(); }

    int run();

  private:
    EventLoop(XConnection &x, PollReactor reactor);

    void dispatch(const xcb_generic_event_t &event);

    XConnection *x_;
    PollReactor reactor_;
    std::unordered_map<xcb_window_t, EventHandler> handlers_;
    std::unordered_map<uint8_t, EventHandler> type_handlers_;
};

} // namespace astralia
