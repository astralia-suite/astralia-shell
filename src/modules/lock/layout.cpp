#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "config/lock_config.h"

#include "modules/lock/layout.h"

namespace astralia::lock {

float icon_box_size() { return lock_config::kFontIcon + lock_config::kIconBoxMargin * 4.0f; }

float card_height(float output_h) { return output_h * lock_config::kCardHeightMult; }

float card_width(float output_h) { return card_height(output_h) * lock_config::kCardRatio; }

float center_scale(float output_h) {
    float s = output_h / lock_config::kCenterRefHeight;
    return s < 1.0f ? s : 1.0f;
}

void columns(float card_w, float card_h, float center_w, Rect &left, Rect &center, Rect &right) {
    float inner_x = lock_config::kPanelGap;
    float inner_y = lock_config::kPanelGap;
    float inner_h = card_h - 2.0f * lock_config::kPanelGap;
    float avail = card_w - 4.0f * lock_config::kPanelGap - center_w;
    float side_w = std::max(0.0f, avail * 0.5f);

    left = {inner_x, inner_y, side_w, inner_h};
    center = {inner_x + side_w + lock_config::kPanelGap, inner_y, center_w, inner_h};
    right = {center.x + center_w + lock_config::kPanelGap, inner_y, side_w, inner_h};
}

float side_card_height(float column_h) {
    return std::max(0.0f, (column_h - lock_config::kPanelGap) * 0.5f);
}

float content_height(float clock_h, float date_h, float message_h) {
    return clock_h + lock_config::kGapClockDate + date_h + lock_config::kGapDateAvatar +
           lock_config::kProfileSize + lock_config::kGapAvatarInput + lock_config::kInputHeight +
           lock_config::kGapInputMessage + message_h;
}

int fetch_colour_count(float available_w, int max_count) {
    int n = 0;
    if (available_w >= lock_config::kFetchColorBox)
        n = 1 + static_cast<int>((available_w - lock_config::kFetchColorBox) /
                                 (lock_config::kFetchColorBox + lock_config::kFetchColorGap));
    return std::clamp(n, 0, max_count);
}

float dot_row_width(int count) {
    return static_cast<float>(std::max(count, 0)) * lock_config::kDotSize;
}

float dot_x(int index, int count, float field_width) {
    float row = dot_row_width(count);
    return (field_width - row) * 0.5f + static_cast<float>(index) * lock_config::kDotSize;
}

bool dev_unlock_hit(double x, double y, double output_h) {
    static const bool enabled = [] {
        const char *env = std::getenv("ASTRALIA_DEBUG_UNLOCK");
        return env != nullptr && std::string_view(env) == "1";
    }();
    return enabled && x <= lock_config::kDevUnlockWidth &&
           y >= output_h - lock_config::kDevUnlockHeight;
}

void panel_origin(float output_w, float output_h, float panel_w, float panel_h, float &out_x,
                  float &out_y) {
    out_x = (output_w - panel_w) * 0.5f;
    out_y = (output_h - panel_h) * 0.5f;
}

} // namespace astralia::lock
