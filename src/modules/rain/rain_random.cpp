#include <random>

#include "modules/rain/rain_random.h"

namespace astralia {

namespace {

std::mt19937 &rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

} // namespace

void rain_seed(uint32_t seed) { rng().seed(seed); }

float rain_random01() {
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng());
}

float rain_random_range(float lo, float hi) { return lo + (hi - lo) * rain_random01(); }

} // namespace astralia
