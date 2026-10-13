#include <algorithm>

#include "render/field_view.h"

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

Box draw_field_input(Canvas &canvas, const std::string &display, const TextFieldState &field, const TextFieldTypeAnim &anim, const FieldDraw &draw, const FieldCursor *cursor) {
    float cell = canvas.advance(draw.style);
    size_t length = text_field_utf8_len(display);
    size_t column = cursor ? std::min(cursor->column, length) : length;
    float preedit_w = field.preedit.empty() ? 0.0f : canvas.measure(field.preedit, draw.style).w;
    if (cursor && cursor->selection_to > cursor->selection_from) {
        size_t from = std::min(cursor->selection_from, length);
        size_t to = std::min(cursor->selection_to, length);
        float shift = preedit_w > 0.0f && from >= column ? preedit_w : 0.0f;
        canvas.rect({draw.x + static_cast<float>(from) * cell + shift, draw.center_y - draw.caret_height / 2.0f, static_cast<float>(to - from) * cell, draw.caret_height}, draw.selection);
    }
    if (!display.empty()) {
        if (settled(anim) && preedit_w <= 0.0f) {
            TextSize size = canvas.measure(display, draw.style);
            canvas.text(display, draw.style, draw.x, draw.center_y - size.h / 2.0f, draw.color);
        } else if (settled(anim)) {
            size_t split = text_field_byte_offset(display, column);
            std::string head = display.substr(0, split);
            std::string tail = display.substr(split);
            if (!head.empty()) {
                TextSize size = canvas.measure(head, draw.style);
                canvas.text(head, draw.style, draw.x, draw.center_y - size.h / 2.0f, draw.color);
            }
            if (!tail.empty()) {
                TextSize size = canvas.measure(tail, draw.style);
                canvas.text(tail, draw.style, draw.x + static_cast<float>(column) * cell + preedit_w, draw.center_y - size.h / 2.0f, draw.color);
            }
        } else {
            size_t index = 0;
            for (size_t i = 0; i < display.size(); ++index) {
                size_t len = std::min(utf8_char_len(static_cast<unsigned char>(display[i])), display.size() - i);
                std::string ch = display.substr(i, len);
                i += len;
                size_t anim_index = draw.first_index + index;
                float scale = anim_index < anim.chars.size() ? anim.chars[anim_index].scale : 1.0f;
                float slide = anim_index < anim.chars.size() ? anim.chars[anim_index].slide_x : 0.0f;
                if (scale <= 0.0f) {
                    continue;
                }
                TextSize size = canvas.measure(ch, draw.style);
                float shift = index >= column ? preedit_w : 0.0f;
                float center = draw.x + static_cast<float>(index) * cell + cell / 2.0f + slide + shift;
                canvas.begin_group({center - size.w / 2.0f, draw.center_y - size.h / 2.0f, size.w, size.h}, {scale, false});
                canvas.text(ch, draw.style, 0.0f, 0.0f, draw.color);
                canvas.end_group();
            }
        }
    }
    float caret_x = draw.x + cell * static_cast<float>(column) + draw.caret_gap;
    if (!field.preedit.empty()) {
        TextSize size = canvas.measure(field.preedit, draw.style);
        float y = draw.center_y - size.h / 2.0f;
        canvas.text(field.preedit, draw.style, caret_x, y, draw.color);
        canvas.rect({caret_x, y + size.h, size.w, 1.0f}, draw.color);
        if (cursor) {
            caret_x += size.w;
        }
    }
    Box caret{caret_x, draw.center_y - draw.caret_height / 2.0f, draw.caret_width, draw.caret_height};
    if (field.cursor_idle_visible) {
        canvas.rect(caret, draw.color);
    }
    return caret;
}

} // namespace astralia::ui
