#include <algorithm>
#include <string>

#include "config/launcher_config.h"
#include "render/icons.h"
#include "render/sticker.h"

#include "modules/launcher/files_provider.h"
#include "modules/launcher/view.h"

#include "render/field_view.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = launcher_config;

constexpr ui::TextStyle label_style{ui::FontFamily::text, cfg::label_px};
constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_size};

const char *mode_icon(LauncherMode mode) {
    switch (mode) {
    case LauncherMode::run:
        return icon::terminal;
    case LauncherMode::google:
        return icon::brand_google;
    case LauncherMode::youtube:
        return icon::brand_youtube;
    case LauncherMode::duckduckgo:
    case LauncherMode::url:
        return icon::link;
    case LauncherMode::drun:
        break;
    }
    return icon::apps;
}

} // namespace

bool launcher_field_hit(const LauncherFrame &frame, double x, double y) {
    const ui::Box &f = frame.field;
    return f.w > 0.0f && x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h;
}

float launcher_field_column(const LauncherFrame &frame, double x) {
    if (frame.cell_w <= 0.0f) {
        return 0.0f;
    }
    float column = static_cast<float>(frame.first_column) + (static_cast<float>(x) - frame.text_x) / frame.cell_w;
    return std::max(column, 0.0f);
}

LauncherFrame paint_launcher(ui::Canvas &canvas, LauncherModel &model, float width, float height, const EntranceState *entrance) {
    LauncherFrame frame;
    constexpr float inset = cfg::clip_inset;
    float box_h = model.box_height();
    float box_x = (width - cfg::width) / 2.0f;
    float box_y = (height - box_h) / 2.0f;
    if (entrance != nullptr) {
        frame.entrance = entrance_sample(*entrance, {box_x, box_y, cfg::width, box_h});
        canvas.set_opacity(1.0f);
        if (frame.entrance.scrim > 0.0f) {
            canvas.rect({0.0f, 0.0f, width, height}, with_alpha(palette::shadow, frame.entrance.scrim));
        }
        canvas.set_opacity(frame.entrance.opacity);
    }
    frame.box = {box_x, box_y, cfg::width, box_h};
    sticker(canvas, frame.box, metrics::radius_card, palette::paper, palette::ink, cfg::menu_border_width);

    float ox = box_x + inset;
    float oy = box_y + inset;
    canvas.begin_group({ox, oy, cfg::width - 2 * inset, box_h - 2 * inset}, {1.0f, true});

    float mode_x = box_x + cfg::menu_pad;
    float field_top = box_y + cfg::menu_pad;
    canvas.rounded({mode_x - ox, field_top - oy, cfg::search_height, cfg::search_height}, metrics::radius_widget, palette::surface, cfg::border_width, palette::ink);
    const char *mode_glyph = mode_icon(model.mode());
    ui::TextSize mode_size = canvas.measure(mode_glyph, icon_style);
    canvas.text(mode_glyph, icon_style, mode_x - ox + (cfg::search_height - mode_size.w) / 2.0f, field_top - oy + (cfg::search_height - mode_size.h) / 2.0f, palette::text);

    float field_x = mode_x + cfg::search_height + cfg::pad;
    float field_w = box_x + cfg::width - cfg::menu_pad - field_x;
    canvas.rounded({field_x - ox, field_top - oy, field_w, cfg::search_height}, metrics::radius_widget, palette::surface, cfg::border_width, palette::ink);
    float text_x = field_x + cfg::pad;
    float center_y = field_top + cfg::search_height / 2.0f;

    float caret_h = cfg::search_height - 2.0f * cfg::pad;
    ui::FieldDraw draw;
    draw.style = label_style;
    draw.x = text_x - ox;
    draw.center_y = center_y - oy;
    draw.caret_height = caret_h;
    draw.caret_width = cfg::caret_width;
    const std::string &text = model.field().text;
    size_t first = model.field_window(cfg::max_row_chars);
    size_t total = text_field_utf8_len(text);
    size_t last = std::min(total, first + cfg::max_row_chars);
    size_t first_byte = text_field_byte_offset(text, first);
    std::string display = text.substr(first_byte, text_field_byte_offset(text, last) - first_byte);
    const TextCursor &cursor = model.cursor();
    auto column_of = [&](size_t byte) { return std::clamp(text_field_char_index(text, byte), first, last) - first; };
    ui::FieldCursor field_cursor{column_of(cursor.pos), column_of(cursor.begin()), column_of(cursor.end())};
    draw.first_index = first;
    ui::Box caret = ui::draw_field_input(canvas, display, model.field(), model.query_anim(), draw, &field_cursor);
    frame.caret = {caret.x + ox, caret.y + oy, caret.w, caret.h};
    frame.field = {field_x, field_top, field_w, cfg::search_height};
    frame.text_x = text_x;
    frame.cell_w = canvas.advance(label_style);
    frame.first_column = first;

    float content_x = mode_x + cfg::search_height + cfg::bullet_gap;
    float list_top = box_y + cfg::list_top;
    float list_h = box_y + box_h - inset - list_top;
    canvas.begin_group({mode_x - ox, list_top - oy, cfg::width - 2 * cfg::menu_pad, list_h}, {1.0f, true});

    float row_x = content_x - mode_x;
    float row_w = box_x + cfg::width - cfg::menu_pad - content_x;
    std::vector<LauncherRow> rows = model.rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        float y = static_cast<float>(i) * cfg::row_pitch - model.scroll_offset();
        if (y + cfg::row_height > 0.0f && y < list_h) {
            selectable(canvas, {row_x, y, row_w, cfg::row_height}, metrics::radius_widget, false, i == model.hovered() && i != model.selected());
        }
    }
    if (model.selected() >= 0) {
        selectable(canvas, {row_x, model.highlight_offset() - model.scroll_offset(), row_w, cfg::row_height}, metrics::radius_widget, true);
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        float y = static_cast<float>(i) * cfg::row_pitch - model.scroll_offset();
        if (y + cfg::row_height <= 0.0f || y >= list_h) {
            continue;
        }
        if (y >= 0.0f && y + cfg::row_height <= list_h) {
            frame.rows.push_back({{content_x, list_top + y, row_w, cfg::row_height}, i});
        }
        const LauncherRow &row = rows[static_cast<size_t>(i)];

        float x = row_x + cfg::pad;
        ui::ImageId image = row.icon_path.empty() ? ui::no_image : canvas.image(row.icon_path, cfg::icon_size);
        if (image != ui::no_image) {
            canvas.draw_image(image, {x, y + (cfg::row_height - cfg::icon_size) / 2.0f, static_cast<float>(cfg::icon_size), static_cast<float>(cfg::icon_size)}, palette::white);
        } else {
            ui::TextSize size = canvas.measure(row.glyph, icon_style);
            canvas.text(row.glyph, icon_style, x + (cfg::icon_size - size.w) / 2.0f, y + (cfg::row_height - size.h) / 2.0f, palette::text);
        }
        x += cfg::icon_size + cfg::pad;

        std::string label = elide(row.label, cfg::max_row_chars);
        ui::TextSize label_size = canvas.measure(label, label_style);
        if (row.subtitle.empty()) {
            canvas.text(label, label_style, x, y + (cfg::row_height - label_size.h) / 2.0f, palette::text);
            continue;
        }
        std::string subtitle = elide_middle(row.subtitle, cfg::max_row_chars);
        ui::TextSize sub_size = canvas.measure(subtitle, small_style);
        float top = y + (cfg::row_height - label_size.h - cfg::two_line_gap - sub_size.h) / 2.0f;
        canvas.text(label, label_style, x, top, palette::text);
        canvas.text(subtitle, small_style, x, top + label_size.h + cfg::two_line_gap, palette::text_alpha65);
    }

    for (int slot = 0; slot < cfg::max_visible; ++slot) {
        std::string asset = std::string(cfg::bullet_asset_prefix) + std::to_string(slot + 1) + cfg::bullet_asset_suffix;
        ui::ImageId bullet = canvas.image(asset, static_cast<int>(cfg::bullet_size));
        if (bullet == ui::no_image) {
            continue;
        }
        canvas.draw_image(bullet, {(cfg::search_height - cfg::bullet_size) / 2.0f, static_cast<float>(slot) * cfg::row_pitch + (cfg::row_height - cfg::bullet_size) / 2.0f, cfg::bullet_size, cfg::bullet_size}, palette::white);
    }

    canvas.end_group();
    canvas.end_group();
    if (frame.entrance.flash > 0.0f) {
        canvas.set_opacity(1.0f);
        canvas.rect({0.0f, 0.0f, width, height}, with_alpha(palette::accent_container, frame.entrance.flash));
    }
    return frame;
}

} // namespace astralia
