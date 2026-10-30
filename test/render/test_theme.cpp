#include <cmath>
#include <utility>
#include <vector>

#include "modules/bar/paint/style.h"

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

astralia::Extraction lit(std::vector<astralia::Seed> seeds, float tone = 0.9f) {
    return {std::move(seeds), true, tone};
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
        Theme t = theme_from_seed(lit({{static_cast<float>(hue), 0.12f, 0.7f}}));
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

    Theme muted = theme_from_seed(lit({{350.0f, 0.05f, 0.7f}}));
    Theme candy = theme_from_seed(lit({{350.0f, 0.16f, 0.7f}}));
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

    Theme paired = theme_from_seed(lit({{200.0f, 0.1f, 0.7f}, {320.0f, 0.1f, 0.7f}}));
    Theme rotated = theme_from_seed(lit({{200.0f, 0.1f, 0.7f}}));
    check(hue_distance(oklch_from_color(paired.accent_alt).h, 320.0f) < 6.0f,
          "the second seed drives accent_alt");
    check(hue_distance(oklch_from_color(rotated.accent_alt).h, 240.0f) < 6.0f,
          "accent_alt rotates the seed hue by 40 degrees without a second seed");

    Theme full = theme_from_seed(lit(
        {{200.0f, 0.1f, 0.7f}, {320.0f, 0.1f, 0.7f}, {80.0f, 0.1f, 0.7f}, {20.0f, 0.1f, 0.7f}}));
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

    Theme night = theme_from_seed(lit({{195.0f, 0.09f, 0.75f}}, 0.15f));
    check(oklch_from_color(night.paper).L < 0.3f && oklch_from_color(night.text).L > 0.85f,
          "a dark wallpaper has a dark paper and a light text");
    check(oklch_from_color(night.ink).L < oklch_from_color(night.paper).L,
          "a dark wallpaper keeps ink as a dark outline color");
    check(oklch_from_color(night.border_outer).L > oklch_from_color(night.shadow).L + 0.4f &&
              near(base.border_outer, base.ink),
          "the dark outline stands apart from the shadow and the bright outline is the ink");
    check(hue_distance(oklch_from_color(night.accent).h, oklch_from_color(base.accent).h) < 1.0f,
          "the tone leaves the seed hue alone");
    check(oklch_from_color(night.text_muted).L > oklch_from_color(night.paper).L + 0.3f,
          "muted text stays readable on a dark paper");

    bool tone_ok = true;
    bool outline_ok = true;
    bool ink_ok = true;
    for (float tone : {0.15f, 0.35f, 0.5f, 0.65f, 0.9f}) {
        for (int hue = 0; hue < 360; hue += 15) {
            Theme t = theme_from_seed(lit({{static_cast<float>(hue), 0.12f, 0.7f}}, tone));
            for (const Color &ground :
                 {t.paper, t.surface, t.surface_alt, t.accent, t.accent_alt, t.pop_a, t.pop_b,
                  t.pop_c, t.accent_container, t.critical_container}) {
                tone_ok = tone_ok && contrast_ratio(t.text, ground) >= 4.5f;
            }
            for (const Color &ground : {t.paper, t.surface, t.surface_alt}) {
                tone_ok = tone_ok && contrast_ratio(t.text_muted, ground) >= 4.5f;
            }
            float inner = oklch_from_color(t.border_inner).L;
            float outer = oklch_from_color(t.border_outer).L;
            float drop = oklch_from_color(t.shadow).L;
            outline_ok = outline_ok && std::fabs(inner - outer) > 0.3f &&
                         std::fabs(outer - drop) > 0.04f && std::fabs(inner - drop) > 0.04f;
            float paper = oklch_from_color(t.paper).L;
            ink_ok = ink_ok && oklch_from_color(t.ink).L < paper &&
                     (oklch_from_color(t.text).L > paper || contrast_ratio(t.ink, t.paper) >= 4.5f);
        }
    }
    check(tone_ok, "text and muted text reach 4.5 on paper, surfaces and fills for every tone and "
                   "seed hue");
    check(outline_ok, "the inner border, outer border and shadow are three distinct colours at "
                      "every tone");
    check(ink_ok, "ink stays a deep line colour and reads on a paper that takes dark text");

    bool falling = true;
    float previous = 2.0f;
    for (int step = 18; step >= 2; --step) {
        float paper = oklch_from_color(theme_from_seed(lit({{195.0f, 0.09f, 0.75f}},
                                                           static_cast<float>(step) * 0.05f))
                                           .paper)
                          .L;
        falling = falling && paper <= previous + 1e-4f;
        previous = paper;
    }
    check(falling, "paper gets darker as the wallpaper tone falls");
    check(oklch_from_color(theme_from_seed(lit({{195.0f, 0.09f, 0.75f}}, 0.9f)).paper).L > 0.96f &&
              oklch_from_color(theme_from_seed(lit({{195.0f, 0.09f, 0.75f}}, 0.15f)).paper).L <
                  0.3f,
          "the extreme tones give a pale and a deep paper");
    check(near(theme_from_seed(lit({{195.0f, 0.09f, 0.75f}}, 0.75f)).paper, base.paper) &&
              near(theme_from_seed(lit({{195.0f, 0.09f, 0.75f}}, 0.35f)).paper, night.paper),
          "the ramp ends settle on the bright and dark tables");

    Theme other = theme_from_seed(lit({{20.0f, 0.12f, 0.7f}}));
    check(!near(other.accent, palette::accent), "a different seed gives a different accent");
    theme_apply(other);
    check(near(other.accent, palette::accent) && near(other.text, palette::text) &&
              near(other.text_alpha20, palette::text_alpha20) &&
              near(other.overlay, palette::overlay),
          "theme_apply sets the palette to the derived values");
    check(
        near(astralia::bar::style_spec(BarStyle::pop, true).border, other.border_outer) &&
            near(astralia::bar::style_spec(BarStyle::pop, true).shadow_color, other.shadow) &&
            near(astralia::bar::style_spec(BarStyle::okinami, true).border, other.border_outer) &&
            near(astralia::bar::style_spec(BarStyle::okinami, true).bg, other.accent_container) &&
            near(astralia::bar::style_spec(BarStyle::continuous, true).bg, other.accent_container),
        "bar styles follow the live palette");
    test::apply_base_theme();
    check(near(base.accent, palette::accent), "theme_apply restores the base theme");
}
