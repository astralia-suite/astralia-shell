#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "core/animation.h"
#include "core/input.h"

#include "ui/geometry.h"

namespace astralia {

void text_field_backspace(std::string &text);

struct TextFieldState {
    std::string text;
    std::string preedit;
    bool cursor_idle_visible = true;
    ui::Box cursor_rect;
    std::string error_message;
};

enum class TextFieldResult { None,
                             Changed,
                             Committed,
                             Cancelled };

TextFieldResult text_field_handle_key(TextFieldState &field, const input::KeyEvent &event);

size_t text_field_utf8_len(const std::string &text);

bool text_field_idle_toggle(TextFieldState &field);

inline constexpr float kTextFieldPopScaleMs = 200.0f;
inline constexpr float kTextFieldPopSlideMs = 80.0f;
inline constexpr float kTextFieldPopSlideOffsetPx = 8.0f;
inline constexpr float kTextFieldRowSlideMs = 200.0f;
inline constexpr size_t kTextFieldTypeAnimMax = 256;

struct TextFieldCharAnim {
    float scale = 1.0f;
    float slide_x = 0.0f;
};

struct TextFieldTypeAnim {
    std::deque<TextFieldCharAnim> chars;
};

struct TextFieldRowSlide {
    float x = 0.0f;
    float target = 0.0f;
    bool primed = false;
};

void text_field_type_anim_sync(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base, const std::string &text);

void text_field_type_anim_settle(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base, const std::string &text);

void text_field_type_anim_clear(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base);

float text_field_row_slide(TextFieldRowSlide &slide, AnimationManager &am, uint64_t owner, float anchor_x);

void text_field_row_slide_reset(TextFieldRowSlide &slide, AnimationManager &am, uint64_t owner);

} // namespace astralia
