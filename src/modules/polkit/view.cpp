#include <algorithm>

#include "config/polkit_config.h"

#include "modules/polkit/layout.h"
#include "modules/polkit/view.h"

#include "render/sticker.h"

namespace astralia::polkit {

namespace {

namespace cfg = polkit_config;

constexpr ui::TextStyle title_style{ui::FontFamily::text, 15, true};
constexpr ui::TextStyle message_style{ui::FontFamily::text, 12};
constexpr ui::TextStyle field_style{ui::FontFamily::text, 12};
constexpr ui::TextStyle info_style{ui::FontFamily::text, 11};

void centered_text(ui::Canvas &canvas, std::string_view text, ui::TextStyle style, float width,
                   float cx, float cy, const Color &color) {
    style.max_width = static_cast<int>(width);
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, cx - size.w / 2.0f, cy - size.h / 2.0f, color);
}

void line_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x,
               float y, float line_height, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, x, y + (line_height - size.h) / 2.0f, color);
}

void dots(ui::Canvas &canvas, const Model &model, float x0, float width, float cy) {
    constexpr float dot = cfg::dot_size;
    size_t length = utf8_length(model.password());
    size_t visible = visible_dots(length, width);
    size_t first = length - visible;
    float x = x0 + (width - static_cast<float>(visible) * dot) / 2.0f;
    const auto &chars = model.dot_anim().chars;
    for (size_t i = 0; i < visible; ++i) {
        size_t index = first + i;
        float scale = index < chars.size() ? chars[index].scale : 1.0f;
        float size = dot * scale;
        float cell_x = x + static_cast<float>(i) * dot;
        ui::Box box{cell_x + (dot - size) / 2.0f, cy - size / 2.0f, size, size};
        canvas.rounded(box, size / 2.0f, palette::accent);
    }
}

} // namespace

void paint(ui::Canvas &canvas, Model &model, float surface_width, float surface_height) {
    if (!model.open()) {
        return;
    }
    const Prompt &prompt = model.prompt();
    float card_h = static_cast<float>(card_height(model.show_info()));
    float card_w = cfg::card_width;
    ui::Box card{(surface_width - card_w) / 2.0f, (surface_height - card_h) / 2.0f, card_w, card_h};

    canvas.begin_group(card, {model.card_scale(), false});
    sticker(canvas, {0, 0, card_w, card_h}, metrics::radius_card, palette::paper);

    float pad = cfg::card_pad;
    float content_w = card_w - pad * 2.0f;
    float content_cx = pad + content_w / 2.0f;
    float y = pad;

    ui::TextStyle title = title_style;
    title.max_width = static_cast<int>(content_w);
    ui::TextSize title_size = canvas.measure(cfg::title_text, title);
    float tag_pad = static_cast<float>(cfg::title_tag_pad);
    sticker(canvas,
            {pad - tag_pad, y - tag_pad / 2.0f,
             title_size.w + 2.0f * tag_pad + metrics::shadow_widget,
             static_cast<float>(cfg::title_line_height) + tag_pad + metrics::shadow_widget},
            metrics::radius_widget, palette::accent, palette::ink, metrics::border_thin,
            metrics::shadow_widget);
    line_text(canvas, cfg::title_text, title, pad, y, cfg::title_line_height, palette::text);
    y += cfg::title_line_height + cfg::spacing;

    draw_marquee_text(canvas, model.animations(), model.marquee(), prompt.message, message_style,
                      pad, y, content_w, palette::text_muted);
    y += cfg::message_line_height + cfg::spacing;

    canvas.rounded({pad, y, content_w, cfg::field_height}, metrics::radius_widget,
                   prompt.needs_input ? palette::pop_b : palette::surface, metrics::border_thick,
                   palette::ink);
    float field_cy = y + cfg::field_height / 2.0f;
    float dots_w = content_w - cfg::dot_margin * 2.0f;
    if (prompt.needs_input) {
        if (!model.error().empty()) {
            centered_text(canvas, model.error(), field_style, dots_w, content_cx, field_cy,
                          palette::critical);
        } else if (model.password().empty()) {
            centered_text(canvas, cfg::password_placeholder, field_style, dots_w, content_cx,
                          field_cy, palette::text_muted);
        } else {
            dots(canvas, model, pad + cfg::dot_margin, dots_w, field_cy);
        }
    } else {
        centered_text(canvas, cfg::authenticating_text, field_style, dots_w, content_cx, field_cy,
                      palette::text_muted);
    }
    y += cfg::field_height;

    if (model.show_info()) {
        y += cfg::spacing;
        ui::TextStyle info = info_style;
        info.max_width = static_cast<int>(content_w);
        line_text(canvas, prompt.info, info, pad, y, cfg::info_line_height, palette::text_muted);
    }
    canvas.end_group();
}

} // namespace astralia::polkit
