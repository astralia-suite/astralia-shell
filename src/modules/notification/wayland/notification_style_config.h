#pragma once

#include "config/notification_config.h"

namespace astralia::notification {

constexpr int kSurfaceWidth = static_cast<int>(astralia::notification_config::card_width);
constexpr int kSurfaceHeight = static_cast<int>(astralia::notification_config::max_stack_height);
constexpr int kMarginRight = astralia::notification_config::margin_right;
constexpr int kMarginBottom = astralia::notification_config::margin_bottom;

} // namespace astralia::notification
