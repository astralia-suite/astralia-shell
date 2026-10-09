#pragma once

#include <chrono>
#include <functional>
#include <poll.h>
#include <span>
#include <vector>

namespace astralia {

class Reactor {
  public:
    using NextFire = std::function<std::chrono::milliseconds()>;
    using TimerCallback = std::function<void()>;
    using FdHandler = std::function<void()>;
    using PollPrepare = std::function<int(std::vector<pollfd> &)>;
    using PollDispatch = std::function<void(std::span<const pollfd>)>;

    virtual ~Reactor() = default;

    virtual int add_timer(NextFire next_fire, TimerCallback callback) = 0;
    virtual void reschedule(int timer) = 0;
    virtual void on_fd(int fd, FdHandler handler) = 0;
    virtual void remove_fd(int fd) = 0;
    virtual int add_poll_source(PollPrepare prepare, PollDispatch dispatch) = 0;
    virtual void remove_poll_source(int id) = 0;
    virtual void stop(int exit_code) = 0;
    virtual bool stopping() const = 0;
};

} // namespace astralia
