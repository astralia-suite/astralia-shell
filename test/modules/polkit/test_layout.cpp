#include "check.h"

#include "modules/polkit/layout.h"

using test::check;

void check_polkit_layout() {
    check(astralia::utf8_length("") == 0, "empty utf8 length");
    check(astralia::utf8_length("pass") == 4, "ascii utf8 length");
    check(astralia::utf8_length("mật") == 3, "multibyte utf8 length");
    check(astralia::polkit_card_height(false) == 183.0, "card height without info");
    check(astralia::polkit_card_height(true) == 213.0, "card height with info");
    check(astralia::polkit_visible_dots(5, 404.0) == 5, "dots fit");
    check(astralia::polkit_visible_dots(40, 404.0) == 25, "dots clamp to width");
    check(astralia::polkit_visible_dots(3, -1.0) == 0, "no width shows no dots");
}
