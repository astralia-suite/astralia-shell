#pragma once

#include "config/notification_config.h"

namespace astralia::notification {

constexpr int surface_width = static_cast<int>(astralia::notification_config::card_width);
constexpr int surface_height = static_cast<int>(astralia::notification_config::max_stack_height);
constexpr int margin_right = astralia::notification_config::margin_right;
constexpr int margin_bottom = astralia::notification_config::margin_bottom;

} // namespace astralia::notification
