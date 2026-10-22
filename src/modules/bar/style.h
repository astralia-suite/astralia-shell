#pragma once

#include <optional>
#include <vector>

#include "config/bar_layout.h"

#include "render/tokens.h"

namespace astralia::bar {

struct StyleSpec {
    Color bg;
    Color border;
    float border_width;
    float radius;
    float pad_ratio;
    int top_margin;
    int side_margin;
    float rail_height;
    float island_radius;
    float fillet_radius;
    float shadow = 0.0f;
    bool continuous = false;
    Color shadow_color{};
    float shadow_dx = 0.0f;

    bool has_rail() const { return rail_height > 0.0f; }
    bool has_pill_bg() const { return !has_rail() && !continuous; }
};

BarStyle style_resolve(BarStyle requested, bool islands_supported);
const StyleSpec &style_spec(BarStyle style, bool islands_supported);

struct IslandSpan {
    float left;
    float right;
};

struct IslandShape {
    float outer_x;
    float outer_width;
    float inner_x;
    float inner_width;
};

struct Fillet {
    float edge_x;
    bool right_of_island;
};

struct Frame {
    std::vector<IslandShape> islands;
    std::vector<Fillet> fillets;
};

IslandShape island_shape(const StyleSpec &spec, float left, float right, bool flush_left,
                         bool flush_right);
Frame bar_frame(const StyleSpec &spec, std::optional<float> left_end,
                std::optional<IslandSpan> center, std::optional<float> right_start, float width);

struct Geometry {
    int height;
    int margin_top;
    int exclusive_zone;
};

Geometry autohide_geometry(bool autohide, bool collapsed, int bar_height, int top_margin,
                           int hug_radius);

} // namespace astralia::bar
