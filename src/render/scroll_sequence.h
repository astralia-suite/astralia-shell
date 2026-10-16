#pragma once

#include <cstdint>
#include <functional>

#include "core/animation.h"

namespace astralia {

struct ScrollTimings {
    float slide_ms = 200.0f;
    float scroll_ms = 280.0f;
    float grid_in_ms = 200.0f;
    float grid_out_ms = 320.0f;
};

class ScrollSequence {
  public:
    ScrollSequence(AnimationManager &animations, uint64_t owner_base, ScrollTimings timings = {})
        : animations_(animations), owner_(owner_base), timings_(timings) {}
    ScrollSequence(const ScrollSequence &) = delete;
    ScrollSequence &operator=(const ScrollSequence &) = delete;

    void open();
    void close(std::function<void()> done);

    float slide() const { return slide_; }
    float scroll() const { return scroll_; }
    float grid() const { return grid_; }
    bool settled() const { return scroll_ == 1.0f && slide_ == 0.0f; }
    bool closing() const { return closing_; }

  private:
    AnimationManager &animations_;
    uint64_t owner_;
    ScrollTimings timings_;
    float slide_ = 0.0f;
    float scroll_ = 0.0f;
    float grid_ = 0.0f;
    bool closing_ = false;
};

} // namespace astralia
