#include <chrono>
#include <fcntl.h>
#include <unistd.h>

#include "core/deferred_call.h"
#include "core/log.h"

namespace astralia {

void DeferredCall::init() {
    if (read_fd() >= 0) {
        return;
    }
    int fds[2];
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        return;
    }
    read_fd() = fds[0];
    write_fd() = fds[1];
}

void DeferredCall::attach(Reactor &reactor) {
    init();
    if (read_fd() >= 0) {
        reactor.on_fd(read_fd(), [] { drain(); });
    }
}

void DeferredCall::call_later(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(mutex());
        pending().push_back(std::move(fn));
    }
    if (write_fd() >= 0) {
        char byte = 0;
        (void)!write(write_fd(), &byte, 1);
    }
}

void DeferredCall::drain() {
    char buf[64];
    while (read_fd() >= 0 && read(read_fd(), buf, sizeof(buf)) > 0) {
    }
    std::vector<std::function<void()>> fns;
    {
        std::lock_guard<std::mutex> lock(mutex());
        fns.swap(pending());
    }
    auto t0 = std::chrono::steady_clock::now();
    for (auto &fn : fns) {
        fn();
    }
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 5.0f) {
        log::info("deferred: drain {} callbacks in {:.1f}ms", fns.size(), ms);
    }
}

int DeferredCall::poll_fd() { return read_fd(); }

int &DeferredCall::read_fd() {
    static int fd = -1;
    return fd;
}

int &DeferredCall::write_fd() {
    static int fd = -1;
    return fd;
}

std::mutex &DeferredCall::mutex() {
    static std::mutex m;
    return m;
}

std::vector<std::function<void()>> &DeferredCall::pending() {
    static std::vector<std::function<void()>> v;
    return v;
}

} // namespace astralia
