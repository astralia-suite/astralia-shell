#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

#include "render/field_ops.h"
#include "render/spread_map.h"

namespace astralia {

namespace {

// Noise octaves
inline constexpr float octave_weights[3] = {0.52f, 0.30f, 0.18f};
inline constexpr float octave_scales[3][2] = {{18.0f, 65.0f}, {7.0f, 20.0f}, {2.0f, 6.0f}};

// Cost field
inline constexpr float cost_floor = 0.12f;
inline constexpr float sharpness_min = 5.0f;
inline constexpr float sharpness_max = 9.0f;
inline constexpr float grain_min = 0.15f;
inline constexpr float grain_max = 0.65f;

// Colonies
inline constexpr int colonies_min = 6;
inline constexpr int colonies_max = 12;
inline constexpr float colony_margin_x = 0.06f;
inline constexpr float colony_margin_y = 0.08f;
inline constexpr float colony_delay = 0.12f;

// Glow twin
inline constexpr float blur_sigma = 2.5f;

std::vector<float> arrival(const std::vector<float> &cost,
                           const std::vector<std::pair<int, float>> &colonies, int w, int h) {
    constexpr int steps[8][2] = {{-1, 0},  {1, 0},  {0, -1}, {0, 1},
                                 {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    constexpr float diagonal = 1.4142f;
    std::vector<float> time(cost.size(), INFINITY);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> heap;
    for (auto [cell, start] : colonies) {
        if (start < time[static_cast<size_t>(cell)]) {
            time[static_cast<size_t>(cell)] = start;
            heap.push({start, cell});
        }
    }
    while (!heap.empty()) {
        auto [t, cell] = heap.top();
        heap.pop();
        if (t != time[static_cast<size_t>(cell)]) {
            continue;
        }
        int x = cell % w;
        int y = cell / w;
        float here = cost[static_cast<size_t>(cell)];
        for (int s = 0; s < 8; ++s) {
            int nx = x + steps[s][0];
            int ny = y + steps[s][1];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                continue;
            }
            int next = ny * w + nx;
            float step = s < 4 ? 1.0f : diagonal;
            float candidate = t + (here + cost[static_cast<size_t>(next)]) * 0.5f * step;
            if (candidate < time[static_cast<size_t>(next)]) {
                time[static_cast<size_t>(next)] = candidate;
                heap.push({candidate, next});
            }
        }
    }
    return time;
}

} // namespace

SpreadMap make_spread_map(uint32_t seed) {
    FieldRng rng(seed);
    int w = spread_map_width;
    int h = spread_map_height;
    size_t cells = static_cast<size_t>(w) * h;

    std::vector<float> noise(cells, 0.0f);
    for (int o = 0; o < 3; ++o) {
        float scale = field_uniform(rng, octave_scales[o][0], octave_scales[o][1]);
        std::vector<float> octave = value_noise(rng, w, h, scale);
        for (size_t i = 0; i < cells; ++i) {
            noise[i] += octave_weights[o] * octave[i];
        }
    }
    float sharpness = field_uniform(rng, sharpness_min, sharpness_max);
    float grain = field_uniform(rng, grain_min, grain_max);
    std::vector<float> cost(cells);
    for (size_t i = 0; i < cells; ++i) {
        cost[i] = cost_floor + std::exp(sharpness * (noise[i] - 0.5f)) +
                  grain * field_uniform(rng, 0.0f, 1.0f);
    }

    int count = std::uniform_int_distribution<int>(colonies_min, colonies_max)(rng);
    float delay = std::sqrt(static_cast<float>(cells)) * colony_delay;
    std::vector<std::pair<int, float>> colonies;
    for (int c = 0; c < count; ++c) {
        int x = static_cast<int>(field_uniform(rng, colony_margin_x, 1.0f - colony_margin_x) *
                                 static_cast<float>(w));
        int y = static_cast<int>(field_uniform(rng, colony_margin_y, 1.0f - colony_margin_y) *
                                 static_cast<float>(h));
        colonies.emplace_back(y * w + x, field_uniform(rng, 0.0f, delay));
    }

    std::vector<float> rank = rank_evenly(arrival(cost, colonies, w, h));
    std::vector<float> glow = gaussian_blur(rank, w, h, blur_sigma);

    SpreadMap map{w, h, std::vector<uint8_t>(cells * 4)};
    for (size_t i = 0; i < cells; ++i) {
        uint16_t a = quantize16(rank[i]);
        uint16_t g = quantize16(glow[i]);
        map.rgba[i * 4 + 0] = static_cast<uint8_t>(a >> 8);
        map.rgba[i * 4 + 1] = static_cast<uint8_t>(a & 0xFF);
        map.rgba[i * 4 + 2] = static_cast<uint8_t>(g >> 8);
        map.rgba[i * 4 + 3] = static_cast<uint8_t>(g & 0xFF);
    }
    return map;
}

} // namespace astralia
