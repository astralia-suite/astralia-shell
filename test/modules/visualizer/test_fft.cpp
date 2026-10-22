#include <cmath>
#include <numbers>
#include <vector>

#include "config/visualizer_config.h"

#include "modules/visualizer/fft.h"

#include "check.h"

namespace {

int peak_bin(const std::vector<float> &data, int complex_points) {
    int best = 1;
    float best_v = -1.0f;
    for (int k = 1; k < complex_points / 2; ++k) {
        float v = data[static_cast<size_t>(2 * k)];
        if (v > best_v) {
            best_v = v;
            best = k;
        }
    }
    return best;
}

} // namespace

void check_visualizer_fft() {
    using namespace astralia;
    const int n = visualizer_config::kFragmentSize;
    const int complex_points = n / 2;

    std::vector<float> silence(static_cast<size_t>(n), 0.0f);
    astralia::visualizer::fft(silence.data(), n, visualizer_config::kFftScale,
                              visualizer_config::kFftCutOff);
    for (float v : silence)
        test::check(v == 0.0f, "silence stays silent");

    const int tone_bin = 64;
    std::vector<float> tone(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < complex_points; ++i)
        tone[static_cast<size_t>(2 * i)] =
            std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(tone_bin) *
                     static_cast<float>(i) / static_cast<float>(complex_points));

    astralia::visualizer::fft(tone.data(), n, visualizer_config::kFftScale,
                              visualizer_config::kFftCutOff);

    int p = peak_bin(tone, complex_points);
    test::check(std::abs(p - tone_bin) <= 2, "a tone peaks at its bin");

    float at_tone = tone[static_cast<size_t>(2 * tone_bin)];
    float far = tone[static_cast<size_t>(2 * (complex_points / 2 - 4))];
    test::check(at_tone > far * 4.0f, "the tone stands out from the far bins");
}
