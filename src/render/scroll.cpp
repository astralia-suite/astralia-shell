#include <algorithm>

#include "render/scroll.h"
#include "render/sticker.h"

namespace astralia {

namespace {

constexpr float cap_flare = 2.0f;
constexpr float highlight_mix = 0.35f;

void paint_roller(ui::Canvas &canvas, const ui::Box &roller, const ScrollMetrics &metrics) {
    float body_w = roller.w;
    float body_h = std::max(0.0f, roller.h - metrics::shadow_offset);
    sticker(canvas, roller, metrics.roller_w * 0.35f, palette::accent);
    Color highlight = lerp_color(palette::accent, palette::white, highlight_mix);
    if (metrics.axis == ScrollAxis::horizontal) {
        float cap_h = std::min(metrics.cap_h, body_h / 2.0f);
        float cap_w = body_w + cap_flare * 2.0f;
        float cap_x = roller.x - cap_flare;
        sticker(canvas, {cap_x, roller.y, cap_w, cap_h}, cap_h / 2.0f, palette::accent_alt,
                palette::border_outer, metrics::border_thin, 0.0f);
        sticker(canvas, {cap_x, roller.y + body_h - cap_h, cap_w, cap_h}, cap_h / 2.0f,
                palette::accent_alt, palette::border_outer, metrics::border_thin, 0.0f);
        float strip_h = body_h - cap_h * 2.0f - 4.0f;
        if (strip_h > 0.0f) {
            canvas.rounded(
                {roller.x + body_w * 0.22f, roller.y + cap_h + 2.0f, body_w * 0.18f, strip_h},
                body_w * 0.09f, highlight);
        }
        return;
    }
    float cap_w = std::min(metrics.cap_h, body_w / 2.0f);
    float cap_h = body_h + cap_flare * 2.0f;
    float cap_y = roller.y - cap_flare;
    sticker(canvas, {roller.x, cap_y, cap_w, cap_h}, cap_w / 2.0f, palette::accent_alt,
            palette::border_outer, metrics::border_thin, 0.0f);
    sticker(canvas, {roller.x + body_w - cap_w, cap_y, cap_w, cap_h}, cap_w / 2.0f,
            palette::accent_alt, palette::border_outer, metrics::border_thin, 0.0f);
    float strip_w = body_w - cap_w * 2.0f - 4.0f;
    if (strip_w > 0.0f) {
        canvas.rounded(
            {roller.x + cap_w + 2.0f, roller.y + body_h * 0.22f, strip_w, body_h * 0.18f},
            body_h * 0.09f, highlight);
    }
}

} // namespace

ScrollGeometry scroll_geometry(const ui::Box &panel, float open, const ScrollMetrics &metrics) {
    float t = std::clamp(open, 0.0f, 1.0f);
    float gap = metrics.gap * t;
    ScrollGeometry g;
    if (metrics.axis == ScrollAxis::vertical) {
        float paper_h = panel.h * t;
        float left = panel.x - metrics.overhang;
        float width = panel.w + metrics.overhang * 2.0f;
        g.paper = {panel.x, panel.y, panel.w, paper_h};
        g.first_roller = {left, panel.y - metrics.roller_w - gap, width, metrics.roller_w};
        g.second_roller = {left, panel.y + paper_h + gap, width, metrics.roller_w};
        return g;
    }
    float cx = panel.x + panel.w / 2.0f;
    float paper_w = panel.w * t;
    float top = panel.y - metrics.overhang;
    float height = panel.h + metrics.overhang * 2.0f;
    g.paper = {cx - paper_w / 2.0f, panel.y, paper_w, panel.h};
    g.first_roller = {g.paper.x - metrics.roller_w - gap, top, metrics.roller_w, height};
    g.second_roller = {g.paper.x + g.paper.w + gap, top, metrics.roller_w, height};
    return g;
}

ui::Box scroll_bounds(const ScrollGeometry &geometry) {
    const ui::Box &a = geometry.first_roller;
    const ui::Box &b = geometry.second_roller;
    float left = std::min(a.x, b.x);
    float top = std::min(a.y, b.y);
    float right = std::max(a.x + a.w, b.x + b.w);
    float bottom = std::max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

ScrollOffset scroll_slide_offset(ScrollEntry entry, float slide, const ui::Box &closed,
                                 float surface_w, float surface_h) {
    switch (entry) {
    case ScrollEntry::from_left:
        return {-(closed.x + closed.w) * slide, 0.0f};
    case ScrollEntry::from_right:
        return {(surface_w - closed.x) * slide, 0.0f};
    case ScrollEntry::from_top:
        return {0.0f, -(closed.y + closed.h) * slide};
    case ScrollEntry::from_bottom:
        return {0.0f, (surface_h - closed.y) * slide};
    }
    return {};
}

void paint_scroll(ui::Canvas &canvas, const ScrollGeometry &geometry,
                  const ScrollMetrics &metrics) {
    const ui::Box &p = geometry.paper;
    if (std::min(p.w, p.h) >= metrics::border_thick * 2.0f) {
        sticker(canvas, p, metrics.rounding, palette::accent_container);
        ui::Box inner{p.x + metrics.band, p.y + metrics.band, p.w - metrics.band * 2.0f,
                      p.h - metrics::shadow_offset - metrics.band * 2.0f};
        if (inner.w > 0.0f && inner.h > 0.0f) {
            sticker(canvas, inner, std::max(0.0f, metrics.rounding - 2.0f), palette::paper,
                    palette::border_outer, metrics::border_thin, 0.0f, std::nullopt, std::nullopt,
                    0.0f);
        }
    }
    paint_roller(canvas, geometry.first_roller, metrics);
    paint_roller(canvas, geometry.second_roller, metrics);
}

} // namespace astralia
