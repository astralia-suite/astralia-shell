#include <algorithm>
#include <cmath>
#include <string>

#include "config/overview_config.h"

#include "modules/overview/view.h"

#include "render/scroll.h"
#include "render/sticker.h"
#include "render/tokens.h"

namespace astralia::overview {

namespace {

namespace cfg = overview_config;

constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ui::TextStyle number_style{ui::FontFamily::text, cfg::number_px};

void paint_cell(ui::Canvas &canvas, const Model &model, const Cell &cell, float scale) {
    bool hovered = model.dragging() && model.drag_target() == cell.workspace;
    sticker(canvas, cell.rect, metrics::radius_card * scale,
            hovered ? palette::pop_a : palette::surface, palette::border_outer,
            cfg::workspace_border_width, 0.0f);
    std::string label = std::to_string(cell.workspace);
    ui::TextSize size = canvas.measure(label, number_style);
    if (size.w <= cell.rect.w && size.h <= cell.rect.h) {
        canvas.text(label, number_style, cell.rect.x + (cell.rect.w - size.w) / 2.0f,
                    cell.rect.y + (cell.rect.h - size.h) / 2.0f,
                    with_alpha(palette::text, 1.0f - cfg::number_fade));
    }
}

void paint_tile(ui::Canvas &canvas, Model &model, const Tile &tile, const TileArt &art,
                float scale) {
    ui::Box rect = model.tile_rect(tile);
    float radius = metrics::radius_widget * scale;
    bool live = art && art(canvas, tile, rect, radius);
    if (!live) {
        sticker(canvas, rect, radius, palette::surface_alt, palette::border_outer,
                cfg::window_border_width, 0.0f);
    }
    const std::string &path = model.icon_path(tile.window_class);
    if (path.empty()) {
        return;
    }
    float size = std::round(std::min(rect.w, rect.h) * cfg::corner_icon_ratio);
    if (size < 1.0f) {
        return;
    }
    ui::ImageId icon = canvas.image(path, static_cast<int>(size));
    canvas.draw_image(icon,
                      {rect.x + rect.w - size - cfg::corner_icon_inset,
                       rect.y + rect.h - size - cfg::corner_icon_inset, size, size},
                      palette::white);
}

} // namespace

void paint(ui::Canvas &canvas, Model &model, const TileArt &art) {
    if (!model.is_open()) {
        return;
    }
    const Layout &layout = model.layout();
    if (layout.cells.empty()) {
        return;
    }
    constexpr ScrollMetrics metrics{};
    for (const ui::Box &panel : layout.panels) {
        paint_scroll(canvas, scroll_geometry(panel, model.scroll(), metrics), metrics);
    }

    if (model.grid_alpha() <= 0.0f) {
        return;
    }
    float scale = layout.scale;
    canvas.set_opacity(model.grid_alpha());
    for (const Cell &cell : layout.cells) {
        paint_cell(canvas, model, cell, scale);
    }
    for (const Tile &tile : model.tiles()) {
        paint_tile(canvas, model, tile, art, scale);
    }
    if (const ui::Box *indicator = model.indicator()) {
        float offset = metrics::shadow_widget * scale;
        ui::Box shadow{indicator->x, indicator->y + offset, indicator->w, indicator->h};
        canvas.rounded(shadow, metrics::radius_card * scale, transparent,
                       cfg::indicator_border_width, palette::border_outer);
        canvas.rounded(*indicator, metrics::radius_card * scale, transparent,
                       cfg::indicator_border_width, palette::accent);
    }
    canvas.set_opacity(1.0f);
}

} // namespace astralia::overview
