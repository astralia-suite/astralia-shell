#pragma once

#include <cstdint>

namespace astralia {

void rain_seed(uint32_t seed);
float rain_random01();
float rain_random_range(float lo, float hi);

} // namespace astralia
