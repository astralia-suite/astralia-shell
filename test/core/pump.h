#pragma once

#include <chrono>
#include <functional>

#include "core/poll_reactor.h"

namespace test {

inline bool pump_until(astralia::PollReactor &reactor, const std::function<bool()> &done,
                       std::chrono::milliseconds limit = std::chrono::milliseconds(3000)) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        reactor.run_once(10);
    }
    return true;
}

} // namespace test
