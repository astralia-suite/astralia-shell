#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "core/reactor.h"

namespace astralia {

class DeferredCall {
  public:
    static void init();
    static void attach(Reactor &reactor);
    static void call_later(std::function<void()> fn);
    static void drain();
    static int poll_fd();

  private:
    static int &read_fd();
    static int &write_fd();
    static std::mutex &mutex();
    static std::vector<std::function<void()>> &pending();
};

} // namespace astralia
