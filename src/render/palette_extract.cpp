#include <algorithm>
#include <vector>

#include "render/oklab.h"
#include "render/palette_extract.h"

namespace astralia {

namespace {

constexpr int max_side = 64;
constexpr int cluster_count = 8;
constexpr int iteration_cap = 12;
constexpr float alpha_floor = 0.5f;
constexpr float grey_chroma = 0.04f;
constexpr float vivid_chroma = 0.12f;
constexpr float secondary_hue_gap = 40.0f;
constexpr float merge_distance = 1e-4f;

float chroma_weight(float chroma) {
    return std::clamp((chroma - grey_chroma) / (vivid_chroma - grey_chroma), 0.0f, 1.0f);
}

float distance_sq(const Oklab &a, const Oklab &b) {
    float dl = a.L - b.L;
    float da = a.a - b.a;
    float db = a.b - b.b;
    return dl * dl + da * da + db * db;
}

std::vector<Oklab> downsample(const uint8_t *rgba, int width, int height, bool premultiplied) {
    int step = std::max(1, (std::max(width, height) + max_side - 1) / max_side);
    std::vector<Oklab> samples;
    for (int y0 = 0; y0 < height; y0 += step) {
        for (int x0 = 0; x0 < width; x0 += step) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            int count = 0;
            for (int y = y0; y < std::min(y0 + step, height); ++y) {
                for (int x = x0; x < std::min(x0 + step, width); ++x) {
                    const uint8_t *p = rgba + (static_cast<size_t>(y) * width + x) * 4;
                    float alpha = p[3] / 255.0f;
                    float un = premultiplied && alpha > 0.0f ? 1.0f / alpha : 1.0f;
                    r += srgb_to_linear(std::min(p[0] / 255.0f * un, 1.0f)) * alpha;
                    g += srgb_to_linear(std::min(p[1] / 255.0f * un, 1.0f)) * alpha;
                    b += srgb_to_linear(std::min(p[2] / 255.0f * un, 1.0f)) * alpha;
                    a += alpha;
                    ++count;
                }
            }
            if (count == 0 || a / count < alpha_floor) {
                continue;
            }
            samples.push_back(oklab_from_linear(r / a, g / a, b / a));
        }
    }
    return samples;
}

struct Cluster {
    Oklab center;
    int population = 0;
};

std::vector<Cluster> cluster(const std::vector<Oklab> &samples) {
    Oklab mean{0.0f, 0.0f, 0.0f};
    for (const Oklab &s : samples) {
        mean.L += s.L;
        mean.a += s.a;
        mean.b += s.b;
    }
    float n = static_cast<float>(samples.size());
    mean = {mean.L / n, mean.a / n, mean.b / n};

    std::vector<Cluster> clusters;
    size_t first = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if (distance_sq(samples[i], mean) < distance_sq(samples[first], mean)) {
            first = i;
        }
    }
    clusters.push_back({samples[first], 0});

    std::vector<float> nearest(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        nearest[i] = distance_sq(samples[i], clusters[0].center);
    }
    while (static_cast<int>(clusters.size()) < cluster_count) {
        size_t far = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (nearest[i] > nearest[far]) {
                far = i;
            }
        }
        if (nearest[far] < merge_distance) {
            break;
        }
        clusters.push_back({samples[far], 0});
        for (size_t i = 0; i < samples.size(); ++i) {
            nearest[i] = std::min(nearest[i], distance_sq(samples[i], samples[far]));
        }
    }

    std::vector<size_t> owner(samples.size(), 0);
    for (int iteration = 0; iteration < iteration_cap; ++iteration) {
        bool moved = false;
        for (size_t i = 0; i < samples.size(); ++i) {
            size_t best = 0;
            float best_distance = distance_sq(samples[i], clusters[0].center);
            for (size_t c = 1; c < clusters.size(); ++c) {
                float d = distance_sq(samples[i], clusters[c].center);
                if (d < best_distance) {
                    best = c;
                    best_distance = d;
                }
            }
            if (owner[i] != best) {
                moved = true;
                owner[i] = best;
            }
        }
        std::vector<Oklab> sums(clusters.size(), Oklab{0.0f, 0.0f, 0.0f});
        std::vector<int> counts(clusters.size(), 0);
        for (size_t i = 0; i < samples.size(); ++i) {
            Oklab &sum = sums[owner[i]];
            sum.L += samples[i].L;
            sum.a += samples[i].a;
            sum.b += samples[i].b;
            ++counts[owner[i]];
        }
        for (size_t c = 0; c < clusters.size(); ++c) {
            clusters[c].population = counts[c];
            if (counts[c] > 0) {
                float k = static_cast<float>(counts[c]);
                clusters[c].center = {sums[c].L / k, sums[c].a / k, sums[c].b / k};
            }
        }
        if (!moved) {
            break;
        }
    }
    return clusters;
}

} // namespace

Extraction extract_seeds(const uint8_t *rgba, int width, int height, bool premultiplied) {
    Extraction none{{0.0f, 0.0f, 0.0f}, std::nullopt, false};
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return none;
    }
    std::vector<Oklab> samples = downsample(rgba, width, height, premultiplied);
    if (samples.empty()) {
        return none;
    }

    struct Scored {
        Seed seed;
        float score;
    };
    std::vector<Scored> ranked;
    float total = static_cast<float>(samples.size());
    for (const Cluster &c : cluster(samples)) {
        Oklch lch = oklch_from_oklab(c.center);
        float score = c.population / total * chroma_weight(lch.C);
        if (c.population > 0 && score > 0.0f) {
            ranked.push_back({{lch.h, lch.C, lch.L}, score});
        }
    }
    if (ranked.empty()) {
        return none;
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });

    Extraction result{ranked[0].seed, std::nullopt, true};
    for (size_t i = 1; i < ranked.size(); ++i) {
        if (hue_distance(ranked[i].seed.hue, result.primary.hue) >= secondary_hue_gap) {
            result.secondary = ranked[i].seed;
            break;
        }
    }
    return result;
}

} // namespace astralia
