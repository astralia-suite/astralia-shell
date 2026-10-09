#include <algorithm>

#include "wayland/render/panel_chrome.h"
#include "wayland/render/text.h"
#include "wayland/render/text_field.h"

namespace {

size_t utf8_char_len(unsigned char lead) {
    if ((lead & 0x80) == 0x00)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1;
}

} // namespace

void draw_text_field_caret(Node *parent, const TextFieldState &field, astralia::ui::Box caret, const float *color, bool active) {
    if (active && field.cursor_idle_visible)
        node_add_rect(parent, caret.x, caret.y, caret.w, caret.h, color);
}

void draw_text_field_preedit(Node *parent, TextureCache &tcache, int32_t scale, const std::string &preedit, float x, float center_y, const float *color) {
    if (preedit.empty())
        return;
    const Texture *tex =
        panel_chrome_detail::cached_text(tcache, preedit, scale);
    if (!tex)
        return;
    float y = center_y - static_cast<float>(tex->height) / 2.0f;
    node_add_texture(parent, x, y, *tex, color);
    node_add_rect(parent, x, y + static_cast<float>(tex->height), static_cast<float>(tex->width), 1.0f, color);
}

float draw_text_field_value(Node *parent, TextureCache &tcache, int32_t scale, const std::string &text, float x, float center_y, const float *color, const TextFieldTypeAnim *anim) {
    float cell_w = astralia_shell_text_advance();

    float cx = x;
    size_t char_index = 0;
    for (size_t i = 0; i < text.size();) {
        size_t len =
            std::min(utf8_char_len(static_cast<unsigned char>(text[i])), text.size() - i);
        std::string ch = text.substr(i, len);
        i += len;

        const TextFieldCharAnim *ca = anim && char_index < anim->chars.size() ? &anim->chars[char_index] : nullptr;
        float glyph_scale = ca ? ca->scale : 1.0f;
        float slide = ca ? ca->slide_x : 0.0f;

        const Texture *ch_tex =
            panel_chrome_detail::cached_text(tcache, ch, scale);
        if (ch_tex && glyph_scale > 0.0f) {
            float inv = 1.0f / static_cast<float>(ch_tex->scale > 0 ? ch_tex->scale : 1);
            float w = static_cast<float>(ch_tex->width) * inv * glyph_scale;
            float h = static_cast<float>(ch_tex->height) * inv * glyph_scale;
            float cell_center_x = cx + cell_w / 2.0f + slide;
            node_add_texture_rect(parent, cell_center_x - w / 2.0f, center_y - h / 2.0f, w, h, *ch_tex, color);
        }
        cx += cell_w;
        ++char_index;
    }
    return cx - x;
}
