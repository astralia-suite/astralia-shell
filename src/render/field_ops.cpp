#include <algorithm>
#include <cmath>
#include <numeric>

#include "render/field_ops.h"

namespace astralia {

namespace {

float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

} // namespace

float field_uniform(FieldRng &rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

std::vector<float> value_noise(FieldRng &rng, int width, int height, float scale) {
    int nx = static_cast<int>(static_cast<float>(width) / scale) + 3;
    int ny = static_cast<int>(static_cast<float>(height) / scale) + 3;
    std::vector<float> lattice(static_cast<size_t>(nx) * ny);
    for (float &v : lattice) {
        v = field_uniform(rng, 0.0f, 1.0f);
    }
    auto at = [&](int x, int y) { return lattice[static_cast<size_t>(y) * nx + x]; };
    std::vector<float> out(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        float py = static_cast<float>(y) / scale;
        int iy = static_cast<int>(py);
        float fy = smoothstep01(py - static_cast<float>(iy));
        for (int x = 0; x < width; ++x) {
            float px = static_cast<float>(x) / scale;
            int ix = static_cast<int>(px);
            float fx = smoothstep01(px - static_cast<float>(ix));
            float top = at(ix, iy) * (1.0f - fx) + at(ix + 1, iy) * fx;
            float bottom = at(ix, iy + 1) * (1.0f - fx) + at(ix + 1, iy + 1) * fx;
            out[static_cast<size_t>(y) * width + x] = top * (1.0f - fy) + bottom * fy;
        }
    }
    return out;
}

std::vector<float> rank_evenly(const std::vector<float> &values) {
    std::vector<int> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, {}, [&](int i) { return values[static_cast<size_t>(i)]; });
    std::vector<float> rank(values.size());
    float last = static_cast<float>(std::max<size_t>(values.size() - 1, 1));
    for (size_t k = 0; k < order.size(); ++k) {
        rank[static_cast<size_t>(order[k])] = static_cast<float>(k) / last;
    }
    return rank;
}

std::vector<float> gaussian_blur(const std::vector<float> &values, int width, int height,
                                 float sigma) {
    int radius = static_cast<int>(3.0f * sigma) + 1;
    std::vector<float> kernel(static_cast<size_t>(radius) * 2 + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        float k = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = k;
        sum += k;
    }
    for (float &k : kernel) {
        k /= sum;
    }
    std::vector<float> rows(values.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                int sx = std::clamp(x + i, 0, width - 1);
                v += kernel[static_cast<size_t>(i + radius)] *
                     values[static_cast<size_t>(y) * width + sx];
            }
            rows[static_cast<size_t>(y) * width + x] = v;
        }
    }
    std::vector<float> out(values.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                int sy = std::clamp(y + i, 0, height - 1);
                v += kernel[static_cast<size_t>(i + radius)] *
                     rows[static_cast<size_t>(sy) * width + x];
            }
            out[static_cast<size_t>(y) * width + x] = v;
        }
    }
    return out;
}

uint16_t quantize16(float value) {
    return static_cast<uint16_t>(std::clamp(value * 65535.0f + 0.5f, 0.0f, 65535.0f));
}

} // namespace astralia
