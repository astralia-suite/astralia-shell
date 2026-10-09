#pragma once

#include <cstdint>
#include <string>

namespace astralia::input {

enum class KeyKind : uint8_t {
    Text,
    Preedit,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Tab,
};

struct KeyEvent {
    KeyKind kind = KeyKind::Text;
    std::string text;
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
    uint32_t base_sym = 0;
};

enum class Button : uint8_t {
    Left,
    Middle,
    Right,
    Other,
};

struct PointerEvent {
    double x = 0;
    double y = 0;
    Button button = Button::Left;
    bool pressed = false;
};

struct ScrollEvent {
    double dy = 0;
};

} // namespace astralia::input
