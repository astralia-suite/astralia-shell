#include <algorithm>

#include "render/sticker.h"

namespace astralia {

void sticker(ui::Canvas &canvas, const ui::Box &box, float radius, const Color &fill,
             const Color &outline, float border_width, float shadow,
             std::optional<Color> shadow_color, std::optional<float> shadow_dx,
             std::optional<float> ring) {
    float dx = shadow_dx.value_or(0.0f);
    float w = std::max(0.0f, box.w - dx);
    float h = std::max(0.0f, box.h - shadow);
    float r = std::min(radius, std::min(w, h) / 2.0f);
    if (shadow > 0.0f) {
        canvas.rounded({box.x + dx, box.y + shadow, w, h}, r, shadow_color.value_or(outline));
    }
    float band = ring.value_or(border_width >= metrics::border_thick ? metrics::ring_width : 0.0f);
    Color rim = with_alpha(palette::paper, palette::paper.a * outline.a);
    bool same = fill.r == rim.r && fill.g == rim.g && fill.b == rim.b && fill.a == rim.a;
    if (band <= 0.0f || same) {
        canvas.rounded({box.x, box.y, w, h}, r, fill, border_width, outline);
        return;
    }
    canvas.rounded({box.x, box.y, w, h}, r, rim, border_width, outline);
    float in = border_width + band;
    canvas.rounded({box.x + in, box.y + in, std::max(0.0f, w - 2.0f * in),
                    std::max(0.0f, h - 2.0f * in)},
                   std::max(0.0f, r - in), fill);
}

void selectable(ui::Canvas &canvas, const ui::Box &box, float radius, bool selected, bool hovered,
                const Color &idle, float alpha) {
    Color ink = with_alpha(palette::ink, palette::ink.a * alpha);
    if (selected) {
        float s = metrics::shadow_widget;
        sticker(canvas,
                {box.x + s, box.y + s, std::max(0.0f, box.w - s), std::max(0.0f, box.h - s)},
                radius, with_alpha(palette::accent, palette::accent.a * alpha), ink,
                metrics::border_thin, 0.0f);
        return;
    }
    const Color &face = hovered ? palette::pop_a : idle;
    sticker(canvas, box, radius, with_alpha(face, face.a * alpha), ink, metrics::border_thin,
            metrics::shadow_widget);
}

} // namespace astralia
