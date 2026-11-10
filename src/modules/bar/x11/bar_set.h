#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

namespace astralia::bar {

class Host;

class HostSet {
  public:
    HostSet(XConnection &x, EventLoop &loop, Shell &shell, Services &services);
    ~HostSet();
    HostSet(const HostSet &) = delete;
    HostSet &operator=(const HostSet &) = delete;

  private:
    struct Entry {
        std::string output;
        std::unique_ptr<Host> bar;
    };

    void sync();

    XConnection &x_;
    EventLoop &loop_;
    Shell &shell_;
    Services &services_;
    std::vector<Entry> bars_;
};

} // namespace astralia::bar
