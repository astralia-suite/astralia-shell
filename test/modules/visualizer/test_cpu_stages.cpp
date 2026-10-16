#include <algorithm>
#include <cmath>
#include <vector>

#include "config/visualizer_config.h"

#include "modules/visualizer/bars.h"
#include "modules/visualizer/cpu_stages.h"

#include "check.h"

void check_visualizer_bars();

void check_visualizer_cpu_stages() {
    using namespace astralia;
    using test::check;

    VisualizerCpuStages stages;
    check(!stages.ready() && !stages.run(0, {}, {}, 60), "nothing runs without samples");
    std::vector<float> small(8, 0.5f);
    check(!stages.run(16, small, small, 60), "a short input is refused");

    constexpr int size = 256;
    std::vector<float> loud(size, 0.8f);
    std::vector<float> quiet(size, 0.0f);
    check(stages.run(size, loud, quiet, 60) && stages.ready() && stages.size() == size,
          "a full input runs");
    float first = stages.smooth_texel(0, size / 4);
    check(first > 0.0f && first < 0.8f, "the newest frame carries the smallest weight");
    check(stages.smooth_texel(1, size / 4) == 0.0f, "a silent channel stays at zero");

    float previous = first;
    bool rose = false;
    for (int i = 0; i < 4; ++i) {
        stages.run(size, loud, quiet, 60);
        float current = stages.smooth_texel(0, size / 4);
        rose = rose || current > previous;
        previous = current;
    }
    check(rose, "a held tone builds up through the averaging window");

    float peak = previous;
    for (int i = 0; i < 80; ++i) {
        stages.run(size, quiet, quiet, 60);
    }
    float settled = stages.smooth_texel(0, size / 4);
    check(settled == 0.0f && peak > 0.0f, "gravity pulls a silent input back to zero");

    stages.run(size, loud, loud, 60);
    check(stages.smooth_texel(-5, 0) == stages.smooth_texel(0, 0) &&
              stages.smooth_texel(9, size + 10) == stages.smooth_texel(1, size - 1),
          "channel and index clamp into range");
    check_visualizer_bars();
}

void check_visualizer_bars() {
    using namespace astralia;
    using test::check;
    check(visualizer_bar_count(0.0f) == 1 &&
              visualizer_bar_count(1000.0f) ==
                  static_cast<int>((1000.0f - kVisualizerBarSpacing) /
                                   (kVisualizerBarWidth + kVisualizerBarSpacing)),
          "the bar count follows the slot width and never drops below one");
    int last = -1;
    bool rising = true;
    for (int i = 0; i < 40; ++i) {
        int texel = visualizer_bar_texel(i, 40, 4096);
        rising = rising && texel >= last && texel >= 0 && texel < 4096;
        last = texel;
    }
    check(rising && last < 4096 / 2, "bars sample the lower half of the spectrum, rising");
    check(visualizer_bar_texel(0, 0, 0) == 0, "an empty spectrum maps to the first texel");
    check(visualizer_bar_height(0.0f, 800.0f) == kVisualizerBarMinHeight &&
              visualizer_bar_height(2.0f, 800.0f) == kVisualizerBarHeightRatio * 800.0f,
          "a bar has a minimum height and clamps at the ratio");
}
