#include "config/bar_layout.h"

#include "modules/bar/style.h"

namespace astralia {

namespace {

constexpr int autohide_strip_px = 1;
BarStyleSpec islands{{}, {}, metrics::border_thick, metrics::radius_card, 0.5f, bar_layout::top_margin, bar_layout::side_margin, 0.0f, 0.0f, 0.0f, metrics::shadow_offset, false, {}, metrics::shadow_offset};
BarStyleSpec okinami{{}, {}, metrics::border_thick, 0.0f, 0.25f, 0, 0, 6.0f, metrics::radius_card, metrics::radius_card};
BarStyleSpec continuous{{}, {}, metrics::border_thick, metrics::radius_card, 0.25f, bar_layout::top_margin, bar_layout::side_margin, 0.0f, 0.0f, 0.0f, metrics::shadow_offset, true, {}, metrics::shadow_offset};

const BarStyleSpec &live(BarStyleSpec &spec) {
    spec.bg = palette::accent_container;
    spec.border = palette::ink;
    spec.shadow_color = palette::shadow;
    return spec;
}

} // namespace

BarStyle bar_style_resolve(BarStyle requested, bool islands_supported) {
    return requested == BarStyle::islands && !islands_supported ? BarStyle::continuous : requested;
}

const BarStyleSpec &bar_style_spec(BarStyle style, bool islands_supported) {
    switch (bar_style_resolve(style, islands_supported)) {
    case BarStyle::okinami:
        return live(okinami);
    case BarStyle::continuous:
        return live(continuous);
    case BarStyle::islands:
        break;
    }
    return live(islands);
}

IslandShape bar_island_shape(const BarStyleSpec &spec, float left, float right, bool flush_left, bool flush_right) {
    IslandShape shape{};
    shape.outer_x = flush_left ? left - spec.island_radius : left;
    shape.outer_width = (flush_right ? right + spec.island_radius : right) - shape.outer_x;
    shape.inner_x = flush_left ? shape.outer_x : shape.outer_x + spec.border_width;
    shape.inner_width = shape.outer_x + shape.outer_width - (flush_right ? 0.0f : spec.border_width) - shape.inner_x;
    return shape;
}

BarFrame bar_frame(const BarStyleSpec &spec, std::optional<float> left_end, std::optional<IslandSpan> center, std::optional<float> right_start, float width) {
    BarFrame frame;
    if (left_end) {
        frame.islands.push_back(bar_island_shape(spec, 0.0f, *left_end, true, false));
        frame.fillets.push_back({*left_end, true});
    }
    if (center) {
        frame.islands.push_back(bar_island_shape(spec, center->left, center->right, false, false));
        frame.fillets.push_back({center->left, false});
        frame.fillets.push_back({center->right, true});
    }
    if (right_start) {
        frame.islands.push_back(bar_island_shape(spec, *right_start, width, false, true));
        frame.fillets.push_back({*right_start, false});
    }
    return frame;
}

BarGeometry bar_autohide_geometry(bool autohide, bool collapsed, int bar_height, int top_margin, int hug_radius) {
    if (!autohide) {
        return {bar_height + hug_radius, top_margin, bar_height};
    }
    if (collapsed) {
        return {autohide_strip_px, 0, 0};
    }
    return {top_margin + bar_height + hug_radius, 0, 0};
}

} // namespace astralia
