#pragma once

#include <cstdint>
#include <optional>
#include <xcb/xcb.h>

#include "core/input.h"
#include "core/keyboard.h"

namespace astralia {

input::PointerEvent pointer_event(int16_t x, int16_t y, xcb_button_t detail, bool pressed);

std::optional<input::ScrollEvent> scroll_event(xcb_button_t detail);

input::KeyEvent to_neutral(const KeyEvent &event);

} // namespace astralia
