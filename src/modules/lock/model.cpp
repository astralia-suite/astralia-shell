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

} // namespace astralia
