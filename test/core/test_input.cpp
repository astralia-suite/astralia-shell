#include "check.h"

#include "core/input.h"

void check_input() {
    using namespace astralia::input;
    KeyEvent key;
    test::check(key.kind == KeyKind::Text && key.text.empty() && !key.shift && !key.alt && !key.ctrl && key.base_sym == 0, "key defaults");
    PointerEvent pointer;
    test::check(pointer.button == Button::Left && !pointer.pressed, "pointer defaults");
    ScrollEvent scroll;
    test::check(scroll.dy == 0, "scroll default");
}
