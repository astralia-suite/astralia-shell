#include <cmath>

#include "render/oklab.h"

#include "check.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) < eps; }

} // namespace

void check_oklab() {
    using namespace astralia;
    using test::check;

    Oklab white = oklab_from_color(color("#FFFFFF"));
    check(near(white.L, 1.0f, 1e-3f) && near(white.a, 0.0f, 1e-3f) && near(white.b, 0.0f, 1e-3f),
          "white is L one with no chroma");
    Oklab black = oklab_from_color(color("#000000"));
    check(near(black.L, 0.0f, 1e-3f), "black is L zero");

    Oklch red = oklch_from_color(color("#FF0000"));
    check(near(red.L, 0.628f, 5e-3f) && near(red.C, 0.2577f, 5e-3f) && near(red.h, 29.2f, 0.5f),
          "sRGB red matches the reference OKLCH");

    for (const char *hex : {"#9B57F4", "#DBAA24", "#0A0614", "#40E0D0", "#808080"}) {
        Color c = color(hex);
        Color back = color_from_oklch(oklch_from_color(c));
        check(near(back.r, c.r, 2e-3f) && near(back.g, c.g, 2e-3f) && near(back.b, c.b, 2e-3f),
              "OKLCH round trip keeps the colour");
    }

    Color clipped = color_from_oklch({0.9f, 0.4f, 140.0f});
    check(clipped.r >= 0.0f && clipped.r <= 1.0f && clipped.g >= 0.0f && clipped.g <= 1.0f &&
              clipped.b >= 0.0f && clipped.b <= 1.0f,
          "out of gamut chroma is reduced into range");
    check(near(oklch_from_color(clipped).h, 140.0f, 3.0f), "gamut reduction keeps the hue");

    check(near(contrast_ratio(color("#FFFFFF"), color("#000000")), 21.0f, 0.05f),
          "black on white is 21");
    check(near(contrast_ratio(color("#777777"), color("#777777")), 1.0f, 1e-4f),
          "equal colours give one");
    check(near(contrast_ratio(color("#FFFFFF"), color("#000000")),
               contrast_ratio(color("#000000"), color("#FFFFFF")), 1e-4f),
          "contrast is symmetric");

    check(near(hue_distance(350.0f, 10.0f), 20.0f, 1e-4f), "hue distance wraps around");
    check(near(hue_distance(0.0f, 180.0f), 180.0f, 1e-4f), "opposite hues are 180 apart");
}
