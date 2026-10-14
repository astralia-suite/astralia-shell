#include <cmath>

#include "check.h"
#include "palette_fixture.h"

#include "modules/bar/style.h"
#include "render/oklab.h"
#include "render/theme.h"

namespace {

bool near(const astralia::Color &a, const astralia::Color &b) {
    return std::fabs(a.r - b.r) < 1.5f / 255.0f && std::fabs(a.g - b.g) < 1.5f / 255.0f && std::fabs(a.b - b.b) < 1.5f / 255.0f && std::fabs(a.a - b.a) < 1.5f / 255.0f;
}

} // namespace

void check_theme() {
    using namespace astralia;
    using test::check;

    test::apply_base_theme();
    Theme base = theme_from_seed(test::base_extraction());
    check(near(base.accent, palette::accent) && near(base.ink, palette::ink) && near(base.paper, palette::paper) && near(base.text_dim, palette::text_dim) && near(base.window_backdrop, palette::window_backdrop), "applying a theme sets every role");

    bool contrast_ok = true;
    for (int hue = 0; hue < 360; hue += 15) {
        Theme t = theme_from_seed({{static_cast<float>(hue), 0.12f, 0.7f}, std::nullopt, true});
        contrast_ok = contrast_ok && contrast_ratio(t.text, t.paper) >= 4.5f && contrast_ratio(t.text, t.surface) >= 4.5f && contrast_ratio(t.text, t.surface_alt) >= 4.5f && contrast_ratio(t.text, t.accent) >= 4.5f && contrast_ratio(t.text, t.accent_alt) >= 4.5f && contrast_ratio(t.text, t.accent_container) >= 4.5f;
        contrast_ok = contrast_ok && contrast_ratio(t.paper, t.ink) >= 4.5f && contrast_ratio(t.text, t.critical_container) >= 4.5f && contrast_ratio(t.text_muted, t.paper) >= 4.5f;
        if (!contrast_ok) {
            check(false, "text contrast holds for every seed hue");
            break;
        }
    }
    check(contrast_ok, "text contrast reaches 4.5 on paper, surfaces and accents for every seed hue");

    Theme paired = theme_from_seed({{200.0f, 0.1f, 0.7f}, Seed{320.0f, 0.1f, 0.7f}, true});
    Theme rotated = theme_from_seed({{200.0f, 0.1f, 0.7f}, std::nullopt, true});
    check(hue_distance(oklch_from_color(paired.accent_alt).h, 320.0f) < 6.0f, "the secondary seed drives accent_alt");
    check(hue_distance(oklch_from_color(rotated.accent_alt).h, 240.0f) < 6.0f, "accent_alt rotates the seed hue by 40 degrees without a secondary");

    Theme other = theme_from_seed({{20.0f, 0.12f, 0.7f}, std::nullopt, true});
    check(!near(other.accent, palette::accent), "a different seed gives a different accent");
    theme_apply(other);
    check(near(other.accent, palette::accent) && near(other.text, palette::text) && near(other.text_alpha20, palette::text_alpha20) && near(other.overlay, palette::overlay), "theme_apply sets the palette to the derived values");
    check(near(bar_style_spec(BarStyle::islands, true).border, other.ink) && near(bar_style_spec(BarStyle::okinami, true).bg, other.paper) && near(bar_style_spec(BarStyle::continuous, true).bg, other.paper), "bar styles follow the live palette");
    test::apply_base_theme();
    check(near(base.accent, palette::accent), "theme_apply restores the base theme");
}
