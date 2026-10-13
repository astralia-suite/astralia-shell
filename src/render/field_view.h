#pragma once

#include <string>

#include "render/canvas.h"
#include "render/text_field.h"

namespace astralia::ui {

struct FieldDraw {
    TextStyle style;
    float x = 0.0f;
    float center_y = 0.0f;
    float caret_height = 0.0f;
    float caret_width = 2.0f;
    float caret_gap = 0.0f;
    Color color = palette::text;
    Color selection = palette::accent_alt_alpha50;
    size_t first_index = 0;
};

struct FieldCursor {
    size_t column = 0;
    size_t selection_from = 0;
    size_t selection_to = 0;
};

Box draw_field_input(Canvas &canvas, const std::string &display, const TextFieldState &field, const TextFieldTypeAnim &anim, const FieldDraw &draw, const FieldCursor *cursor = nullptr);

} // namespace astralia::ui
