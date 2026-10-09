#include <algorithm>

#include "ui/field_view.h"

namespace astralia::ui {

namespace {

size_t utf8_char_len(unsigned char lead) {
    if ((lead & 0x80) == 0x00) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

bool settled(const TextFieldTypeAnim &anim) {
    return std::ranges::all_of(anim.chars, [](const TextFieldCharAnim &c) { return c.scale == 1.0f && c.slide_x == 0.0f; });
}

} // namespace

Box draw_field_input(Canvas &canvas, const std::string &display, const TextFieldState &field, const TextFieldTypeAnim &anim, const FieldDraw &draw) {
    float cell = canvas.advance(draw.style);
    float advance = cell * static_cast<float>(text_field_utf8_len(display));
    if (!display.empty()) {
        if (settled(anim)) {
            TextSize size = canvas.measure(display, draw.style);
            canvas.text(display, draw.style, draw.x, draw.center_y - size.h / 2.0f, draw.color);
        } else {
            size_t index = 0;
            for (size_t i = 0; i < display.size(); ++index) {
                size_t len = std::min(utf8_char_len(static_cast<unsigned char>(display[i])), display.size() - i);
                std::string ch = display.substr(i, len);
                i += len;
                float scale = index < anim.chars.size() ? anim.chars[index].scale : 1.0f;
                float slide = index < anim.chars.size() ? anim.chars[index].slide_x : 0.0f;
                if (scale <= 0.0f) {
                    continue;
                }
                TextSize size = canvas.measure(ch, draw.style);
                float center = draw.x + static_cast<float>(index) * cell + cell / 2.0f + slide;
                canvas.begin_group({center - size.w / 2.0f, draw.center_y - size.h / 2.0f, size.w, size.h}, {scale, false});
                canvas.text(ch, draw.style, 0.0f, 0.0f, draw.color);
                canvas.end_group();
            }
        }
    }
    float caret_x = draw.x + advance + draw.caret_gap;
    if (!field.preedit.empty()) {
        TextSize size = canvas.measure(field.preedit, draw.style);
        float y = draw.center_y - size.h / 2.0f;
        canvas.text(field.preedit, draw.style, caret_x, y, draw.color);
        canvas.rect({caret_x, y + size.h, size.w, 1.0f}, draw.color);
    }
    Box caret{caret_x, draw.center_y - draw.caret_height / 2.0f, draw.caret_width, draw.caret_height};
    if (field.cursor_idle_visible) {
        canvas.rect(caret, draw.color);
    }
    return caret;
}

} // namespace astralia::ui
