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
    am.animate(
        0.0f, 1.0f, kTextFieldPopScaleMs, Easing::EaseOutBack,
        [&anim, idx](float v) {
            if (idx < anim.chars.size())
                anim.chars[idx].scale = v;
        },
        {}, type_anim_owner(owner_base, idx, 0));
    am.animate(
        kTextFieldPopSlideOffsetPx, 0.0f, kTextFieldPopSlideMs, Easing::Linear,
        [&anim, idx](float v) {
            if (idx < anim.chars.size())
                anim.chars[idx].slide_x = v;
        },
        {}, type_anim_owner(owner_base, idx, 1));
}

void type_anim_pop(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base) {
    if (anim.chars.empty())
        return;
    size_t idx = anim.chars.size() - 1;
    am.cancelForOwner(type_anim_owner(owner_base, idx, 0));
    am.cancelForOwner(type_anim_owner(owner_base, idx, 1));
    anim.chars.pop_back();
}

bool is_continuation(const std::string &text, size_t i) {
    return (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80;
}

bool is_word_byte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           c >= 0x80;
}

size_t prev_boundary(const std::string &text, size_t i) {
    if (i == 0)
        return 0;
    --i;
    while (i > 0 && is_continuation(text, i))
        --i;
    return i;
}

size_t next_boundary(const std::string &text, size_t i) {
    if (i >= text.size())
        return text.size();
    ++i;
    while (i < text.size() && is_continuation(text, i))
        ++i;
    return i;
}

size_t prev_word(const std::string &text, size_t i) {
    while (i > 0 && !is_word_byte(static_cast<unsigned char>(text[i - 1])))
        --i;
    while (i > 0 && is_word_byte(static_cast<unsigned char>(text[i - 1])))
        --i;
    return i;
}

size_t next_word(const std::string &text, size_t i) {
    while (i < text.size() && !is_word_byte(static_cast<unsigned char>(text[i])))
        ++i;
    while (i < text.size() && is_word_byte(static_cast<unsigned char>(text[i])))
        ++i;
    return i;
}

void move_to(TextCursor &cursor, size_t pos, bool extend) {
    cursor.pos = pos;
    if (!extend)
        cursor.anchor = pos;
}

} // namespace

