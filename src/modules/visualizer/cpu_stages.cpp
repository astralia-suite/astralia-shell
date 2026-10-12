#include <algorithm>
#include <cmath>
#include <numbers>

#include "modules/visualizer/cpu_stages.h"

namespace astralia {

namespace {

constexpr int max_smooth_iterations = 512;

float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float hamming(float t, float size) { return 0.53836f - 0.46164f * std::cos(2.0f * std::numbers::pi_v<float> * t / size); }

float sinusoidal(float x) { return 0.5f * std::sin(std::numbers::pi_v<float> * x - std::numbers::pi_v<float> / 2.0f) + 0.5f; }

float scale_audio(float index) { return -std::log(-kVisualizerSampleRange * index + 1.0f) / kVisualizerSampleScale; }

float fetch(const std::vector<float> &source, float s) {
    int size = static_cast<int>(source.size());
    int coordinate = static_cast<int>(std::floor(s + 0.5f));
    return source[static_cast<size_t>(std::clamp(coordinate, 0, size - 1))];
}

} // namespace

float VisualizerCpuStages::smooth_at(const std::vector<float> &source, float u) const {
    float size = static_cast<float>(source.size());
    float smin = scale_audio(clamp01(u - kVisualizerSmoothFactor)) * size;
    float smax = scale_audio(clamp01(u + kVisualizerSmoothFactor)) * size;
    float half = (smax - smin) / 2.0f;
    float middle = smin + half;
    float weight_of_distance = 0.0f;
    float total = 0.0f;
    float weight = 0.0f;
    float peak = 0.0f;
    for (int n = 0; n < max_smooth_iterations; ++n) {
        float s = smin + static_cast<float>(n);
        if (s > smax) {
            break;
        }
        weight_of_distance = half > 0.0f ? sinusoidal(clamp01((half - std::fabs(middle - s)) / half)) : 0.0f;
        float value = fetch(source, s);
        weight += weight_of_distance;
        total += value * weight_of_distance;
        peak = std::max(peak, value * weight_of_distance);
    }
    float average = weight > 0.0f ? total / weight : 0.0f;
    float result = 0.0f;
    if (kVisualizerSampleMode == 0) {
        result = average;
    } else if (kVisualizerSampleMode == 1) {
        result = peak;
    } else {
        result = peak * (1.0f - kVisualizerSampleHybridWeight) + average * kVisualizerSampleHybridWeight;
    }
    return result / (2.0f * (static_cast<float>(kVisualizerAdjacentSampleNums) - 1.0f) + 1.0f);
}

void VisualizerCpuStages::run_channel(Channel &channel, const std::vector<float> &raw, int fps) {
    size_t size = static_cast<size_t>(size_);
    if (channel.held.size() != size) {
        channel.held.assign(size, 0.0f);
        for (std::vector<float> &frame : channel.ring) {
            frame.assign(size, 0.0f);
        }
        channel.average.assign(size, 0.0f);
        channel.ring_index = 0;
    }
    float drop = kVisualizerGravityStep / static_cast<float>(fps);
    for (size_t i = 0; i < size; ++i) {
        float held = std::max(channel.held[i], clamp01(raw[i]));
        channel.held[i] = clamp01(held - drop);
    }
    channel.ring[static_cast<size_t>(channel.ring_index)] = channel.held;
    for (size_t i = 0; i < size; ++i) {
        float sum = 0.0f;
        for (int t = 0; t < ring_size; ++t) {
            int frame = channel.ring_index - t;
            if (frame < 0) {
                frame += ring_size;
            }
            sum += hamming(static_cast<float>(t), static_cast<float>(kVisualizerGravityAverageFrames - 1)) * channel.ring[static_cast<size_t>(frame)][i];
        }
        channel.average[i] = clamp01(sum / static_cast<float>(kVisualizerGravityAverageFrames));
    }
    channel.ring_index = (channel.ring_index + 1) % ring_size;
}

bool VisualizerCpuStages::run(int size, const std::vector<float> &left, const std::vector<float> &right, int fps) {
    if (size <= 0 || fps <= 0 || static_cast<int>(left.size()) < size || static_cast<int>(right.size()) < size) {
        return false;
    }
    size_ = size;
    run_channel(channels_[0], left, fps);
    run_channel(channels_[1], right, fps);
    return true;
}

float VisualizerCpuStages::smooth_texel(int channel, int index) const {
    const std::vector<float> &average = channels_[static_cast<size_t>(std::clamp(channel, 0, 1))].average;
    if (average.empty()) {
        return 0.0f;
    }
    float u = (static_cast<float>(std::clamp(index, 0, size_ - 1)) + 0.5f) / static_cast<float>(size_);
    return clamp01(smooth_at(average, u));
}

} // namespace astralia
