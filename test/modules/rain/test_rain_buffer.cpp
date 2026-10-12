#include <cstdint>
#include <vector>

#include "modules/rain/matrix_rain.h"
#include "modules/rain/rain_random.h"
#include "modules/rain/stiletto_rain.h"

#include "check.h"

namespace {

std::vector<uint8_t> copy(const astralia::RainFrame &frame) {
    return std::vector<uint8_t>(frame.pixels, frame.pixels + static_cast<size_t>(frame.stride) * frame.height);
}

bool has_ink(const std::vector<uint8_t> &pixels) {
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            return true;
        }
    }
    return false;
}

template <typename Rain>
void check_rain(const char *name) {
    Rain rain;
    test::check(!rain.frame().ready(), "a rain with no rebuild has no frame");
    rain.rebuild(480, 600, false);
    astralia::RainFrame frame = rain.frame();
    test::check(frame.ready() && frame.width == 480 && frame.height == 600 && frame.stride >= 480 * 4, name);
    test::check(!has_ink(copy(frame)), "a fresh buffer is transparent");

    astralia::rain_seed(7);
    rain.rebuild(480, 600, true);
    for (int i = 0; i < 40; ++i) {
        rain.tick();
    }
    std::vector<uint8_t> first = copy(rain.frame());
    test::check(has_ink(first), "ticking draws");

    astralia::rain_seed(7);
    rain.rebuild(480, 600, true);
    for (int i = 0; i < 40; ++i) {
        rain.tick();
    }
    test::check(copy(rain.frame()) == first, "the same seed gives the same frames");

    rain.rebuild(200, 100, false);
    test::check(rain.frame().width == 200 && rain.frame().height == 100, "a rebuild resizes the buffer");
    rain.rebuild(0, 0, false);
    test::check(rain.frame().width == 1 && rain.frame().height == 1, "a degenerate size is clamped");
}

} // namespace

void check_rain_buffer() {
    check_rain<astralia::MatrixRain>("the matrix buffer has the requested size");
    check_rain<astralia::StilettoRain>("the stiletto buffer has the requested size");
}
