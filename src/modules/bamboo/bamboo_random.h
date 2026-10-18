#pragma once

#include <cstdint>

namespace astralia {

void bamboo_seed(uint32_t seed);
float bamboo_random01();
float bamboo_random_range(float lo, float hi);

} // namespace astralia
