#pragma once

#include "render/icons.h"

namespace astralia::icon {

inline const char *volume_threshold(bool muted, int percent) {
    if (muted) {
        return volume_mute;
    }
    if (percent < 1) {
        return volume_empty;
    }
    return percent < 50 ? volume_low : volume_high;
}

inline const char *brightness_threshold(int percent) {
    return percent < 50 ? brightness_down : brightness_up;
}

inline int level_percent(float level) {
    return static_cast<int>(level * 100.0f + 1e-4f);
}

} // namespace astralia::icon
