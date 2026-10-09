#include <thread>
#include <vector>

#include "core/deferred_call.h"
#include "core/poll_reactor.h"

#include "check.h"
#include "core/pump.h"

void check_deferred_call() {
    using astralia::DeferredCall;
    using test::check;
    auto created = astralia::PollReactor::create();
    check(created.has_value(), "reactor is created");
    if (!created) {
        return;
    }
    astralia::PollReactor reactor = std::move(*created);
    DeferredCall::attach(reactor);
    check(DeferredCall::poll_fd() >= 0, "attach creates the wake pipe");

    int result = 0;
    std::thread worker([&] { DeferredCall::call_later([&] { result = 42; }); });
    worker.join();
    check(test::pump_until(reactor, [&] { return result == 42; }), "a call from another thread runs on the reactor");

    std::vector<int> order;
    DeferredCall::call_later([&] { order.push_back(1); });
    DeferredCall::call_later([&] { order.push_back(2); });
    DeferredCall::drain();
    check((order == std::vector<int>{1, 2}), "calls run in order");

    int again = 0;
    DeferredCall::drain();
    check(again == 0, "draining nothing is harmless");
}
