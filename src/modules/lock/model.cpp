#include <cstring>

#include "config/lock_config.h"

#include "modules/lock/model.h"

namespace astralia {

LockKey LockModel::key(const input::KeyEvent &event) {
    switch (text_field_handle_key(field_, event)) {
    case TextFieldResult::Committed:
        return field_.text.empty() || authenticating_ ? LockKey::none : LockKey::submit;
    case TextFieldResult::Cancelled:
        clear_password();
        failed_ = false;
        return LockKey::changed;
    case TextFieldResult::Changed:
        failed_ = false;
        return LockKey::changed;
    case TextFieldResult::None:
        break;
    }
    return LockKey::none;
}

std::string LockModel::begin_auth() {
    authenticating_ = true;
    failed_ = false;
    ++generation_;
    return field_.text;
}

bool LockModel::finish_auth(uint64_t generation, bool success) {
    if (generation != generation_) {
        return false;
    }
    authenticating_ = false;
    if (success) {
        return true;
    }
    clear_password();
    failed_ = true;
    fail_clear_at_ = Clock::now() + std::chrono::milliseconds(static_cast<int>(kLockTimerFailMs));
    return false;
}

bool LockModel::tick(Clock::time_point now) {
    if (!failed_ || now < fail_clear_at_) {
        return false;
    }
    failed_ = false;
    return true;
}

LockModel::Clock::duration LockModel::until_fail_clear(Clock::time_point now) const {
    return failed_ && fail_clear_at_ > now ? fail_clear_at_ - now : Clock::duration::zero();
}

void LockModel::reset() {
    clear_password();
    failed_ = false;
    authenticating_ = false;
    ++generation_;
    hits_ = {};
}

void LockModel::clear_password() {
    explicit_bzero(field_.text.data(), field_.text.size());
    field_.text.clear();
}

namespace {

void enter(LockMotion &m, float card_w, float card_h) {
    m.started = true;
    m.panel_scale = kLockScaleFull;
    m.panel_rotation = 0.0f;
    m.panel_w = card_w;
    m.panel_h = card_h;
    m.icon_alpha = 0.0f;
    m.content_alpha = 1.0f;
    m.content_scale = kLockScaleFull;
}

} // namespace

void lock_motion_fit(LockMotion &motion, float card_w, float card_h) {
    if (!motion.started) {
        enter(motion, card_w, card_h);
    } else if (!motion.animations.hasActive()) {
        motion.panel_w = card_w;
        motion.panel_h = card_h;
    }
}

void lock_motion_type(LockMotion &motion, const std::string &password) {
    text_field_type_anim_sync(motion.dots, motion.animations, kLockOwnerDotBase, password);
}

void lock_motion_clear_dots(LockMotion &motion) {
    text_field_type_anim_clear(motion.dots, motion.animations, kLockOwnerDotBase);
    text_field_row_slide_reset(motion.row, motion.animations, kLockOwnerDotRowX);
}

} // namespace astralia
