#include <algorithm>

#include "render/sticker.h"

namespace astralia {

void sticker(ui::Canvas &canvas, const ui::Box &box, float radius, const Color &fill, const Color &outline, float border_width, float shadow) {
    float w = std::max(0.0f, box.w - shadow);
    float h = std::max(0.0f, box.h - shadow);
    float r = std::min(radius, std::min(w, h) / 2.0f);
    if (shadow > 0.0f) {
        canvas.rounded({box.x + shadow, box.y + shadow, w, h}, r, outline);
    }
    canvas.rounded({box.x, box.y, w, h}, r, fill, border_width, outline);
}

} // namespace astralia
