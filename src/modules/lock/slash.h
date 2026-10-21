#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "render/geometry.h"

namespace astralia {

using SlashPolygon = std::vector<ui::Point>;

struct SlashStrike {
    ui::Point a;
    ui::Point b;
    float progress = 0.0f;
    float intensity = 0.0f;
    float seed = 0.0f;
    float thick = 1.0f;
    float amp = 0.0f;
    float time_s = 0.0f;
};

struct SlashBeam {
    ui::Point a;
    ui::Point b;
    float width = 0.0f;
    float alpha = 0.0f;
};

struct SlashMark {
    ui::Point a;
    ui::Point b;
    float width = 0.0f;
    float alpha = 0.0f;
};

struct SlashShock {
    ui::Point center;
    float radius = 0.0f;
    float progress = 0.0f;
    float intensity = 0.0f;
    float time_s = 0.0f;
};

struct SlashFrame {
    std::vector<SlashPolygon> desktop;
    std::vector<SlashPolygon> sources;
    bool shattered = false;
    std::vector<SlashMark> marks;
    std::vector<SlashBeam> beams;
    std::vector<SlashStrike> strikes;
    std::vector<SlashPolygon> rays;
    std::vector<SlashPolygon> slivers;
    std::optional<SlashShock> shock;
    float sliver_alpha = 0.0f;
    float flash = 0.0f;
    float dim = 0.0f;
};

struct SlashSliver {
    float along = 0.0f;
    float perp = 0.0f;
    float length = 0.0f;
    float thick = 0.0f;
    float drift_perp = 0.0f;
    float drift_along = 0.0f;
};

struct SlashCut {
    ui::Point center;
    ui::Point direction;
    ui::Point a;
    ui::Point b;
    float start = 0.0f;
    float land = 0.0f;
};

float slash_polygon_area(const SlashPolygon &polygon);
float slash_run_progress(float elapsed_ms);

class SlashPlan {
  public:
    SlashPlan(uint32_t seed, float width, float height);

    float width() const { return width_; }
    float height() const { return height_; }
    const std::vector<SlashCut> &cuts() const { return cuts_; }
    SlashFrame sample(float progress) const;

  private:
    void add_cutmarks(SlashFrame &frame, float progress) const;
    void add_final(SlashFrame &frame, float progress) const;
    void add_slivers(SlashFrame &frame, float progress) const;

    float width_;
    float height_;
    std::vector<SlashCut> cuts_;
    SlashCut final_;
    std::vector<SlashPolygon> rays_;
    std::vector<SlashSliver> slivers_;
    std::vector<SlashPolygon> shards_;
    std::vector<size_t> fall_rank_;
    std::vector<float> shard_spin_;
};

} // namespace astralia
