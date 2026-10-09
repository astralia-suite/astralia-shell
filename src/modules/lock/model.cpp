#include <cstring>

#include "config/lock_config.h"

#include "modules/lock/layout.h"
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

// The card spins in as a small lock icon, then expands while its content fades and scales in.
void enter(LockMotion &m, float card_w, float card_h) {
    m.started = true;
    m.unlocking = false;
    float box = lock_icon_box_size();
    m.panel_scale = kLockScaleHidden;
    m.panel_rotation = 0.0f;
    m.panel_w = box;
    m.panel_h = box;
    m.icon_alpha = 1.0f;
    m.content_alpha = 0.0f;
    m.content_scale = kLockScaleHidden;

    AnimationManager &a = m.animations;
    a.animate(kLockScaleHidden, kLockScaleFull, kLockAnimSpinMs, Easing::EaseOutBack, [&m](float v) { m.panel_scale = v; }, {}, kLockOwnerPanelScale);
    a.animate(0.0f, 360.0f, kLockAnimSpinMs, Easing::EaseInOutCubic, [&m](float v) { m.panel_rotation = v; }, [&m, card_w, card_h] {
            m.panel_rotation = 0.0f;
            AnimationManager &b = m.animations;
            b.animate(m.panel_w, card_w, kLockAnimExpandMs, Easing::EaseOutCubic, [&m](float v) { m.panel_w = v; }, {}, kLockOwnerPanelWidth);
            b.animate(m.panel_h, card_h, kLockAnimExpandMs, Easing::EaseOutCubic, [&m](float v) { m.panel_h = v; }, {}, kLockOwnerPanelHeight);
            b.animate(1.0f, 0.0f, kLockAnimIconFadeOutMs, Easing::EaseOutCubic, [&m](float v) { m.icon_alpha = v; }, {}, kLockOwnerIconAlpha);
            b.animate(0.0f, 1.0f, kLockAnimContentFadeInMs, Easing::EaseOutCubic, [&m](float v) { m.content_alpha = v; }, {}, kLockOwnerContentAlpha);
            b.animate(kLockScaleHidden, kLockScaleFull, kLockAnimContentScaleInMs, Easing::EaseOutBack, [&m](float v) { m.content_scale = v; }, {}, kLockOwnerContentScale); }, kLockOwnerPanelRotation);
}

} // namespace

void lock_motion_fit(LockMotion &motion, float card_w, float card_h) {
    if (!motion.started) {
        enter(motion, card_w, card_h);
    } else if (!motion.animations.hasActive() && !motion.unlocking) {
        motion.panel_w = card_w;
        motion.panel_h = card_h;
    }
}

// The reverse of the entrance: shrink to the icon, then spin out; `done` runs once the spin ends.
void lock_motion_exit(LockMotion &motion, std::function<void()> done) {
    LockMotion &m = motion;
    m.unlocking = true;
    float box = lock_icon_box_size();
    AnimationManager &a = m.animations;
    a.animate(m.panel_w, box, kLockAnimShrinkMs, Easing::EaseInCubic, [&m](float v) { m.panel_w = v; }, {}, kLockOwnerPanelWidth);
    a.animate(m.panel_h, box, kLockAnimShrinkMs, Easing::EaseInCubic, [&m](float v) { m.panel_h = v; }, {}, kLockOwnerPanelHeight);
    a.animate(m.icon_alpha, 1.0f, kLockAnimIconFadeInMs, Easing::EaseInCubic, [&m](float v) { m.icon_alpha = v; }, {}, kLockOwnerIconAlpha);
    a.animate(m.content_alpha, 0.0f, kLockAnimContentFadeOutMs, Easing::EaseInCubic, [&m](float v) { m.content_alpha = v; }, {}, kLockOwnerContentAlpha);
    a.animate(m.content_scale, kLockScaleHidden, kLockAnimContentScaleOutMs, Easing::EaseInBack, [&m](float v) { m.content_scale = v; }, {}, kLockOwnerContentScale);
    a.animate(0.0f, 1.0f, kLockAnimShrinkMs, Easing::Linear, [](float) {}, [&m, done = std::move(done)] {
            AnimationManager &b = m.animations;
            b.animate(m.panel_scale, kLockScaleHidden, kLockAnimSpinMs, Easing::EaseInBack, [&m](float v) { m.panel_scale = v; }, {}, kLockOwnerPanelScale);
            b.animate(0.0f, -360.0f, kLockAnimSpinMs, Easing::EaseInOutCubic, [&m](float v) { m.panel_rotation = v; }, done, kLockOwnerPanelRotation); }, kLockOwnerSequence);
}

void lock_motion_type(LockMotion &motion, const std::string &password) {
    text_field_type_anim_sync(motion.dots, motion.animations, kLockOwnerDotBase, password);
}

void lock_motion_clear_dots(LockMotion &motion) {
    text_field_type_anim_clear(motion.dots, motion.animations, kLockOwnerDotBase);
    text_field_row_slide_reset(motion.row, motion.animations, kLockOwnerDotRowX);
}

} // namespace astralia
