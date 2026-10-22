#include <cmath>

#include "modules/bar/style.h"

#include "render/oklab.h"
#include "render/theme.h"

#include "check.h"
#include "palette_fixture.h"

namespace {

astralia::Color over(const astralia::Color &fg, const astralia::Color &bg) {
    return astralia::lerp_color(bg, fg, fg.a);
}

bool near(const astralia::Color &a, const astralia::Color &b) {
    return std::fabs(a.r - b.r) < 1.5f / 255.0f && std::fabs(a.g - b.g) < 1.5f / 255.0f &&
           std::fabs(a.b - b.b) < 1.5f / 255.0f && std::fabs(a.a - b.a) < 1.5f / 255.0f;
}

} // namespace

void check_theme() {
    using namespace astralia;
    using test::check;

    test::apply_base_theme();
    Theme base = theme_from_seed(test::base_extraction());
    check(near(base.accent, palette::accent) && near(base.ink, palette::ink) &&
              near(base.paper, palette::paper) && near(base.shadow, palette::shadow) &&
              near(base.text_dim, palette::text_dim) &&
              near(base.window_backdrop, palette::window_backdrop),
          "applying a theme sets every role");

    bool contrast_ok = true;
    for (int hue = 0; hue < 360; hue += 15) {
        Theme t = theme_from_seed({{{static_cast<float>(hue), 0.12f, 0.7f}}, true});
        contrast_ok = contrast_ok && contrast_ratio(t.text, t.paper) >= 4.5f &&
                      contrast_ratio(t.text, t.surface) >= 4.5f &&
                      contrast_ratio(t.text, t.surface_alt) >= 4.5f &&
                      contrast_ratio(t.text, t.accent) >= 4.5f &&
                      contrast_ratio(t.text, t.accent_alt) >= 4.5f &&
                      contrast_ratio(t.text, t.accent_container) >= 4.5f;
        contrast_ok = contrast_ok && contrast_ratio(over(t.text_dim, t.paper), t.paper) >= 4.5f &&
                      contrast_ratio(over(t.text_dim, t.surface), t.surface) >= 4.5f &&
                      contrast_ratio(over(t.text_alpha65, t.paper), t.paper) >= 4.5f &&
                      contrast_ratio(t.text_muted, t.pop_b) >= 4.5f;
        contrast_ok = contrast_ok && contrast_ratio(t.text, t.pop_a) >= 4.5f &&
                      contrast_ratio(t.text, t.pop_b) >= 4.5f &&
                      contrast_ratio(t.text, t.pop_c) >= 4.5f &&
                      contrast_ratio(t.lavender, t.paper) >= 4.5f;
        contrast_ok = contrast_ok && contrast_ratio(t.paper, t.ink) >= 4.5f &&
                      contrast_ratio(t.text, t.critical_container) >= 4.5f &&
                      contrast_ratio(t.text_muted, t.paper) >= 4.5f;
        if (!contrast_ok) {
            check(false, "text contrast holds for every seed hue");
            break;
        }
    }
    check(contrast_ok, "text contrast reaches 4.5 on paper, surfaces and "
                       "accents for every seed hue");

    Theme muted = theme_from_seed({{{350.0f, 0.05f, 0.7f}}, true});
    Theme candy = theme_from_seed({{{350.0f, 0.16f, 0.7f}}, true});
    check(oklch_from_color(candy.accent).C > oklch_from_color(muted.accent).C + 0.04f,
          "a vivid seed gives a more saturated accent than a muted one");
    check(oklch_from_color(muted.accent).C > 0.08f && oklch_from_color(muted.pop_a).C > 0.08f,
          "a muted wallpaper still gives a visible tint");
    bool ends_ok = true;
    for (const Theme *e : {&muted, &candy}) {
        ends_ok = ends_ok && contrast_ratio(e->text, e->accent) >= 4.5f &&
                  contrast_ratio(e->text, e->surface_alt) >= 4.5f &&
                  contrast_ratio(e->text_muted, e->paper) >= 4.5f;
    }
    check(ends_ok, "contrast holds at the muted and vivid ends");

    Theme paired = theme_from_seed({{{200.0f, 0.1f, 0.7f}, {320.0f, 0.1f, 0.7f}}, true});
    Theme rotated = theme_from_seed({{{200.0f, 0.1f, 0.7f}}, true});
    check(hue_distance(oklch_from_color(paired.accent_alt).h, 320.0f) < 6.0f,
          "the second seed drives accent_alt");
    check(hue_distance(oklch_from_color(rotated.accent_alt).h, 240.0f) < 6.0f,
          "accent_alt rotates the seed hue by 40 degrees without a second seed");

    Theme full = theme_from_seed(
        {{{200.0f, 0.1f, 0.7f}, {320.0f, 0.1f, 0.7f}, {80.0f, 0.1f, 0.7f}, {20.0f, 0.1f, 0.7f}},
         true});
    check(hue_distance(oklch_from_color(full.pop_a).h, 320.0f) < 8.0f &&
              hue_distance(oklch_from_color(full.pop_b).h, 80.0f) < 8.0f &&
              hue_distance(oklch_from_color(full.pop_c).h, 20.0f) < 8.0f,
          "the pop roles follow the second to fourth seeds");
    check(hue_distance(oklch_from_color(rotated.pop_a).h, 320.0f) < 8.0f &&
              hue_distance(oklch_from_color(rotated.pop_b).h, 80.0f) < 8.0f &&
              hue_distance(oklch_from_color(rotated.pop_c).h, 20.0f) < 8.0f,
          "the pop roles rotate the seed hue when the wallpaper has fewer hues");
    check(oklch_from_color(full.accent).C > 0.12f && oklch_from_color(full.pop_a).C > 0.12f,
          "a vivid wallpaper gives bold accent and pop colours");
    check(oklch_from_color(full.paper).L > 0.96f,
          "paper stays near white so the colour blocks stand out");

    Theme other = theme_from_seed({{{20.0f, 0.12f, 0.7f}}, true});
    check(!near(other.accent, palette::accent), "a different seed gives a different accent");
    theme_apply(other);
    check(near(other.accent, palette::accent) && near(other.text, palette::text) &&
              near(other.text_alpha20, palette::text_alpha20) &&
              near(other.overlay, palette::overlay),
          "theme_apply sets the palette to the derived values");
    check(
        near(astralia::bar::style_spec(BarStyle::islands, true).border, other.ink) &&
            near(astralia::bar::style_spec(BarStyle::islands, true).shadow_color, other.shadow) &&
            near(astralia::bar::style_spec(BarStyle::okinami, true).border, other.ink) &&
            near(astralia::bar::style_spec(BarStyle::okinami, true).bg, other.accent_container) &&
            near(astralia::bar::style_spec(BarStyle::continuous, true).bg, other.accent_container),
        "bar styles follow the live palette");
    test::apply_base_theme();
    check(near(base.accent, palette::accent), "theme_apply restores the base theme");
}
