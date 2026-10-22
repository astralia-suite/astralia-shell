#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

namespace astralia {

enum class Easing : uint8_t {
    Linear,
    EaseOutQuad,
    EaseInOutCubic,
    EaseOutCubic,
    EaseInCubic,
    EaseOutBack,
    EaseInBack,
};

float apply_easing(Easing easing, float t);

void animation_set_instant(bool value);

bool animation_instant();

struct Animation {
    float from = 0.0f;
    float to = 0.0f;
    float duration_ms = 0.0f;
    std::chrono::steady_clock::time_point started_at;
    Easing easing = Easing::EaseOutCubic;
    std::function<void(float)> setter;
    std::function<void()> on_complete;
    bool finished = false;
};

class AnimationManager {
  public:
    using Id = uint32_t;

    Id animate(float from, float to, float duration_ms, Easing easing,
               std::function<void(float)> setter, std::function<void()> on_complete = {},
               uint64_t owner = 0) {
        return start(from, to, duration_ms, easing, std::move(setter), std::move(on_complete),
                     owner);
    }

    void cancel_owner(uint64_t owner);

    void tick(std::chrono::steady_clock::time_point now);

    bool has_active() const { return !entries_.empty(); }

  private:
    struct Entry {
        Id id = 0;
        uint64_t owner = 0;
        Animation anim;
    };

    Id start(float from, float to, float duration_ms, Easing easing,
             std::function<void(float)> setter, std::function<void()> on_complete, uint64_t owner);

    std::vector<Entry> entries_;
    Id next_id_ = 1;
};

} // namespace astralia
