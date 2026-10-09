#include <algorithm>
#include <string>

#include "config/launcher_config.h"
#include "render/icons.h"

#include "modules/launcher/files_provider.h"
#include "modules/launcher/view.h"

#include "render/field_view.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = launcher_config;

constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
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

LauncherFrame paint_launcher(ui::Canvas &canvas, LauncherModel &model, float width, float height) {
    LauncherFrame frame;
    constexpr float inset = cfg::clip_inset;
    float box_h = model.box_height();
    float box_x = (width - cfg::width) / 2.0f;
    float box_y = (height - box_h) / 2.0f;
    frame.box = {box_x, box_y, cfg::width, box_h};
    canvas.rounded(frame.box, metrics::radius_md, palette::base_alpha80, cfg::menu_border_width, palette::accent);

    float ox = box_x + inset;
    float oy = box_y + inset;
    canvas.begin_group({ox, oy, cfg::width - 2 * inset, box_h - 2 * inset}, {1.0f, true});

    float mode_x = box_x + cfg::menu_pad;
    float field_top = box_y + cfg::menu_pad;
    canvas.rounded({mode_x - ox, field_top - oy, cfg::search_height, cfg::search_height}, metrics::radius_sm, transparent, cfg::border_width, palette::accent);
    const char *mode_glyph = mode_icon(model.mode());
    ui::TextSize mode_size = canvas.measure(mode_glyph, icon_style);
    canvas.text(mode_glyph, icon_style, mode_x - ox + (cfg::search_height - mode_size.w) / 2.0f, field_top - oy + (cfg::search_height - mode_size.h) / 2.0f, palette::text);

    float field_x = mode_x + cfg::search_height + cfg::pad;
    float field_w = box_x + cfg::width - cfg::menu_pad - field_x;
    canvas.rounded({field_x - ox, field_top - oy, field_w, cfg::search_height}, metrics::radius_sm, transparent, cfg::border_width, palette::accent);
    float text_x = field_x + cfg::pad;
    float center_y = field_top + cfg::search_height / 2.0f;

    float caret_h = cfg::search_height - 2.0f * cfg::pad;
    ui::FieldDraw draw;
    draw.style = label_style;
    draw.x = text_x - ox;
    draw.center_y = center_y - oy;
    draw.caret_height = caret_h;
    draw.caret_width = cfg::caret_width;
    ui::Box caret = ui::draw_field_input(canvas, elide(model.field().text, cfg::max_row_chars), model.field(), model.query_anim(), draw);
    frame.caret = {caret.x + ox, caret.y + oy, caret.w, caret.h};

    float content_x = mode_x + cfg::search_height + cfg::bullet_gap;
    float list_top = box_y + cfg::list_top;
    float list_h = box_y + box_h - inset - list_top;
    canvas.begin_group({mode_x - ox, list_top - oy, cfg::width - 2 * cfg::menu_pad, list_h}, {1.0f, true});

    float row_x = content_x - mode_x;
    float row_w = box_x + cfg::width - cfg::menu_pad - content_x;
    std::vector<LauncherRow> rows = model.rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        float y = static_cast<float>(i) * cfg::row_pitch - model.scroll_offset();
        if (y + cfg::row_height <= 0.0f || y >= list_h) {
            continue;
        }
        if (y >= 0.0f && y + cfg::row_height <= list_h) {
            frame.rows.push_back({{content_x, list_top + y, row_w, cfg::row_height}, i});
        }
        const LauncherRow &row = rows[static_cast<size_t>(i)];
        canvas.rounded({row_x, y, row_w, cfg::row_height}, metrics::radius_sm, palette::text_alpha03);
        if (i == model.hovered() && i != model.selected()) {
            canvas.rounded({row_x, y, row_w, cfg::row_height}, metrics::radius_sm, transparent, cfg::border_width, palette::accent);
        }

        float x = row_x + cfg::pad;
        ui::ImageId image = row.icon_path.empty() ? ui::no_image : canvas.image(row.icon_path, cfg::icon_size);
        if (image != ui::no_image) {
            canvas.draw_image(image, {x, y + (cfg::row_height - cfg::icon_size) / 2.0f, static_cast<float>(cfg::icon_size), static_cast<float>(cfg::icon_size)}, palette::text);
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
        canvas.draw_image(bullet, {(cfg::search_height - cfg::bullet_size) / 2.0f, static_cast<float>(slot) * cfg::row_pitch + (cfg::row_height - cfg::bullet_size) / 2.0f, cfg::bullet_size, cfg::bullet_size}, palette::text);
    }

    if (model.selected() >= 0) {
        canvas.rounded({row_x, model.highlight_offset() - model.scroll_offset(), row_w, cfg::row_height}, metrics::radius_sm, transparent, cfg::highlight_border_width, palette::accent_alt_alpha50);
    }
    canvas.end_group();
    canvas.end_group();
    return frame;
}

} // namespace astralia
