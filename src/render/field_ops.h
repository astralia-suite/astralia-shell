#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace astralia {

using FieldRng = std::mt19937;

float field_uniform(FieldRng &rng, float lo, float hi);

std::vector<float> value_noise(FieldRng &rng, int width, int height, float scale);

std::vector<float> rank_evenly(const std::vector<float> &values);

std::vector<float> gaussian_blur(const std::vector<float> &values, int width, int height,
                                 float sigma);

uint16_t quantize16(float value);

} // namespace astralia
