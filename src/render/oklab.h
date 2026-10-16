#pragma once

#include <algorithm>
#include <cmath>

#include "render/tokens.h"

namespace astralia {

struct Oklab {
    float L, a, b;
};

struct Oklch {
    float L, C, h;
};

inline float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline Oklab oklab_from_linear(float r, float g, float b) {
    float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

inline Oklab oklab_from_color(const Color &c) {
    return oklab_from_linear(srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b));
}

struct LinearRgb {
    float r, g, b;
};

inline LinearRgb linear_from_oklab(const Oklab &lab) {
    float l = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    float m = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
    float s = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

inline bool in_gamut(const LinearRgb &c) {
    constexpr float eps = 1e-4f;
    return c.r >= -eps && c.r <= 1.0f + eps && c.g >= -eps && c.g <= 1.0f + eps && c.b >= -eps &&
           c.b <= 1.0f + eps;
}

inline Color color_from_linear(const LinearRgb &c, float alpha = 1.0f) {
    auto encode = [](float v) { return linear_to_srgb(std::clamp(v, 0.0f, 1.0f)); };
    return {encode(c.r), encode(c.g), encode(c.b), alpha};
}

inline Oklab oklab_from_oklch(const Oklch &lch) {
    float rad = lch.h * 3.14159265358979f / 180.0f;
    return {lch.L, lch.C * std::cos(rad), lch.C * std::sin(rad)};
}

inline Oklch oklch_from_oklab(const Oklab &lab) {
    float h = std::atan2(lab.b, lab.a) * 180.0f / 3.14159265358979f;
    return {lab.L, std::hypot(lab.a, lab.b), h < 0.0f ? h + 360.0f : h};
}

inline Oklch oklch_from_color(const Color &c) { return oklch_from_oklab(oklab_from_color(c)); }

inline Color color_from_oklch(const Oklch &lch, float alpha = 1.0f) {
    float low = 0.0f;
    float high = lch.C;
    if (in_gamut(linear_from_oklab(oklab_from_oklch({lch.L, high, lch.h})))) {
        return color_from_linear(linear_from_oklab(oklab_from_oklch({lch.L, high, lch.h})), alpha);
    }
    for (int i = 0; i < 20; ++i) {
        float mid = 0.5f * (low + high);
        if (in_gamut(linear_from_oklab(oklab_from_oklch({lch.L, mid, lch.h})))) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return color_from_linear(linear_from_oklab(oklab_from_oklch({lch.L, low, lch.h})), alpha);
}

inline float relative_luminance(const Color &c) {
    return 0.2126f * srgb_to_linear(c.r) + 0.7152f * srgb_to_linear(c.g) +
           0.0722f * srgb_to_linear(c.b);
}

inline float contrast_ratio(const Color &a, const Color &b) {
    float la = relative_luminance(a);
    float lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

inline float hue_distance(float a, float b) {
    float d = std::fabs(a - b);
    d = std::fmod(d, 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

} // namespace astralia
