#include <algorithm>
#include <cmath>

#include "config/visualizer_config.h"

#include "modules/visualizer/bars.h"

namespace astralia {

int visualizer_bar_count(float width) {
    float slot = kVisualizerBarWidth + kVisualizerBarSpacing;
    return std::max(1, static_cast<int>((width - kVisualizerBarSpacing) / slot));
}

int visualizer_bar_texel(int index, int count, int size) {
    float position = (static_cast<float>(index) + 0.5f) / static_cast<float>(std::max(count, 1));
    float coordinate = std::pow(std::max(position, 1e-4f), 1.8f) * 0.5f;
    return std::clamp(static_cast<int>(std::floor(coordinate * static_cast<float>(size))), 0,
                      std::max(size - 1, 0));
}

float visualizer_bar_height(float magnitude, float area_height) {
    return std::max(kVisualizerBarMinHeight,
                    std::clamp(magnitude, 0.0f, 1.0f) * kVisualizerBarHeightRatio * area_height);
}

} // namespace astralia
