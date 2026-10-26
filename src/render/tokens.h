#pragma once

#include <cstdint>
#include <string_view>

namespace astralia {

struct Color {
    float r, g, b, a;
};

inline const float *rgba(const Color &c) { return &c.r; }

constexpr uint8_t digit(char c) {
    return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
}

constexpr Color color(std::string_view hex) {
    size_t i = hex.front() == '#' ? 1 : 0;
    auto byte = [&](size_t at) {
        return static_cast<float>(digit(hex[at]) * 16 + digit(hex[at + 1])) / 255.0f;
    };
    float a = hex.size() - i > 6 ? byte(i + 6) : 1.0f;
    return {byte(i), byte(i + 2), byte(i + 4), a};
}

inline constexpr Color with_alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

inline constexpr Color lerp_color(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}

namespace palette {

inline constexpr Color white{1.0f, 1.0f, 1.0f, 1.0f};

inline Color ink{};
inline Color paper{};
inline Color surface{};
inline Color shadow{};

inline Color accent{};
inline Color accent_alpha12{};
inline Color accent_alpha19{};
inline Color accent_alpha25{};
inline Color accent_container{};

inline Color accent_alt{};
inline Color accent_alt_alpha50{};
inline Color accent_alt_container{};

inline Color pop_a{};
inline Color pop_b{};
inline Color pop_c{};

inline Color base{};
inline Color overlay{};

inline Color lavender{};

inline Color field_bg{};
inline Color surface_alt{};

inline Color text{};
inline Color text_alpha04{};
inline Color text_alpha06{};
inline Color text_alpha08{};
inline Color text_alpha11{};
inline Color text_alpha15{};
inline Color text_alpha20{};
inline Color text_alpha65{};
inline Color text_muted{};
inline Color text_dim{};

inline Color electro{};

inline Color critical{};
inline Color critical_alpha15{};
inline Color critical_container{};

inline Color warn{};

inline Color window_backdrop{};

} // namespace palette

namespace metrics {

inline constexpr float radius_card = 10.0f;
inline constexpr float radius_widget = 5.0f;
inline constexpr float border_thin = 2.0f;
inline constexpr float border_thick = 3.0f;
inline constexpr float shadow_offset = 4.0f;
inline constexpr float ring_width = 3.0f;
inline constexpr float shadow_widget = 2.0f;

} // namespace metrics

} // namespace astralia
