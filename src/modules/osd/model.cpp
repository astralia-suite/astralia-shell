#include <algorithm>
#include <format>

#include "config/osd_config.h"
#include "render/icons.h"

#include "modules/osd/model.h"

#include "render/glyphs.h"

namespace astralia {

namespace {

namespace cfg = osd_config;

constexpr uint64_t owner_opacity = 1;
constexpr uint64_t owner_bar_fill = 2;
constexpr uint64_t owner_icon_mix = 3;

} // namespace

OsdModel::OsdModel(Clock::time_point created) : created_(created) {}

bool OsdModel::show(OsdKind kind, int percent, bool muted, Clock::time_point now) {
    if (now - created_ < cfg::ready_delay) {
        return false;
    }
    kind_ = kind;
    percent_ = std::clamp(percent, 0, 100);
    muted_ = muted;
    visible_ = true;
    hiding_ = false;
    hide_at_ = now + cfg::visible_for;
    animations_.animate(opacity_, 1.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic, [this](float v) { opacity_ = v; }, {}, owner_opacity);
    animations_.animate(bar_fill_, static_cast<float>(percent_) / 100.0f, cfg::anim_fast_ms, astralia::Easing::EaseOutCubic, [this](float v) { bar_fill_ = v; }, {}, owner_bar_fill);
    animations_.animate(icon_mix_, muted ? 1.0f : 0.0f, cfg::anim_fast_ms, astralia::Easing::Linear, [this](float v) { icon_mix_ = v; }, {}, owner_icon_mix);
    return true;
}

void OsdModel::hide() {
    if (hiding_) {
        return;
    }
    hiding_ = true;
    animations_.animate(opacity_, 0.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic, [this](float v) { opacity_ = v; }, [this] {
            visible_ = false;
            hiding_ = false; }, owner_opacity);
}

void OsdModel::tick(Clock::time_point now) {
    animations_.tick(now);
    if (visible_ && now >= hide_at_) {
        hide();
    }
}

std::chrono::milliseconds OsdModel::until_hide(Clock::time_point now) const {
    if (!visible_) {
        return std::chrono::hours(1);
    }
    return std::max(std::chrono::ceil<std::chrono::milliseconds>(hide_at_ - now), std::chrono::milliseconds(0));
}

const char *OsdModel::glyph() const {
    switch (kind_) {
    case OsdKind::brightness:
        return icon::brightness_threshold(percent_);
    case OsdKind::mic:
        return muted_ ? icon::mic_off : icon::mic_on;
    case OsdKind::volume:
        return icon::volume_threshold(muted_, percent_);
    }
    return icon::volume_high;
}

std::string OsdModel::label() const {
    return muted_ ? std::string("muted") : std::format("{}%", percent_);
}

} // namespace astralia
