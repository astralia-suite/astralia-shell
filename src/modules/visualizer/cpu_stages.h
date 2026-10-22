#pragma once

#include <array>
#include <vector>

#include "config/visualizer_config.h"

namespace astralia::visualizer {

class CpuStages {
  public:
    bool run(int size, const std::vector<float> &left, const std::vector<float> &right, int fps);

    float smooth_texel(int channel, int index) const;
    int size() const { return size_; }
    bool ready() const { return size_ > 0; }

  private:
    static constexpr int ring_size = 5;

    struct Channel {
        std::vector<float> held;
        std::array<std::vector<float>, ring_size> ring;
        std::vector<float> average;
        int ring_index = 0;
    };

    float smooth_at(const std::vector<float> &source, float u) const;
    void run_channel(Channel &channel, const std::vector<float> &raw, int fps);

    std::array<Channel, 2> channels_;
    int size_ = 0;
};

} // namespace astralia::visualizer
