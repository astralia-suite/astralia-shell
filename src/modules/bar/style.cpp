#include "config/bar_layout.h"

#include "modules/bar/style.h"

namespace astralia::bar {

namespace {

constexpr int autohide_strip_px = 1;
StyleSpec islands{{},
                  {},
                  metrics::border_thick,
                  metrics::radius_card,
                  0.5f,
                  bar_layout::top_margin,
                  bar_layout::side_margin,
                  0.0f,
                  0.0f,
                  0.0f,
                  metrics::shadow_offset,
                  false,
                  {},
                  metrics::shadow_offset};
StyleSpec okinami{{},
                  {},
                  metrics::border_thick,
                  0.0f,
                  0.25f,
                  0,
                  0,
                  6.0f,
                  metrics::radius_card,
                  metrics::radius_card};
StyleSpec continuous{{},
                     {},
                     metrics::border_thick,
                     metrics::radius_card,
                     0.25f,
                     bar_layout::top_margin,
                     bar_layout::side_margin,
                     0.0f,
                     0.0f,
                     0.0f,
                     metrics::shadow_offset,
                     true,
                     {},
                     metrics::shadow_offset};

const StyleSpec &live(StyleSpec &spec) {
    spec.bg = palette::accent_container;
    spec.border = palette::ink;
    spec.shadow_color = palette::shadow;
    return spec;
}

} // namespace

BarStyle style_resolve(BarStyle requested, bool islands_supported) {
    return requested == BarStyle::islands && !islands_supported ? BarStyle::continuous : requested;
}

const StyleSpec &style_spec(BarStyle style, bool islands_supported) {
    switch (style_resolve(style, islands_supported)) {
    case BarStyle::okinami:
        return live(okinami);
    case BarStyle::continuous:
        return live(continuous);
    case BarStyle::islands:
        break;
    }
    return live(islands);
}

IslandShape island_shape(const StyleSpec &spec, float left, float right, bool flush_left,
                         bool flush_right) {
    IslandShape shape{};
    shape.outer_x = flush_left ? left - spec.island_radius : left;
    shape.outer_width = (flush_right ? right + spec.island_radius : right) - shape.outer_x;
    shape.inner_x = flush_left ? shape.outer_x : shape.outer_x + spec.border_width;
    shape.inner_width = shape.outer_x + shape.outer_width -
                        (flush_right ? 0.0f : spec.border_width) - shape.inner_x;
    return shape;
}

Frame bar_frame(const StyleSpec &spec, std::optional<float> left_end,
                std::optional<IslandSpan> center, std::optional<float> right_start, float width) {
    Frame frame;
    if (left_end) {
        frame.islands.push_back(island_shape(spec, 0.0f, *left_end, true, false));
        frame.fillets.push_back({*left_end, true});
    }
    if (center) {
        frame.islands.push_back(island_shape(spec, center->left, center->right, false, false));
        frame.fillets.push_back({center->left, false});
        frame.fillets.push_back({center->right, true});
    }
    if (right_start) {
        frame.islands.push_back(island_shape(spec, *right_start, width, false, true));
        frame.fillets.push_back({*right_start, false});
    }
    return frame;
}

Geometry autohide_geometry(bool autohide, bool collapsed, int bar_height, int top_margin,
                           int hug_radius) {
    if (!autohide) {
        return {bar_height + hug_radius, top_margin, bar_height};
    }
    if (collapsed) {
        return {autohide_strip_px, 0, 0};
    }
    return {top_margin + bar_height + hug_radius, 0, 0};
}

} // namespace astralia::bar
