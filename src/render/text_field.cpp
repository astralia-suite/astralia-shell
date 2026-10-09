#include <algorithm>

#include "render/text_field.h"

namespace astralia {

namespace {

uint64_t type_anim_owner(uint64_t base, size_t index, uint64_t prop) {
    return base + (index % kTextFieldTypeAnimMax) * 2 + prop;
}

void type_anim_push(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base) {
    size_t idx = anim.chars.size();
    anim.chars.push_back({0.0f, kTextFieldPopSlideOffsetPx});
    am.animate(0.0f, 1.0f, kTextFieldPopScaleMs, Easing::EaseOutBack, [&anim, idx](float v) {
            if (idx < anim.chars.size())
                anim.chars[idx].scale = v; }, {}, type_anim_owner(owner_base, idx, 0));
    am.animate(kTextFieldPopSlideOffsetPx, 0.0f, kTextFieldPopSlideMs, Easing::Linear, [&anim, idx](float v) {
            if (idx < anim.chars.size())
                anim.chars[idx].slide_x = v; }, {}, type_anim_owner(owner_base, idx, 1));
}

void type_anim_pop(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base) {
    if (anim.chars.empty())
        return;
    size_t idx = anim.chars.size() - 1;
    am.cancelForOwner(type_anim_owner(owner_base, idx, 0));
    am.cancelForOwner(type_anim_owner(owner_base, idx, 1));
    anim.chars.pop_back();
}

} // namespace

void text_field_backspace(std::string &text) {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

TextFieldResult text_field_handle_key(TextFieldState &field, const input::KeyEvent &event) {
    switch (event.kind) {
    case input::KeyKind::Text:
        field.text += event.text;
        field.preedit.clear();
        field.cursor_idle_visible = true;
        field.error_message.clear();
        return TextFieldResult::Changed;
    case input::KeyKind::Preedit:
        field.preedit = event.text;
        field.cursor_idle_visible = true;
        field.error_message.clear();
        return TextFieldResult::Changed;
    case input::KeyKind::Backspace:
        text_field_backspace(field.text);
        field.preedit.clear();
        field.cursor_idle_visible = true;
        field.error_message.clear();
        return TextFieldResult::Changed;
    case input::KeyKind::Enter:
        field.preedit.clear();
        return TextFieldResult::Committed;
    case input::KeyKind::Escape:
        field.preedit.clear();
        return TextFieldResult::Cancelled;
    default:
        return TextFieldResult::None;
    }
}

size_t text_field_utf8_len(const std::string &text) {
    size_t n = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

bool text_field_idle_toggle(TextFieldState &field) {
    field.cursor_idle_visible = !field.cursor_idle_visible;
    return true;
}

void text_field_type_anim_sync(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base, const std::string &text) {
    size_t target = text_field_utf8_len(text);
    while (anim.chars.size() > target)
        type_anim_pop(anim, am, owner_base);
    while (anim.chars.size() < target)
        type_anim_push(anim, am, owner_base);
}

void text_field_type_anim_settle(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base, const std::string &text) {
    text_field_type_anim_clear(anim, am, owner_base);
    size_t n = text_field_utf8_len(text);
    for (size_t i = 0; i < n; ++i)
        anim.chars.push_back({1.0f, 0.0f});
}

void text_field_type_anim_clear(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base) {
    while (!anim.chars.empty())
        type_anim_pop(anim, am, owner_base);
}

float text_field_row_slide(TextFieldRowSlide &slide, AnimationManager &am, uint64_t owner, float anchor_x) {
    if (!slide.primed) {
        slide.primed = true;
        slide.target = anchor_x;
        slide.x = anchor_x;
        return slide.x;
    }
    if (anchor_x != slide.target) {
        slide.target = anchor_x;
        am.cancelForOwner(owner);
        am.animate(slide.x, anchor_x, kTextFieldRowSlideMs, Easing::EaseOutCubic, [&slide](float v) { slide.x = v; }, {}, owner);
    }
    return slide.x;
}

void text_field_row_slide_reset(TextFieldRowSlide &slide, AnimationManager &am, uint64_t owner) {
    am.cancelForOwner(owner);
    slide.primed = false;
    slide.x = 0.0f;
    slide.target = 0.0f;
}

} // namespace astralia
