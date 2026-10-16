#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "config/launcher_config.h"
#include "core/animation.h"

#include "modules/launcher/entrance.h"

namespace astralia {

namespace {

constexpr float trace_start_ms = 30.0f;
constexpr float trace_end_ms = 380.0f;
constexpr float blast_ms = 490.0f;
constexpr float fall_ms = 60.0f;
constexpr float charge_lead_ms = 40.0f;
constexpr float spark_grow_ms = 50.0f;
constexpr float fork_delay_ms = 25.0f;
constexpr float strike_fade_ms = 240.0f;
constexpr float flash_peak = 0.6f;
constexpr float flash_ms = 140.0f;
constexpr float outline_fade_ms = 240.0f;
constexpr float branch_ms = 150.0f;
constexpr float branch_length = 20.0f;
constexpr float charge_gain = 0.5f;
constexpr float scrim_peak = 0.35f;
constexpr float scrim_in_ms = 300.0f;
constexpr float scrim_out_ms = 300.0f;
constexpr float burst_ms = 280.0f;

float seg(float ms, float from, float to) {
    return std::clamp((ms - from) / (to - from), 0.0f, 1.0f);
}

float ease_in_out_cubic_inverse(float p) {
    p = std::clamp(p, 0.0f, 1.0f);
    return p < 0.5f ? std::cbrt(p / 4.0f) : 1.0f - std::cbrt(2.0f * (1.0f - p)) / 2.0f;
}

float hash01(float seed, int a) {
    uint32_t x = static_cast<uint32_t>(seed * 1000.0f) * 2654435761u;
    x ^= static_cast<uint32_t>(a) * 40503u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}

} // namespace

EntranceFrame entrance_sample(const EntranceState &state, const ui::Box &box) {
    EntranceFrame frame;
    float ms = std::clamp(state.t, 0.0f, 1.0f) * launcher_config::entrance_ms;
    frame.time_s = ms / 1000.0f;
    frame.opacity = ms >= blast_ms ? 1.0f : 0.0f;
    frame.flash =
        ms >= blast_ms ? flash_peak * (1.0f - seg(ms, blast_ms, blast_ms + flash_ms)) : 0.0f;
    frame.scrim = scrim_peak * seg(ms, 0.0f, scrim_in_ms) *
                  (1.0f - seg(ms, blast_ms, blast_ms + scrim_out_ms));

    std::array<float, 4> x{box.x, box.x + box.w, box.x + box.w, box.x};
    std::array<float, 4> y{box.y, box.y, box.y + box.h, box.y + box.h};
    std::array<float, 4> length{box.w, box.h, box.w, box.h};
    constexpr std::array<float, 4> dir_x{-1.0f, 1.0f, 1.0f, -1.0f};
    constexpr std::array<float, 4> dir_y{-1.0f, -1.0f, 1.0f, 1.0f};
    float perimeter = 2.0f * (box.w + box.h);
    float head =
        perimeter * applyEasing(Easing::EaseInOutCubic, seg(ms, trace_start_ms, trace_end_ms));
    float charge = 1.0f + charge_gain * seg(ms, trace_end_ms - charge_lead_ms, blast_ms);
    float outline = ms < blast_ms ? charge : 1.0f - seg(ms, blast_ms, blast_ms + outline_fade_ms);

    auto add_spark = [&](std::size_t i, float arrival) {
        float grow = seg(ms, arrival, arrival + spark_grow_ms);
        float fade = 1.0f - seg(ms, arrival + spark_grow_ms, arrival + spark_grow_ms + branch_ms);
        if (grow > 0.0f && fade > 0.0f) {
            frame.bolts.push_back({x[i], y[i], x[i] + dir_x[i] * branch_length,
                                   y[i] + dir_y[i] * branch_length, grow, fade,
                                   state.seed + 7.0f + static_cast<float>(i), 6.0f, 0.6f, true});
        }
    };

    float start = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t next = (i + 1) % 4;
        float progress = std::clamp((head - start) / length[i], 0.0f, 1.0f);
        if (progress > 0.0f && outline > 0.0f) {
            frame.bolts.push_back({x[i], y[i], x[next], y[next], progress, outline,
                                   state.seed + static_cast<float>(i), 4.0f, 0.6f, true});
        }
        add_spark(i, trace_start_ms + (trace_end_ms - trace_start_ms) *
                                          ease_in_out_cubic_inverse(start / perimeter));
        start += length[i];
    }
    add_spark(0, trace_end_ms);

    float cx = box.x + box.w / 2.0f;
    float jitter = (hash01(state.seed, 0) - 0.5f) * 120.0f;
    float land = (hash01(state.seed, 1) - 0.5f) * 40.0f;
    float side = hash01(state.seed, 2) < 0.5f ? -1.0f : 1.0f;
    float strike =
        ms < blast_ms ? 1.0f : 1.0f - seg(ms, blast_ms + 60.0f, blast_ms + strike_fade_ms + 60.0f);
    float drop = seg(ms, blast_ms - fall_ms, blast_ms);
    float fork = seg(ms, blast_ms - fall_ms + fork_delay_ms, blast_ms + fork_delay_ms);
    if (drop > 0.0f && strike > 0.0f) {
        frame.bolts.push_back(
            {cx + jitter, -10.0f, cx + land, box.y, drop, strike, state.seed + 11.0f, 22.0f, 1.0f});
    }
    if (fork > 0.0f && strike > 0.0f) {
        frame.bolts.push_back({cx - jitter * 1.6f, -10.0f, cx + side * 90.0f, box.y, fork,
                               0.6f * strike, state.seed + 14.0f, 18.0f, 0.8f});
    }

    float blast = seg(ms, blast_ms, blast_ms + burst_ms);
    if (ms >= blast_ms && blast < 1.0f) {
        frame.bursts.push_back({box, launcher_config::blast_reach, blast, 1.0f});
    }
    return frame;
}

} // namespace astralia
