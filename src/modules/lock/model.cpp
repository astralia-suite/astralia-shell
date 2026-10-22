#include <cstring>

#include "config/lock_config.h"

#include "modules/lock/model.h"

namespace astralia::lock {

Key Model::key(const input::KeyEvent &event) {
    switch (text_field_handle_key(field_, event)) {
    case TextFieldResult::Committed:
        return field_.text.empty() || authenticating_ ? Key::none : Key::submit;
    case TextFieldResult::Cancelled:
        clear_password();
        failed_ = false;
        return Key::changed;
    case TextFieldResult::Changed:
        failed_ = false;
        return Key::changed;
    case TextFieldResult::None:
        break;
    }
    return Key::none;
}

std::string Model::begin_auth() {
    authenticating_ = true;
    failed_ = false;
    ++generation_;
    return field_.text;
}

bool Model::finish_auth(uint64_t generation, bool success) {
    if (generation != generation_) {
        return false;
    }
    authenticating_ = false;
    if (success) {
        return true;
    }
    clear_password();
    failed_ = true;
    fail_clear_at_ =
        Clock::now() + std::chrono::milliseconds(static_cast<int>(lock_config::kTimerFailMs));
    return false;
}

bool Model::tick(Clock::time_point now) {
    if (!failed_ || now < fail_clear_at_) {
        return false;
    }
    failed_ = false;
    return true;
}

Model::Clock::duration Model::until_fail_clear(Clock::time_point now) const {
    return failed_ && fail_clear_at_ > now ? fail_clear_at_ - now : Clock::duration::zero();
}

void Model::reset() {
    clear_password();
    failed_ = false;
    authenticating_ = false;
    ++generation_;
    hits_ = {};
}

void Model::clear_password() {
    explicit_bzero(field_.text.data(), field_.text.size());
    field_.text.clear();
}

namespace {

void enter(Motion &m, float card_w, float card_h) {
    m.started = true;
    m.panel_scale = lock_config::kScaleFull;
    m.panel_rotation = 0.0f;
    m.panel_w = card_w;
    m.panel_h = card_h;
    m.icon_alpha = 0.0f;
    m.content_alpha = 1.0f;
    m.content_scale = lock_config::kScaleFull;
}

} // namespace

void motion_fit(Motion &motion, float card_w, float card_h) {
    if (!motion.started) {
        enter(motion, card_w, card_h);
    } else if (!motion.animations.has_active()) {
        motion.panel_w = card_w;
        motion.panel_h = card_h;
    }
}

void motion_type(Motion &motion, const std::string &password) {
    text_field_type_anim_sync(motion.dots, motion.animations, lock_config::kOwnerDotBase, password);
}

void motion_clear_dots(Motion &motion) {
    text_field_type_anim_clear(motion.dots, motion.animations, lock_config::kOwnerDotBase);
    text_field_row_slide_reset(motion.row, motion.animations, lock_config::kOwnerDotRowX);
}

} // namespace astralia::lock
