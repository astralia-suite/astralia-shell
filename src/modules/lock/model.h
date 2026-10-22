#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "core/animation.h"
#include "core/input.h"

#include "render/geometry.h"
#include "render/text_field.h"

namespace astralia::lock {

enum class Key { none, changed, submit };

struct Hits {
    ui::Box pill_button{};
    ui::Box media_prev{};
    ui::Box media_play{};
    ui::Box media_next{};
};

class Model {
  public:
    using Clock = std::chrono::steady_clock;

    Key key(const input::KeyEvent &event);
    std::string begin_auth();
    bool finish_auth(uint64_t generation, bool success);
    bool tick(Clock::time_point now);
    Clock::duration until_fail_clear(Clock::time_point now) const;
    void reset();

    const std::string &password() const { return field_.text; }
    bool failed() const { return failed_; }
    bool authenticating() const { return authenticating_; }
    uint64_t generation() const { return generation_; }
    Hits &hits() { return hits_; }

  private:
    void clear_password();

    TextFieldState field_;
    bool failed_ = false;
    bool authenticating_ = false;
    uint64_t generation_ = 0;
    Clock::time_point fail_clear_at_{};
    Hits hits_;
};

struct Motion {
    AnimationManager animations;
    float panel_scale = 1.0f;
    float panel_rotation = 0.0f;
    float panel_w = 0.0f;
    float panel_h = 0.0f;
    float icon_alpha = 0.0f;
    float content_alpha = 1.0f;
    float content_scale = 1.0f;
    bool started = false;
    TextFieldTypeAnim dots;
    TextFieldRowSlide row;

    Motion() = default;
    Motion(const Motion &) = delete;
    Motion &operator=(const Motion &) = delete;
};

void motion_fit(Motion &motion, float card_w, float card_h);
void motion_type(Motion &motion, const std::string &password);
void motion_clear_dots(Motion &motion);

} // namespace astralia::lock