void text_field_backspace(std::string &text) {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

void text_cursor_clamp(const std::string &text, TextCursor &cursor) {
    cursor.pos = std::min(cursor.pos, text.size());
    cursor.anchor = std::min(cursor.anchor, text.size());
}

void text_cursor_move(const std::string &text, TextCursor &cursor, CursorDir dir, bool extend) {
    text_cursor_clamp(text, cursor);
    if (!extend && cursor.has_selection()) {
        move_to(cursor, dir == CursorDir::Left ? cursor.begin() : cursor.end(), false);
        return;
    }
    move_to(cursor,
            dir == CursorDir::Left ? prev_boundary(text, cursor.pos)
                                   : next_boundary(text, cursor.pos),
            extend);
}

void text_cursor_move_word(const std::string &text, TextCursor &cursor, CursorDir dir,
                           bool extend) {
    text_cursor_clamp(text, cursor);
    move_to(cursor,
            dir == CursorDir::Left ? prev_word(text, cursor.pos) : next_word(text, cursor.pos),
            extend);
}

void text_cursor_home(TextCursor &cursor, bool extend) { move_to(cursor, 0, extend); }

void text_cursor_end(const std::string &text, TextCursor &cursor, bool extend) {
    move_to(cursor, text.size(), extend);
}

void text_cursor_select_all(const std::string &text, TextCursor &cursor) {
    cursor.anchor = 0;
    cursor.pos = text.size();
}

void text_cursor_select_word(const std::string &text, TextCursor &cursor, size_t byte) {
    if (text.empty()) {
        cursor = {};
        return;
    }
    size_t at = std::min(byte, text.size() - 1);
    bool word = is_word_byte(static_cast<unsigned char>(text[at]));
    size_t from = at;
    while (from > 0 && is_word_byte(static_cast<unsigned char>(text[from - 1])) == word)
        --from;
    size_t to = at + 1;
    while (to < text.size() && is_word_byte(static_cast<unsigned char>(text[to])) == word)
        ++to;
    cursor.anchor = from;
    cursor.pos = to;
}

void text_cursor_set(const std::string &text, TextCursor &cursor, size_t byte, bool extend) {
    size_t pos = std::min(byte, text.size());
    while (pos > 0 && pos < text.size() && is_continuation(text, pos))
        --pos;
    move_to(cursor, pos, extend);
}

bool text_cursor_delete_selection(std::string &text, TextCursor &cursor) {
    text_cursor_clamp(text, cursor);
    if (!cursor.has_selection())
        return false;
    text.erase(cursor.begin(), cursor.end() - cursor.begin());
    move_to(cursor, cursor.begin(), false);
    return true;
}

bool text_cursor_insert(std::string &text, TextCursor &cursor, const std::string &inserted) {
    bool removed = text_cursor_delete_selection(text, cursor);
    if (inserted.empty())
        return removed;
    text.insert(cursor.pos, inserted);
    move_to(cursor, cursor.pos + inserted.size(), false);
    return true;
}

bool text_cursor_backspace(std::string &text, TextCursor &cursor) {
    if (text_cursor_delete_selection(text, cursor))
        return true;
    if (cursor.pos == 0)
        return false;
    size_t from = prev_boundary(text, cursor.pos);
    text.erase(from, cursor.pos - from);
    move_to(cursor, from, false);
    return true;
}

bool text_cursor_delete(std::string &text, TextCursor &cursor) {
    if (text_cursor_delete_selection(text, cursor))
        return true;
    if (cursor.pos >= text.size())
        return false;
    text.erase(cursor.pos, next_boundary(text, cursor.pos) - cursor.pos);
    return true;
}

std::string text_cursor_selected_text(const std::string &text, const TextCursor &cursor) {
    size_t begin = std::min(cursor.begin(), text.size());
    size_t end = std::min(cursor.end(), text.size());
    return text.substr(begin, end - begin);
}

size_t text_field_byte_offset(const std::string &text, size_t char_index) {
    size_t i = 0;
    for (size_t n = 0; n < char_index && i < text.size(); ++n)
        i = next_boundary(text, i);
    return i;
}

size_t text_field_char_index(const std::string &text, size_t byte) {
    size_t n = 0;
    for (size_t i = 0; i < std::min(byte, text.size()); ++i)
        if (!is_continuation(text, i))
            ++n;
    return n;
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

void text_field_type_anim_sync(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base,
                               const std::string &text) {
    size_t target = text_field_utf8_len(text);
    while (anim.chars.size() > target)
        type_anim_pop(anim, am, owner_base);
    while (anim.chars.size() < target)
        type_anim_push(anim, am, owner_base);
}

void text_field_type_anim_settle(TextFieldTypeAnim &anim, AnimationManager &am, uint64_t owner_base,
                                 const std::string &text) {
    text_field_type_anim_clear(anim, am, owner_base);
    size_t n = text_field_utf8_len(text);
    for (size_t i = 0; i < n; ++i)
        anim.chars.push_back({1.0f, 0.0f});
}

void text_field_type_anim_clear(TextFieldTypeAnim &anim, AnimationManager &am,
                                uint64_t owner_base) {
    while (!anim.chars.empty())
        type_anim_pop(anim, am, owner_base);
}

float text_field_row_slide(TextFieldRowSlide &slide, AnimationManager &am, uint64_t owner,
                           float anchor_x) {
    if (!slide.primed) {
        slide.primed = true;
        slide.target = anchor_x;
        slide.x = anchor_x;
        return slide.x;
    }
    if (anchor_x != slide.target) {
        slide.target = anchor_x;
        am.cancelForOwner(owner);
        am.animate(
            slide.x, anchor_x, kTextFieldRowSlideMs, Easing::EaseOutCubic,
            [&slide](float v) { slide.x = v; }, {}, owner);
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
