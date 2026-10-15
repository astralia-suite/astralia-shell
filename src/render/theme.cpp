#include <algorithm>
#include <cmath>

#include "render/oklab.h"
#include "render/theme.h"

namespace astralia {

namespace {

constexpr float alt_hue_offset = 40.0f;
constexpr float critical_hue = 27.0f;
constexpr float warn_hue = 75.0f;

float byte_alpha(int value) { return static_cast<float>(value) / 255.0f; }

constexpr float dusty_chroma = 0.04f;
constexpr float candy_chroma = 0.16f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float wrap_hue(float hue) {
    float h = std::fmod(hue, 360.0f);
    return h < 0.0f ? h + 360.0f : h;
}

} // namespace

Theme theme_from_seed(const Extraction &extraction) {
    float hue = extraction.primary.hue;
    float alt_hue = extraction.secondary ? extraction.secondary->hue : wrap_hue(hue + alt_hue_offset);

    float vivid = std::clamp((extraction.primary.chroma - dusty_chroma) / (candy_chroma - dusty_chroma), 0.0f, 1.0f);
    auto c = [vivid](float dusty, float candy) { return lerp(dusty, candy, vivid); };

    Theme t{};
    t.scheme = Scheme::light;

    t.ink = color_from_oklch({0.30f, c(0.080f, 0.130f), hue});
    t.paper = color_from_oklch({0.920f, c(0.040f, 0.090f), hue});
    t.surface = color_from_oklch({0.900f, c(0.050f, 0.100f), hue});
    t.surface_alt = color_from_oklch({0.860f, c(0.065f, 0.120f), hue});
    t.shadow = color_from_oklch({0.22f, 0.065f, hue});

    t.accent = color_from_oklch({0.80f, c(0.070f, 0.140f), hue});
    t.accent_alpha12 = with_alpha(t.accent, byte_alpha(0x1F));
    t.accent_alpha19 = with_alpha(t.accent, byte_alpha(0x30));
    t.accent_alpha25 = with_alpha(t.accent, byte_alpha(0x40));
    t.accent_container = color_from_oklch({0.90f, c(0.040f, 0.080f), hue});

    t.accent_alt = color_from_oklch({0.82f, c(0.070f, 0.140f), alt_hue});
    t.accent_alt_alpha50 = with_alpha(t.accent_alt, byte_alpha(0x80));
    t.accent_alt_container = color_from_oklch({0.92f, c(0.035f, 0.070f), alt_hue});

    t.base = t.paper;
    t.overlay = with_alpha(t.paper, byte_alpha(0xEB));

    t.lavender = color_from_oklch({0.55f, c(0.070f, 0.120f), hue});

    t.field_bg = t.surface;

    t.text = t.ink;
    t.text_alpha04 = with_alpha(t.ink, byte_alpha(0x0A));
    t.text_alpha06 = with_alpha(t.ink, byte_alpha(0x0F));
    t.text_alpha08 = with_alpha(t.ink, byte_alpha(0x14));
    t.text_alpha11 = with_alpha(t.ink, byte_alpha(0x1C));
    t.text_alpha15 = with_alpha(t.ink, byte_alpha(0x26));
    t.text_alpha20 = with_alpha(t.ink, byte_alpha(0x33));
    t.text_alpha65 = with_alpha(t.ink, byte_alpha(0xA6));
    t.text_muted = color_from_oklch({0.42f, c(0.060f, 0.100f), hue});
    t.text_dim = with_alpha(t.text_muted, byte_alpha(0x99));

    t.electro = color_from_oklch({0.70f, 0.150f, hue});

    t.critical = color_from_oklch({0.58f, 0.190f, critical_hue});
    t.critical_alpha15 = with_alpha(t.critical, byte_alpha(0x26));
    t.critical_container = color_from_oklch({0.86f, 0.070f, critical_hue});

    t.warn = color_from_oklch({0.68f, 0.140f, warn_hue});

    t.window_backdrop = with_alpha(color_from_oklch({0.20f, 0.030f, hue}), byte_alpha(0xB3));
    return t;
}

void theme_apply(const Theme &t) {
    palette::ink = t.ink;
    palette::paper = t.paper;
    palette::surface = t.surface;
    palette::surface_alt = t.surface_alt;
    palette::shadow = t.shadow;

    palette::accent = t.accent;
    palette::accent_alpha12 = t.accent_alpha12;
    palette::accent_alpha19 = t.accent_alpha19;
    palette::accent_alpha25 = t.accent_alpha25;
    palette::accent_container = t.accent_container;

    palette::accent_alt = t.accent_alt;
    palette::accent_alt_alpha50 = t.accent_alt_alpha50;
    palette::accent_alt_container = t.accent_alt_container;

    palette::base = t.base;
    palette::overlay = t.overlay;

    palette::lavender = t.lavender;

    palette::field_bg = t.field_bg;

    palette::text = t.text;
    palette::text_alpha04 = t.text_alpha04;
    palette::text_alpha06 = t.text_alpha06;
    palette::text_alpha08 = t.text_alpha08;
    palette::text_alpha11 = t.text_alpha11;
    palette::text_alpha15 = t.text_alpha15;
    palette::text_alpha20 = t.text_alpha20;
    palette::text_alpha65 = t.text_alpha65;
    palette::text_muted = t.text_muted;
    palette::text_dim = t.text_dim;

    palette::electro = t.electro;

    palette::critical = t.critical;
    palette::critical_alpha15 = t.critical_alpha15;
    palette::critical_container = t.critical_container;

    palette::warn = t.warn;

    palette::window_backdrop = t.window_backdrop;
}

} // namespace astralia
