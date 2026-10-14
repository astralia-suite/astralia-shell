#pragma once

#include "render/canvas.h"

namespace astralia {

enum class ScrollAxis { horizontal,
                        vertical };

enum class ScrollEntry { from_left,
                         from_right,
                         from_top,
                         from_bottom };

struct ScrollMetrics {
    float roller_w = 18.0f;
    float overhang = 10.0f;
    float cap_h = 8.0f;
    float rounding = 6.0f;
    float band = 4.0f;
    ScrollAxis axis = ScrollAxis::horizontal;
};

struct ScrollGeometry {
    ui::Box paper;
    ui::Box first_roller;
    ui::Box second_roller;
};

struct ScrollOffset {
    float dx = 0.0f;
    float dy = 0.0f;
};

ScrollGeometry scroll_geometry(const ui::Box &panel, float open, const ScrollMetrics &metrics);

ui::Box scroll_bounds(const ScrollGeometry &geometry);

ScrollOffset scroll_slide_offset(ScrollEntry entry, float slide, const ui::Box &closed, float surface_w, float surface_h);

void paint_scroll(ui::Canvas &canvas, const ScrollGeometry &geometry, const ScrollMetrics &metrics);

} // namespace astralia
