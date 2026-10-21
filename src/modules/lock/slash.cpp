#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "config/lock_config.h"

#include "modules/lock/slash.h"

namespace astralia {

namespace {

struct Rng {
    uint64_t state;

    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    float unit() { return static_cast<float>(next() >> 40) / 16777216.0f; }
    float range(float low, float high) { return low + (high - low) * unit(); }
};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float ease_out_cubic(float t) {
    float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

ui::Point lerp(ui::Point a, ui::Point b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

SlashPolygon clip_half(const SlashPolygon &polygon, ui::Point origin, ui::Point direction,
                       float sign) {
    auto side = [&](ui::Point p) {
        return sign * (direction.x * (p.y - origin.y) - direction.y * (p.x - origin.x));
    };
    SlashPolygon out;
    size_t count = polygon.size();
    for (size_t i = 0; i < count; ++i) {
        ui::Point current = polygon[i];
        ui::Point next = polygon[(i + 1) % count];
        float side_current = side(current);
        float side_next = side(next);
        if (side_current >= 0.0f) {
            out.push_back(current);
        }
        if ((side_current >= 0.0f) != (side_next >= 0.0f)) {
            out.push_back(lerp(current, next, side_current / (side_current - side_next)));
        }
    }
    return out;
}

ui::Point centroid(const SlashPolygon &polygon) {
    ui::Point sum;
    for (const ui::Point &p : polygon) {
        sum.x += p.x;
        sum.y += p.y;
    }
    float n = static_cast<float>(polygon.size());
    return {sum.x / n, sum.y / n};
}

SlashPolygon transformed(const SlashPolygon &polygon, ui::Point middle, ui::Point drift,
                         float scale, float rotation) {
    float c = std::cos(rotation);
    float s = std::sin(rotation);
    SlashPolygon out;
    out.reserve(polygon.size());
    for (const ui::Point &p : polygon) {
        float dx = (p.x - middle.x) * scale;
        float dy = (p.y - middle.y) * scale;
        out.push_back({middle.x + drift.x + dx * c - dy * s, middle.y + drift.y + dx * s + dy * c});
    }
    return out;
}

void clip_axis(float origin, float direction, float low, float high, float &t_min, float &t_max) {
    if (std::fabs(direction) < 1e-6f) {
        return;
    }
    float t0 = (low - origin) / direction;
    float t1 = (high - origin) / direction;
    t_min = std::max(t_min, std::min(t0, t1));
    t_max = std::min(t_max, std::max(t0, t1));
}

SlashCut make_cut(ui::Point center, ui::Point direction, float width, float height, float reach,
                  float start, float land) {
    SlashCut cut;
    cut.center = center;
    cut.direction = direction;
    cut.start = start;
    cut.land = land;

    float diagonal = std::hypot(width, height) + 2.0f * reach;
    float t_min = -diagonal;
    float t_max = diagonal;
    clip_axis(center.x, direction.x, -reach, width + reach, t_min, t_max);
    clip_axis(center.y, direction.y, -reach, height + reach, t_min, t_max);
    cut.a = {center.x + direction.x * t_min, center.y + direction.y * t_min};
    cut.b = {center.x + direction.x * t_max, center.y + direction.y * t_max};
    return cut;
}

SlashStrike strike_of(const SlashCut &cut, float overshoot, float progress, float intensity,
                      float seed, float thick, float amp, float time_s) {
    ui::Point reach{(cut.b.x - cut.a.x) * overshoot, (cut.b.y - cut.a.y) * overshoot};
    return {{cut.a.x - reach.x, cut.a.y - reach.y},
            {cut.b.x + reach.x, cut.b.y + reach.y},
            progress,
            intensity,
            seed,
            thick,
            amp,
            time_s};
}

} // namespace

float slash_polygon_area(const SlashPolygon &polygon) {
    float twice = 0.0f;
    size_t count = polygon.size();
    for (size_t i = 0; i < count; ++i) {
        const ui::Point &a = polygon[i];
        const ui::Point &b = polygon[(i + 1) % count];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::fabs(twice) * 0.5f;
}

float slash_run_progress(float elapsed_ms) {
    return clamp01(std::max(elapsed_ms, 0.0f) / kLockSlashMs);
}

SlashPlan::SlashPlan(uint32_t seed, float width, float height)
    : width_(std::max(width, 0.0f)), height_(std::max(height, 0.0f)) {
    Rng rng{seed};
    float diagonal = std::hypot(width_, height_);
    ui::Point middle{width_ * 0.5f, height_ * 0.5f};

    ui::Point final_direction{1.0f, 0.0f};
    if (diagonal > 0.0f) {
        final_direction = {std::cos(kLockFinalTilt), -std::sin(kLockFinalTilt)};
    }
    final_ = make_cut(middle, final_direction, width_, height_, kLockSlashFinalReach * diagonal,
                      kLockSlashFinalStart, kLockSlashFinalStart + kLockSlashFinalDrawSpan);

    for (int k = 0; k < kLockStarCount; ++k) {
        const LockStarCorner &from = kLockStarCorners[k];
        const LockStarCorner &to = kLockStarCorners[(k + 1) % kLockStarCount];
        ui::Point a{from.x * width_, from.y * height_};
        ui::Point b{to.x * width_, to.y * height_};
        float length = std::hypot(b.x - a.x, b.y - a.y);
        ui::Point direction = length > 0.0f ? ui::Point{(b.x - a.x) / length, (b.y - a.y) / length}
                                            : ui::Point{1.0f, 0.0f};
        float start =
            kLockSlashFirstStart + static_cast<float>(k) * kLockStarAdvanceMs / kLockSlashMs;
        cuts_.push_back(make_cut(lerp(a, b, 0.5f), direction, width_, height_, 0.0f, start,
                                 start + kLockSlashDrawSpan));
    }

    for (const LockFlurrySlash &slash : kLockFlurrySlashes) {
        float angle = slash.degrees * std::numbers::pi_v<float> / 180.0f;
        ui::Point heading{std::cos(angle), std::sin(angle)};
        float x = (0.5f - heading.y * slash.offset * kLockReferenceHeight / kLockReferenceWidth);
        float y = 0.5f + heading.x * slash.offset;
        ui::Point direction = heading;
        if (diagonal > 0.0f) {
            float scaled_x = heading.x / kLockReferenceWidth * width_;
            float scaled_y = heading.y / kLockReferenceHeight * height_;
            float length = std::hypot(scaled_x, scaled_y);
            direction = {scaled_x / length, scaled_y / length};
        }
        float start = kLockSlashFirstStart + slash.start_ms / kLockSlashMs;
        cuts_.push_back(make_cut({x * width_, y * height_}, direction, width_, height_, 0.0f, start,
                                 start + kLockSlashDrawSpan));
    }

    for (int i = 0; i < kLockSlashRayCount; ++i) {
        float angle = (static_cast<float>(i) + rng.range(-0.3f, 0.3f)) * 2.0f *
                      std::numbers::pi_v<float> / static_cast<float>(kLockSlashRayCount);
        rays_.push_back({middle,
                         {middle.x + diagonal * std::cos(angle - kLockSlashRayHalfAngle),
                          middle.y + diagonal * std::sin(angle - kLockSlashRayHalfAngle)},
                         {middle.x + diagonal * std::cos(angle + kLockSlashRayHalfAngle),
                          middle.y + diagonal * std::sin(angle + kLockSlashRayHalfAngle)}});
    }

    for (int i = 0; i < kLockSliverCount; ++i) {
        SlashSliver sliver;
        sliver.along = rng.unit();
        sliver.perp = (rng.unit() - 0.5f) * 2.0f * kLockSliverSpreadFrac * height_;
        sliver.length = rng.range(kLockSliverLengthMin, kLockSliverLengthMax);
        sliver.thick = rng.range(kLockSliverThickMin, kLockSliverThickMax);
        sliver.drift_perp = (rng.unit() - 0.5f) * 2.0f * kLockSliverDriftFrac * height_;
        sliver.drift_along = (rng.unit() - 0.5f) * 2.0f * kLockSliverAlongFrac * diagonal;
        slivers_.push_back(sliver);
    }

    std::vector<const SlashCut *> lines;
    for (const SlashCut &cut : cuts_) {
        lines.push_back(&cut);
    }
    lines.push_back(&final_);
    shards_ = {{{0.0f, 0.0f}, {width_, 0.0f}, {width_, height_}, {0.0f, height_}}};
    for (const SlashCut *line : lines) {
        std::vector<SlashPolygon> next;
        for (const SlashPolygon &piece : shards_) {
            for (float sign : {1.0f, -1.0f}) {
                SlashPolygon half = clip_half(piece, line->center, line->direction, sign);
                if (half.size() >= 3 && slash_polygon_area(half) > kLockSlashMinPieceArea) {
                    next.push_back(std::move(half));
                }
            }
        }
        shards_ = std::move(next);
    }

    std::vector<std::pair<float, size_t>> keyed;
    for (size_t i = 0; i < shards_.size(); ++i) {
        ui::Point at = centroid(shards_[i]);
        float distance = std::hypot(at.x - middle.x, at.y - middle.y);
        keyed.emplace_back(distance * (1.0f + kLockSlashShardRankNoise * rng.unit()), i);
    }
    std::ranges::sort(keyed);
    fall_rank_.resize(shards_.size());
    shard_spin_.resize(shards_.size());
    for (size_t position = 0; position < keyed.size(); ++position) {
        fall_rank_[keyed[position].second] = position;
    }
    for (float &spin : shard_spin_) {
        spin = (rng.unit() < 0.5f ? -1.0f : 1.0f) * rng.range(0.5f, 1.0f) * kLockSlashShardSpin;
    }
}

void SlashPlan::add_cutmarks(SlashFrame &frame, float progress) const {
    float time_s = progress * kLockSlashMs * 0.001f;
    for (size_t k = 0; k < cuts_.size(); ++k) {
        const SlashCut &cut = cuts_[k];
        if (progress < cut.start) {
            continue;
        }
        float t = clamp01((progress - cut.start) / (cut.land - cut.start));
        float s = ease_out_cubic(t);
        float reveal = std::min(2.0f * s, 1.0f);
        if (progress < final_.land) {
            frame.marks.push_back(
                {cut.a, lerp(cut.a, cut.b, reveal), kLockCutmarkWidth, kLockCutmarkAlpha});
        }
        if (t < 1.0f && s > 0.002f) {
            float pulse = 4.0f * s * (1.0f - s);
            frame.beams.push_back({cut.a, lerp(cut.a, cut.b, reveal),
                                   kLockBeamPeakFrac * height_ * pulse,
                                   std::min(pulse * kLockStrikePeak, 1.0f)});
            frame.strikes.push_back(strike_of(
                cut, kLockSlashStrikeOvershoot, reveal, 4.0f * s * (1.0f - s) * kLockStrikePeak,
                static_cast<float>(k) * 1.7f + 1.0f, 1.0f, kLockStrikeAmp, time_s));
        }
    }
}

void SlashPlan::add_final(SlashFrame &frame, float progress) const {
    if (progress < final_.start) {
        return;
    }
    float time_s = progress * kLockSlashMs * 0.001f;
    float peak = kLockFinalBeamFrac * height_;
    float thin = kLockFinalThinFrac * height_;
    float sweep = 1.0f;
    float intensity = kLockFinalIntensity;
    float beam_width = 0.0f;
    float beam_alpha = 0.0f;
    bool strike = true;
    if (progress < final_.land) {
        sweep = clamp01((progress - final_.start) / (final_.land - final_.start));
        beam_width = thin + (peak - thin) * sweep;
        beam_alpha = 1.0f;
    } else {
        float since = progress - final_.land;
        float u = since / kLockSlashFinalBeamSpan;
        if (u < 1.0f) {
            beam_width = peak + (thin - peak) * ease_out_cubic(u);
            beam_alpha = 1.0f - clamp01((u - 0.4f) / 0.6f);
        }
        float fade = since / kLockSlashFinalFade;
        strike = fade < 1.0f;
        float crackle = 0.55f + 0.45f * std::sin(time_s * 71.0f) * std::sin(time_s * 127.0f);
        intensity = (1.0f - fade) * (1.0f - fade) * kLockFinalIntensity * crackle;
    }
    if (beam_alpha > 0.0f) {
        frame.beams.push_back({final_.a, lerp(final_.a, final_.b, sweep), beam_width, beam_alpha});
    }
    if (strike) {
        frame.strikes.push_back(strike_of(final_, 0.0f, sweep, intensity, kLockFinalSeed,
                                          kLockFinalThick, kLockStrikeAmp * kLockFinalAmpMult,
                                          time_s));
    }
}

void SlashPlan::add_slivers(SlashFrame &frame, float progress) const {
    if (progress < final_.land || progress >= final_.land + kLockSlashSliverSpan) {
        return;
    }
    float u = (progress - final_.land) / kLockSlashSliverSpan;
    ui::Point along{final_.b.x - final_.a.x, final_.b.y - final_.a.y};
    float length = std::hypot(along.x, along.y);
    if (length < 1e-3f) {
        return;
    }
    along = {along.x / length, along.y / length};
    ui::Point normal{-along.y, along.x};
    for (const SlashSliver &sliver : slivers_) {
        ui::Point at = lerp(final_.a, final_.b, sliver.along);
        float shift = sliver.drift_along * u;
        float lift = sliver.perp + sliver.drift_perp * u;
        ui::Point center{at.x + along.x * shift + normal.x * lift,
                         at.y + along.y * shift + normal.y * lift};
        float half_length = sliver.length * 0.5f;
        float half_thick = sliver.thick * 0.5f;
        frame.slivers.push_back({{center.x - along.x * half_length - normal.x * half_thick,
                                  center.y - along.y * half_length - normal.y * half_thick},
                                 {center.x + along.x * half_length - normal.x * half_thick,
                                  center.y + along.y * half_length - normal.y * half_thick},
                                 {center.x + along.x * half_length + normal.x * half_thick,
                                  center.y + along.y * half_length + normal.y * half_thick},
                                 {center.x - along.x * half_length + normal.x * half_thick,
                                  center.y - along.y * half_length + normal.y * half_thick}});
    }
    frame.sliver_alpha = 1.0f - u;
}

SlashFrame SlashPlan::sample(float progress) const {
    float p = clamp01(progress);
    SlashFrame frame;

    if (p < final_.land) {
        if (width_ * height_ > kLockSlashMinPieceArea) {
            frame.desktop.push_back(
                {{0.0f, 0.0f}, {width_, 0.0f}, {width_, height_}, {0.0f, height_}});
            frame.sources.push_back(frame.desktop.back());
        }
    } else {
        frame.shattered = true;
        float fall_begin = final_.land + kLockSlashShatterHold;
        ui::Point middle{width_ * 0.5f, height_ * 0.5f};
        float reach = kLockSlashShardDrift * std::hypot(width_, height_);
        for (size_t i = 0; i < shards_.size(); ++i) {
            float share = shards_.size() > 1 ? static_cast<float>(fall_rank_[i]) /
                                                   static_cast<float>(shards_.size() - 1)
                                             : 0.0f;
            float begin = fall_begin + share * kLockSlashShatterSpread;
            float local = clamp01((p - begin) / kLockSlashFallSpan);
            float eased = local * local;
            float scale = (1.0f - kLockSlashGapFrac) * (1.0f - eased);
            if (scale <= 0.001f) {
                continue;
            }
            ui::Point at = centroid(shards_[i]);
            ui::Point away{at.x - middle.x, at.y - middle.y};
            float length = std::hypot(away.x, away.y);
            away = length > 1e-3f ? ui::Point{away.x / length, away.y / length}
                                  : ui::Point{0.0f, -1.0f};
            frame.sources.push_back(shards_[i]);
            frame.desktop.push_back(transformed(shards_[i], at,
                                                {away.x * reach * eased, away.y * reach * eased},
                                                scale, shard_spin_[i] * eased));
        }
    }

    add_cutmarks(frame, p);
    add_final(frame, p);
    add_slivers(frame, p);

    frame.dim = kLockDimPeak * ease_out_cubic(clamp01(p / kLockSlashDimRamp));
    if (p >= final_.land) {
        float lift = clamp01((p - final_.land) / kLockSlashDimLift);
        frame.dim *= 1.0f - ease_out_cubic(lift);
    }

    if (p >= final_.land && p < final_.land + kLockSlashFlashSpan) {
        float since = p - final_.land;
        if (since < kLockSlashFlashHold) {
            frame.flash = 1.0f;
        } else {
            float fade =
                1.0f - (since - kLockSlashFlashHold) / (kLockSlashFlashSpan - kLockSlashFlashHold);
            frame.flash = fade * fade;
        }
    }
    if (p >= final_.land && p < final_.land + kLockSlashImpactSpan) {
        frame.rays = rays_;
    }
    if (p >= final_.land && p < final_.land + kLockSlashShockSpan) {
        frame.shock = SlashShock{{width_ * 0.5f, height_ * 0.5f},
                                 kLockShockRadiusFrac * height_,
                                 (p - final_.land) / kLockSlashShockSpan,
                                 kLockShockIntensity,
                                 p * kLockSlashMs * 0.001f};
    }
    return frame;
}

} // namespace astralia
