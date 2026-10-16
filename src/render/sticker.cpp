#include <algorithm>

#include "render/sticker.h"

namespace astralia {

void sticker(ui::Canvas &canvas, const ui::Box &box, float radius, const Color &fill,
             const Color &outline, float border_width, float shadow,
             std::optional<Color> shadow_color, std::optional<float> shadow_dx) {
    float dx = shadow_dx.value_or(shadow);
    float w = std::max(0.0f, box.w - dx);
    float h = std::max(0.0f, box.h - shadow);
    float r = std::min(radius, std::min(w, h) / 2.0f);
    if (shadow > 0.0f) {
        canvas.rounded({box.x + dx, box.y + shadow, w, h}, r, shadow_color.value_or(outline));
    }
    canvas.rounded({box.x, box.y, w, h}, r, fill, border_width, outline);
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
