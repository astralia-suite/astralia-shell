#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ui/text_field.h"

#include "core/animation.h"
#include "ui/geometry.h"
#include "wayland/render/node.h"
#include "wayland/render/texture_cache.h"

#include "service/wayland/input_service.h"

using astralia::kTextFieldPopScaleMs;
using astralia::kTextFieldPopSlideMs;
using astralia::kTextFieldPopSlideOffsetPx;
using astralia::kTextFieldRowSlideMs;
using astralia::kTextFieldTypeAnimMax;
using astralia::text_field_backspace;
using astralia::text_field_idle_toggle;
using astralia::text_field_row_slide;
using astralia::text_field_row_slide_reset;
using astralia::text_field_type_anim_clear;
using astralia::text_field_type_anim_settle;
using astralia::text_field_type_anim_sync;
using astralia::text_field_utf8_len;
using astralia::TextFieldCharAnim;
using astralia::TextFieldResult;
using astralia::TextFieldRowSlide;
using astralia::TextFieldState;
using astralia::TextFieldTypeAnim;

inline TextFieldResult text_field_handle_key(TextFieldState &field, const KeyEvent &event) {
    return astralia::text_field_handle_key(field, to_neutral(event));
}

void draw_text_field_caret(Node *parent, const TextFieldState &field, astralia::ui::Box caret, const float *color, bool active);

void draw_text_field_preedit(Node *parent, TextureCache &tcache, int32_t scale, const std::string &preedit, float x, float center_y, const float *color);

float draw_text_field_value(Node *parent, TextureCache &tcache, int32_t scale, const std::string &text, float x, float center_y, const float *color, const TextFieldTypeAnim *anim);
