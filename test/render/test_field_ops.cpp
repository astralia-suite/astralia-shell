#include <algorithm>
#include <cmath>
#include <vector>

#include "render/field_ops.h"

#include "check.h"

void check_field_ops() {
    using test::check;
    constexpr int w = 40;
    constexpr int h = 30;

    astralia::FieldRng first(3);
    astralia::FieldRng second(3);
    std::vector<float> noise = astralia::value_noise(first, w, h, 8.0f);
    check(noise == astralia::value_noise(second, w, h, 8.0f), "the same seed gives the same noise");
    check(std::ranges::all_of(noise, [](float v) { return v >= 0.0f && v <= 1.0f; }),
          "value noise stays within 0 to 1");

    std::vector<float> values{3.0f, 1.0f, 2.0f, 0.0f};
    std::vector<float> ranks = astralia::rank_evenly(values);
    check(ranks[3] == 0.0f && ranks[1] == 1.0f / 3.0f && ranks[0] == 1.0f,
          "ranking spreads the order evenly over 0 to 1");

    std::vector<float> flat(static_cast<size_t>(w) * h, 0.25f);
    std::vector<float> blurred = astralia::gaussian_blur(flat, w, h, 3.0f);
    check(std::ranges::all_of(blurred, [](float v) { return std::abs(v - 0.25f) < 1e-5f; }),
          "blurring a flat field keeps it flat");
    check(astralia::quantize16(0.0f) == 0 && astralia::quantize16(1.0f) == 65535 &&
              astralia::quantize16(2.0f) == 65535,
          "quantizing clamps to 16 bits");
}
