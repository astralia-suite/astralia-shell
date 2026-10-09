#pragma once

#include <optional>
#include <vector>

#include "config/bar_layout.h"

#include "render/tokens.h"

namespace astralia {

struct BarStyleSpec {
    Color bg;
    Color border;
    float border_width;
    float radius_ratio;
    float pad_ratio;
    int top_margin;
    int side_margin;
    float rail_height;
    float island_radius;
    float fillet_radius;
    bool continuous = false;

    bool has_rail() const { return rail_height > 0.0f; }
    bool has_pill_bg() const { return !has_rail() && !continuous; }
};

BarStyle bar_style_resolve(BarStyle requested, bool islands_supported);
const BarStyleSpec &bar_style_spec(BarStyle style, bool islands_supported);

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

struct BarFrame {
    std::vector<IslandShape> islands;
    std::vector<Fillet> fillets;
};

IslandShape bar_island_shape(const BarStyleSpec &spec, float left, float right, bool flush_left, bool flush_right);
BarFrame bar_frame(const BarStyleSpec &spec, std::optional<float> left_end, std::optional<IslandSpan> center, std::optional<float> right_start, float width);

struct BarGeometry {
    int height;
    int margin_top;
    int exclusive_zone;
};

BarGeometry bar_autohide_geometry(bool autohide, bool collapsed, int bar_height, int top_margin, int hug_radius);

} // namespace astralia
