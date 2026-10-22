#pragma once

#include <cstdint>

namespace astralia::rain {

void rain_seed(uint32_t seed);
float random01();
float random_range(float lo, float hi);

} // namespace astralia::rain
