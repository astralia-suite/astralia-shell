#include "core/pointer.h"

namespace astralia {

input::PointerEvent pointer_event(int16_t x, int16_t y, xcb_button_t detail, bool pressed) {
    input::Button button = input::Button::Other;
    switch (detail) {
    case XCB_BUTTON_INDEX_1:
        button = input::Button::Left;
        break;
    case XCB_BUTTON_INDEX_2:
        button = input::Button::Middle;
        break;
    case XCB_BUTTON_INDEX_3:
        button = input::Button::Right;
        break;
    default:
        break;
    }
    return {static_cast<double>(x), static_cast<double>(y), button, pressed};
}

std::optional<input::ScrollEvent> scroll_event(xcb_button_t detail) {
    if (detail == XCB_BUTTON_INDEX_4) {
        return input::ScrollEvent{-1.0};
    }
    if (detail == XCB_BUTTON_INDEX_5) {
        return input::ScrollEvent{1.0};
    }
    return std::nullopt;
}

input::KeyEvent to_neutral(const KeyEvent &event) {
    input::KeyEvent out;
    out.text = event.text;
    out.shift = event.shift;
    out.alt = event.alt;
    out.ctrl = event.ctrl;
    out.base_sym = event.base_sym;
    switch (event.kind) {
    case KeyKind::none:
    case KeyKind::text:
        out.kind = input::KeyKind::Text;
        break;
    case KeyKind::backspace:
        out.kind = input::KeyKind::Backspace;
        break;
    case KeyKind::up:
        out.kind = input::KeyKind::Up;
        break;
    case KeyKind::down:
        out.kind = input::KeyKind::Down;
        break;
    case KeyKind::left:
        out.kind = input::KeyKind::Left;
        break;
    case KeyKind::right:
        out.kind = input::KeyKind::Right;
        break;
    case KeyKind::enter:
        out.kind = input::KeyKind::Enter;
        break;
    case KeyKind::escape:
        out.kind = input::KeyKind::Escape;
        break;
    case KeyKind::tab:
        out.kind = input::KeyKind::Tab;
        break;
    }
    return out;
}

} // namespace astralia
