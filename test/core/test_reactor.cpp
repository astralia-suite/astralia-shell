#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include "core/poll_reactor.h"

#include "check.h"
#include "core/pump.h"

void check_reactor() {
    using namespace std::chrono_literals;
    using test::check;
    auto created = astralia::PollReactor::create();
    check(created.has_value(), "reactor is created");
    if (!created) {
        return;
    }
    astralia::PollReactor reactor = std::move(*created);

    int ticks = 0;
    reactor.add_timer([] { return 15ms; }, [&] { ++ticks; });
    check(test::pump_until(reactor, [&] { return ticks >= 3; }), "a timer fires repeatedly");

    int fds[2];
    check(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0, "pipe is created");
    int reads = 0;
    reactor.on_fd(fds[0], [&] {
        char buffer[8];
        while (read(fds[0], buffer, sizeof buffer) > 0) {
        }
        ++reads;
    });
    (void)!write(fds[1], "x", 1);
    check(test::pump_until(reactor, [&] { return reads == 1; }), "an fd handler runs when readable");
    reactor.remove_fd(fds[0]);
    (void)!write(fds[1], "x", 1);
    reactor.run_once(30);
    check(reads == 1, "a removed fd handler no longer runs");

    int prepared = 0;
    int dispatched = 0;
    int source = reactor.add_poll_source(
        [&](std::vector<pollfd> &out) {
            ++prepared;
            out.push_back({fds[0], POLLIN, 0});
            return 5;
        },
        [&](std::span<const pollfd> ready) {
            if (!ready.empty() && (ready[0].revents & POLLIN)) {
                ++dispatched;
            }
        });
    check(test::pump_until(reactor, [&] { return dispatched >= 1; }), "a poll source is prepared and dispatched");
    check(prepared >= 1, "prepare runs before every poll");
    reactor.remove_poll_source(source);
    int before = prepared;
    reactor.run_once(20);
    check(prepared == before, "a removed poll source is not prepared again");

    check(!reactor.stopping(), "reactor is running");
    reactor.stop(7);
    reactor.stop(9);
    check(reactor.stopping(), "stop is recorded");
    check(reactor.run() == 7, "the first stop code wins");
    close(fds[0]);
    close(fds[1]);
}
