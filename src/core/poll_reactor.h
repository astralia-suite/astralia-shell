#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/reactor.h"
#include "core/unique_fd.h"

namespace astralia {

class PollReactor : public Reactor {
  public:
    static std::expected<PollReactor, std::string> create();

    int add_timer(NextFire next_fire, TimerCallback callback) override;
    void reschedule(int timer) override;
    void on_fd(int fd, FdHandler handler) override;
    void remove_fd(int fd) override;
    int add_poll_source(PollPrepare prepare, PollDispatch dispatch) override;
    void remove_poll_source(int id) override;
    void stop(int exit_code) override;
    bool stopping() const override { return exit_code_.has_value(); }
    int exit_code() const { return exit_code_.value_or(0); }

    int run();
    bool run_once(int timeout_ms);

  private:
    struct Timer {
        NextFire next_fire;
        TimerCallback callback;
        std::chrono::nanoseconds deadline;
    };

    struct PollSource {
        int id;
        PollPrepare prepare;
        PollDispatch dispatch;
    };

    struct PolledRange {
        int id;
        std::size_t start;
        std::size_t count;
    };

    PollReactor(UniqueFd signal_fd, UniqueFd timer_fd);

    void read_signal();
    void fire_timers();
    void arm_timer();

    UniqueFd signal_fd_;
    UniqueFd timer_fd_;
    std::vector<Timer> timers_;
    std::unordered_map<int, FdHandler> fd_handlers_;
    std::vector<PollSource> poll_sources_;
    int next_poll_source_ = 0;
    std::optional<int> exit_code_;
};

} // namespace astralia
