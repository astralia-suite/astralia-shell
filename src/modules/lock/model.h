#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "core/animation.h"
#include "core/input.h"

#include "render/geometry.h"
#include "render/text_field.h"

namespace astralia {

enum class LockKey { none,
                     changed,
                     submit };

struct LockHits {
    ui::Box pill_button{};
    ui::Box media_prev{};
    ui::Box media_play{};
    ui::Box media_next{};
};

class LockModel {
  public:
    using Clock = std::chrono::steady_clock;

    LockKey key(const input::KeyEvent &event);
    std::string begin_auth();
    bool finish_auth(uint64_t generation, bool success);
    bool tick(Clock::time_point now);
    Clock::duration until_fail_clear(Clock::time_point now) const;
    void reset();

    const std::string &password() const { return field_.text; }
    bool failed() const { return failed_; }
    bool authenticating() const { return authenticating_; }
    uint64_t generation() const { return generation_; }
    LockHits &hits() { return hits_; }

  private:
    void clear_password();

    TextFieldState field_;
    bool failed_ = false;
    bool authenticating_ = false;
    uint64_t generation_ = 0;
    Clock::time_point fail_clear_at_{};
    LockHits hits_;
};

// One output's entrance, exit and typing animation. The defaults are the settled card,
// which is all X11 ever shows.
struct LockMotion {
    AnimationManager animations;
    float panel_scale = 1.0f;
    float panel_rotation = 0.0f;
    float panel_w = 0.0f;
    float panel_h = 0.0f;
    float icon_alpha = 0.0f;
    float content_alpha = 1.0f;
    float content_scale = 1.0f;
    bool started = false;
    bool unlocking = false;
    TextFieldTypeAnim dots;
    TextFieldRowSlide row;

    LockMotion() = default;
    LockMotion(const LockMotion &) = delete;
    LockMotion &operator=(const LockMotion &) = delete;
};

void lock_motion_fit(LockMotion &motion, float card_w, float card_h);
void lock_motion_exit(LockMotion &motion, std::function<void()> done);
void lock_motion_type(LockMotion &motion, const std::string &password);
void lock_motion_clear_dots(LockMotion &motion);

} // namespace astralia
