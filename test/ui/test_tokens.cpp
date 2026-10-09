#include <cmath>

#include "check.h"

#include "ui/glyphs.h"
#include "ui/tokens.h"

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

} // namespace

void check_tokens() {
    constexpr astralia::Color opaque = astralia::color("#9B57F4");
    test::check(near(opaque.r, 155.0f / 255.0f) && near(opaque.g, 87.0f / 255.0f) && near(opaque.b, 244.0f / 255.0f), "hex channels");
    test::check(near(opaque.a, 1.0f), "alpha defaults to one");

    constexpr astralia::Color translucent = astralia::color("F0ECF914");
    test::check(near(translucent.a, 20.0f / 255.0f), "eight digit alpha without a hash");
    test::check(near(astralia::color("#aBcDeF").r, 171.0f / 255.0f), "hex digits are case blind");

    const float *channels = astralia::rgba(astralia::palette::accent);
    test::check(near(channels[0], astralia::palette::accent.r) && near(channels[3], astralia::palette::accent.a), "rgba exposes r g b a in order");

    test::check(astralia::metrics::radius_md > astralia::metrics::radius_sm, "metrics ordered");
}

void check_glyph_thresholds() {
    namespace icon = astralia::icon;
    test::check(icon::volume_threshold(true, 80) == icon::volume_mute, "muted wins");
    test::check(icon::volume_threshold(false, 0) == icon::volume_empty, "zero is empty");
    test::check(icon::volume_threshold(false, 1) == icon::volume_low, "one percent is low");
    test::check(icon::volume_threshold(false, 49) == icon::volume_low, "below half is low");
    test::check(icon::volume_threshold(false, 50) == icon::volume_high, "half is high");
    test::check(icon::brightness_threshold(49) == icon::brightness_down, "dim below half");
    test::check(icon::brightness_threshold(50) == icon::brightness_up, "bright at half");
    test::check(icon::level_percent(0.5f) == 50 && icon::level_percent(0.01f) == 1 && icon::level_percent(0.009f) == 0, "level maps to percent on the thresholds");
}
