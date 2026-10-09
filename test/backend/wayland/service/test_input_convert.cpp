#include <cassert>

#include "service/wayland/input_service.h"

void test_input_convert() {
    namespace in = astralia::input;

    KeyEvent key{KeyKind::Tab, "", true, false, true, XKB_KEY_Tab};
    in::KeyEvent neutral = to_neutral(key);
    assert(neutral.kind == in::KeyKind::Tab && neutral.shift && !neutral.alt && neutral.ctrl && neutral.base_sym == XKB_KEY_Tab);

    KeyEvent text{KeyKind::Text, "ñ"};
    assert(to_neutral(text).kind == in::KeyKind::Text && to_neutral(text).text == "ñ");
    assert(to_neutral(KeyEvent{KeyKind::Preedit, "a"}).kind == in::KeyKind::Preedit);
    assert(to_neutral(KeyEvent{KeyKind::Backspace, ""}).kind == in::KeyKind::Backspace);

    PointerClick right{nullptr, true, BTN_RIGHT, 3.5, 4.5, 9};
    in::PointerEvent pointer = to_neutral(right);
    assert(pointer.button == in::Button::Right && pointer.pressed && pointer.x == 3.5 && pointer.y == 4.5);
    assert(to_neutral(PointerClick{nullptr, false, BTN_LEFT}).button == in::Button::Left);
    assert(to_neutral(PointerClick{nullptr, false, BTN_MIDDLE}).button == in::Button::Middle);
    assert(to_neutral(PointerClick{nullptr, false, BTN_SIDE}).button == in::Button::Other);

    assert(to_neutral(PointerScroll{nullptr, -2.5}).dy == -2.5);
}
