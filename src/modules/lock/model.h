#pragma once

#include <chrono>
#include <cstdint>
#include <string>

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

} // namespace astralia
