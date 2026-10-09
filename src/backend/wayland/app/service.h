#pragma once

#include <vector>

#include "wayland/core/poll_source.h"

struct WaylandState;

class Service {
  public:
    virtual ~Service() = default;

    virtual const char *name() const = 0;
    virtual bool init(WaylandState &) { return true; }
    virtual void timer_tick(WaylandState &) {}
    virtual std::vector<FnPollSource> poll_sources(WaylandState &) {
        return {};
    }

    virtual std::vector<PollSource *> raw_poll_sources(WaylandState &) {
        return {};
    }
};
