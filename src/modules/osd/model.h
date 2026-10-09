#pragma once

#include <chrono>
#include <string>

#include "core/animation.h"

namespace astralia {

enum class OsdKind { volume,
                     mic,
                     brightness };

class OsdModel {
  public:
    using Clock = std::chrono::steady_clock;

    explicit OsdModel(Clock::time_point created = Clock::now());

    bool show(OsdKind kind, int percent, bool muted, Clock::time_point now);
    void hide();
    void tick(Clock::time_point now);

    OsdKind kind() const { return kind_; }
    int percent() const { return percent_; }
    bool muted() const { return muted_; }
    bool visible() const { return visible_; }
    bool animating() const { return animations_.hasActive(); }
    float opacity() const { return opacity_; }
    float bar_fill() const { return bar_fill_; }
    float icon_mix() const { return icon_mix_; }
    Clock::time_point hide_at() const { return hide_at_; }
    std::chrono::milliseconds until_hide(Clock::time_point now) const;

    const char *glyph() const;
    std::string label() const;

  private:
    OsdKind kind_ = OsdKind::volume;
    int percent_ = 0;
    bool muted_ = false;
    bool visible_ = false;
    bool hiding_ = false;
    float opacity_ = 0.0f;
    float bar_fill_ = 0.0f;
    float icon_mix_ = 0.0f;
    Clock::time_point created_;
    Clock::time_point hide_at_;
    AnimationManager animations_;
};

} // namespace astralia
