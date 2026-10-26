#pragma once

#include <cstddef>

namespace astralia::bar {

enum class Item : int {
    logout,
    tray,
    resource,
    network,
    bluetooth,
    volume,
    brightness,
    battery,
    media,
    time,
    date,
    active_window,
    notifications,
    count
};

inline constexpr size_t item_count = static_cast<size_t>(Item::count);

} // namespace astralia::bar
