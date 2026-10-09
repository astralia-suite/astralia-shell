#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <poll.h>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

#include "core/log.h"
#include "core/poll_reactor.h"

namespace astralia {

namespace {

std::chrono::nanoseconds boot_now() {
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return std::chrono::seconds(now.tv_sec) + std::chrono::nanoseconds(now.tv_nsec);
}

std::string errno_message(std::string_view what) {
    return std::format("{}: {}", what, std::strerror(errno));
}

} // namespace

PollReactor::PollReactor(UniqueFd signal_fd, UniqueFd timer_fd)
    : signal_fd_(std::move(signal_fd)), timer_fd_(std::move(timer_fd)) {}

std::expected<PollReactor, std::string> PollReactor::create() {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &signals, nullptr) != 0) {
        return std::unexpected(errno_message("sigprocmask"));
    }
    UniqueFd signal_fd(signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK));
    if (signal_fd.get() < 0) {
        return std::unexpected(errno_message("signalfd"));
    }
    UniqueFd timer_fd(timerfd_create(CLOCK_BOOTTIME, TFD_CLOEXEC | TFD_NONBLOCK));
    if (timer_fd.get() < 0) {
        return std::unexpected(errno_message("timerfd_create"));
    }
    return PollReactor(std::move(signal_fd), std::move(timer_fd));
}

int PollReactor::add_timer(NextFire next_fire, TimerCallback callback) {
    std::chrono::nanoseconds deadline = boot_now() + next_fire();
    timers_.push_back({std::move(next_fire), std::move(callback), deadline});
    arm_timer();
    return static_cast<int>(timers_.size()) - 1;
}

void PollReactor::reschedule(int timer) {
    Timer &t = timers_.at(static_cast<std::size_t>(timer));
    t.deadline = boot_now() + t.next_fire();
    arm_timer();
}

void PollReactor::on_fd(int fd, FdHandler handler) {
    fd_handlers_.insert_or_assign(fd, std::move(handler));
}

void PollReactor::remove_fd(int fd) { fd_handlers_.erase(fd); }

int PollReactor::add_poll_source(PollPrepare prepare, PollDispatch dispatch) {
    int id = next_poll_source_++;
    poll_sources_.push_back({id, std::move(prepare), std::move(dispatch)});
    return id;
}

void PollReactor::remove_poll_source(int id) {
    std::erase_if(poll_sources_, [id](const PollSource &source) { return source.id == id; });
}

void PollReactor::stop(int exit_code) {
    if (!exit_code_) {
        exit_code_ = exit_code;
    }
}

int PollReactor::run() {
    while (!exit_code_) {
        if (!run_once(-1)) {
            return EXIT_FAILURE;
        }
    }
    return *exit_code_;
}

bool PollReactor::run_once(int timeout_ms) {
    std::vector<pollfd> fds{
        {signal_fd_.get(), POLLIN, 0},
        {timer_fd_.get(), POLLIN, 0},
    };
    for (const auto &[fd, handler] : fd_handlers_) {
        fds.push_back({fd, POLLIN, 0});
    }
    std::size_t handlers_end = fds.size();
    int timeout = timeout_ms;
    std::vector<PolledRange> polled;
    for (const PollSource &source : poll_sources_) {
        std::size_t start = fds.size();
        int source_timeout = source.prepare(fds);
        polled.push_back({source.id, start, fds.size() - start});
        if (source_timeout >= 0 && (timeout < 0 || source_timeout < timeout)) {
            timeout = source_timeout;
        }
    }
    if (poll(fds.data(), fds.size(), timeout) < 0) {
        if (errno != EINTR) {
            log::error("{}", errno_message("poll"));
            return false;
        }
        for (pollfd &fd : fds) {
            fd.revents = 0;
        }
    }
    if (fds[0].revents & POLLIN) {
        read_signal();
    }
    if (fds[1].revents & POLLIN) {
        fire_timers();
    }
    for (std::size_t i = 2; i < handlers_end && !exit_code_; ++i) {
        if (fds[i].revents == 0) {
            continue;
        }
        if (auto it = fd_handlers_.find(fds[i].fd); it != fd_handlers_.end()) {
            FdHandler handler = it->second;
            handler();
        }
    }
    for (const PolledRange &range : polled) {
        auto it = std::ranges::find(poll_sources_, range.id, &PollSource::id);
        if (it != poll_sources_.end()) {
            PollDispatch dispatch = it->dispatch;
            dispatch(std::span<const pollfd>(fds).subspan(range.start, range.count));
        }
    }
    return true;
}

void PollReactor::read_signal() {
    signalfd_siginfo info{};
    if (read(signal_fd_.get(), &info, sizeof info) == static_cast<ssize_t>(sizeof info)) {
        log::info("received signal {}, exiting", info.ssi_signo);
        stop(EXIT_SUCCESS);
    }
}

void PollReactor::fire_timers() {
    uint64_t expirations = 0;
    if (read(timer_fd_.get(), &expirations, sizeof expirations) < 0 && errno != EAGAIN) {
        log::error("{}", errno_message("timerfd read"));
    }
    std::chrono::nanoseconds now = boot_now();
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        if (timers_[i].deadline > now) {
            continue;
        }
        timers_[i].callback();
        timers_[i].deadline = boot_now() + timers_[i].next_fire();
    }
    arm_timer();
}

void PollReactor::arm_timer() {
    itimerspec spec{};
    if (!timers_.empty()) {
        std::chrono::nanoseconds earliest =
            std::ranges::min_element(timers_, {}, &Timer::deadline)->deadline;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(earliest);
        spec.it_value.tv_sec = seconds.count();
        spec.it_value.tv_nsec = (earliest - seconds).count();
    }
    if (timerfd_settime(timer_fd_.get(), TFD_TIMER_ABSTIME, &spec, nullptr) != 0) {
        log::error("{}", errno_message("timerfd_settime"));
    }
}

} // namespace astralia
