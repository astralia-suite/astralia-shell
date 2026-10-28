#include <algorithm>
#include <cmath>

#include "render/oklab.h"
#include "render/theme.h"

namespace astralia {

namespace {

constexpr float fallback_hue = 250.0f;
constexpr float alt_hue_offset = 40.0f;
constexpr float pop_a_offset = 120.0f;
constexpr float pop_b_offset = 240.0f;
constexpr float pop_c_offset = 180.0f;
constexpr float critical_hue = 27.0f;
constexpr float warn_hue = 75.0f;

float byte_alpha(int value) { return static_cast<float>(value) / 255.0f; }

constexpr float dusty_chroma = 0.07f;
constexpr float candy_chroma = 0.16f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float wrap_hue(float hue) {
    float h = std::fmod(hue, 360.0f);
    return h < 0.0f ? h + 360.0f : h;
}

} // namespace

Theme theme_from_seed(const Extraction &extraction, Scheme scheme) {
    const std::vector<Seed> &seeds = extraction.seeds;
    float hue = seeds.empty() ? fallback_hue : seeds[0].hue;
    float chroma = seeds.empty() ? dusty_chroma : seeds[0].chroma;
    auto seed_hue = [&](size_t index, float offset) {
        return index < seeds.size() ? seeds[index].hue : wrap_hue(hue + offset);
    };
    float alt_hue = seed_hue(1, alt_hue_offset);

    float vivid = std::clamp((chroma - dusty_chroma) / (candy_chroma - dusty_chroma), 0.0f, 1.0f);
    auto c = [vivid](float dusty, float candy) { return lerp(dusty, candy, vivid); };

    bool dark = scheme == Scheme::dark;
    auto l = [dark](float light, float night) { return dark ? night : light; };

    Theme t{};
    t.scheme = scheme;

    float tint = dark ? 1.4f : 1.0f;

    t.ink = color_from_oklch({l(0.20f, 0.15f), c(0.060f, 0.100f), hue});
    t.paper = color_from_oklch({l(0.975f, 0.27f), c(0.010f, 0.020f) * tint, hue});
    t.surface = color_from_oklch({l(0.945f, 0.32f), c(0.020f, 0.040f) * tint, hue});
    t.surface_alt = color_from_oklch({l(0.890f, 0.38f), c(0.040f, 0.070f) * tint, hue});
    t.shadow = color_from_oklch({l(0.14f, 0.07f), 0.050f, hue});
    t.border_outer = color_from_oklch({l(0.20f, 0.66f), c(0.060f, 0.100f), hue});
    t.border_inner = t.paper;

    t.accent = color_from_oklch({l(0.74f, 0.50f), c(0.090f, 0.190f), hue});
    t.accent_alpha12 = with_alpha(t.accent, byte_alpha(0x1F));
    t.accent_alpha19 = with_alpha(t.accent, byte_alpha(0x30));
    t.accent_alpha25 = with_alpha(t.accent, byte_alpha(0x40));
    t.accent_container = color_from_oklch({l(0.90f, 0.36f), c(0.060f, 0.080f), hue});

    t.accent_alt = color_from_oklch({l(0.80f, 0.48f), c(0.080f, 0.170f), alt_hue});
    t.accent_alt_alpha50 = with_alpha(t.accent_alt, byte_alpha(0x80));
    t.accent_alt_container = color_from_oklch({l(0.92f, 0.35f), c(0.050f, 0.070f), alt_hue});

    t.pop_a = color_from_oklch({l(0.76f, 0.48f), c(0.110f, 0.200f), seed_hue(1, pop_a_offset)});
    t.pop_b = color_from_oklch({l(0.86f, 0.46f), c(0.100f, 0.180f), seed_hue(2, pop_b_offset)});
    t.pop_c = color_from_oklch({l(0.80f, 0.47f), c(0.100f, 0.170f), seed_hue(3, pop_c_offset)});

    t.base = t.paper;
    t.overlay = with_alpha(t.paper, byte_alpha(0xEB));

    t.lavender = color_from_oklch({l(0.48f, 0.72f), c(0.090f, 0.150f), hue});

    t.field_bg = t.surface;

    t.text = color_from_oklch({l(0.20f, 0.93f), c(0.060f, 0.100f), hue});
    t.text_alpha04 = with_alpha(t.text, byte_alpha(0x0A));
    t.text_alpha06 = with_alpha(t.text, byte_alpha(0x0F));
    t.text_alpha08 = with_alpha(t.text, byte_alpha(0x14));
    t.text_alpha11 = with_alpha(t.text, byte_alpha(0x1C));
    t.text_alpha15 = with_alpha(t.text, byte_alpha(0x26));
    t.text_alpha20 = with_alpha(t.text, byte_alpha(0x33));
    t.text_alpha65 = with_alpha(t.text, byte_alpha(0xA6));
    t.text_muted = color_from_oklch({l(0.42f, 0.78f), c(0.060f, 0.100f), hue});
    t.text_dim = with_alpha(t.text_muted, byte_alpha(0xD0));

    t.electro = color_from_oklch({l(0.70f, 0.74f), 0.150f, hue});

    t.critical = color_from_oklch({l(0.58f, 0.66f), 0.190f, critical_hue});
    t.critical_alpha15 = with_alpha(t.critical, byte_alpha(0x26));
    t.critical_container = color_from_oklch({l(0.86f, 0.36f), 0.070f, critical_hue});

    t.warn = color_from_oklch({l(0.68f, 0.78f), 0.140f, warn_hue});

    t.window_backdrop =
        with_alpha(color_from_oklch({l(0.20f, 0.16f), 0.030f, hue}), byte_alpha(0xB3));
    return t;
}

void theme_apply(const Theme &t) {
    palette::ink = t.ink;
    palette::paper = t.paper;
    palette::surface = t.surface;
    palette::surface_alt = t.surface_alt;
    palette::shadow = t.shadow;
    palette::border_outer = t.border_outer;
    palette::border_inner = t.border_inner;

    palette::accent = t.accent;
    palette::accent_alpha12 = t.accent_alpha12;
    palette::accent_alpha19 = t.accent_alpha19;
    palette::accent_alpha25 = t.accent_alpha25;
    palette::accent_container = t.accent_container;

    palette::accent_alt = t.accent_alt;
    palette::accent_alt_alpha50 = t.accent_alt_alpha50;
    palette::accent_alt_container = t.accent_alt_container;

    palette::pop_a = t.pop_a;
    palette::pop_b = t.pop_b;
    palette::pop_c = t.pop_c;

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
