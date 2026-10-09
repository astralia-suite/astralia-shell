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
};

Box draw_field_input(Canvas &canvas, const std::string &display, const TextFieldState &field, const TextFieldTypeAnim &anim, const FieldDraw &draw);

} // namespace astralia::ui
