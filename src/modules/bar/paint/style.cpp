#include "config/bar_layout.h"

#include "modules/bar/paint/style.h"

namespace astralia::bar {

namespace {

constexpr int autohide_strip_px = 1;
StyleSpec okinami{.border_width = metrics::border_thick,
                  .radius = 0.0f,
                  .pad_ratio = 0.25f,
                  .top_margin = 0,
                  .side_margin = 0,
                  .rail_height = 6.0f,
                  .island_radius = metrics::radius_card,
                  .fillet_radius = metrics::radius_card,
                  .zones = &bar_zones(),
                  .icon_gap = bar_layout::pop_icon_gap};
StyleSpec continuous{.border_width = metrics::border_thick,
                     .radius = bar_layout::pop_radius,
                     .pad_ratio = 0.25f,
                     .top_margin = bar_layout::top_margin,
                     .side_margin = bar_layout::side_margin,
                     .rail_height = 0.0f,
                     .island_radius = 0.0f,
                     .fillet_radius = 0.0f,
                     .shadow = metrics::shadow_offset,
                     .continuous = true,
                     .shadow_dx = 0.0f,
                     .zones = &bar_zones(),
                     .text_lift = metrics::shadow_offset / 2.0f,
                     .uniform_fill = true,
                     .icon_gap = bar_layout::pop_icon_gap};
StyleSpec pop{.border_width = metrics::border_thick,
              .radius = bar_layout::pop_radius,
              .pad_ratio = 0.325f,
              .top_margin = bar_layout::top_margin,
              .side_margin = bar_layout::pop_side_margin,
              .rail_height = 0.0f,
              .island_radius = 0.0f,
              .fillet_radius = 0.0f,
              .shadow = metrics::shadow_offset,
              .shadow_dx = 0.0f,
              .zones = &bar_zones(),
              .text_lift = metrics::shadow_offset / 2.0f,
              .uniform_fill = true,
              .workspace_badges = true,
              .workspace_size = bar_layout::pop_badge,
              .workspace_gap = bar_layout::pop_badge_gap,
              .workspace_scale = bar_layout::pop_badge_active / bar_layout::pop_badge,
              .icon_gap = bar_layout::pop_icon_gap};

const StyleSpec &live(StyleSpec &spec) {
    spec.bg = palette::accent_container;
    spec.border = palette::border_outer;
    spec.shadow_color = palette::shadow;
    return spec;
}

StyleSpec &raw(BarStyle style) {
    switch (style) {
    case BarStyle::okinami:
        return okinami;
    case BarStyle::pop:
        return pop;
    case BarStyle::continuous:
        break;
    }
    return continuous;
}

} // namespace

BarStyle style_resolve(BarStyle requested, bool islands_supported) {
    return raw(requested).has_pill_bg() && !islands_supported ? BarStyle::continuous : requested;
}

const StyleSpec &style_spec(BarStyle style, bool islands_supported) {
    return live(raw(style_resolve(style, islands_supported)));
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
