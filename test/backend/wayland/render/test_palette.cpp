
#include <cassert>
#include <cmath>

#include "ui/tokens.h"

void test_palette() {
    astralia::Color a{0.0f, 0.2f, 0.4f, 1.0f};
    astralia::Color b{1.0f, 0.6f, 0.8f, 0.0f};

    astralia::Color at0 = astralia::lerp_color(a, b, 0.0f);
    assert(at0.r == a.r && at0.g == a.g && at0.b == a.b && at0.a == a.a);

    astralia::Color at1 = astralia::lerp_color(a, b, 1.0f);
    assert(at1.r == b.r && at1.g == b.g && at1.b == b.b && at1.a == b.a);

    astralia::Color mid = astralia::lerp_color(a, b, 0.5f);
    assert(std::fabs(mid.r - (a.r + b.r) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.g - (a.g + b.g) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.b - (a.b + b.b) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.a - (a.a + b.a) / 2.0f) < 1e-6f);

    astralia::Color c = astralia::with_alpha(a, 0.75f);
    assert(c.r == a.r && c.g == a.g && c.b == a.b && c.a == 0.75f);

    astralia::Color opaque = astralia::color("#9B57F4");
    assert(std::fabs(opaque.r - 155.0f / 255.0f) < 1e-6f);
    assert(std::fabs(opaque.g - 87.0f / 255.0f) < 1e-6f);
    assert(std::fabs(opaque.b - 244.0f / 255.0f) < 1e-6f);
    assert(opaque.a == 1.0f);

    astralia::Color translucent = astralia::color("#9B57F41F");
    assert(translucent.r == opaque.r && translucent.g == opaque.g && translucent.b == opaque.b);
    assert(std::fabs(translucent.a - 31.0f / 255.0f) < 1e-6f);
}
