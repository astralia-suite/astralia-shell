#include <algorithm>
#include <cmath>
#include <string>

#include "config/overview_config.h"

#include "modules/overview/view.h"

#include "render/scroll.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = overview_config;

constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ui::TextStyle number_style{ui::FontFamily::text, cfg::number_px};

void paint_cell(ui::Canvas &canvas, const OverviewModel &model, const OverviewCell &cell, float scale) {
    bool hovered = model.dragging() && model.drag_target() == cell.workspace;
    canvas.rounded(cell.rect, metrics::radius_card * scale, hovered ? palette::pop_a : palette::surface, cfg::workspace_border_width, palette::ink);
    std::string label = std::to_string(cell.workspace);
    ui::TextSize size = canvas.measure(label, number_style);
    if (size.w <= cell.rect.w && size.h <= cell.rect.h) {
        canvas.text(label, number_style, cell.rect.x + (cell.rect.w - size.w) / 2.0f, cell.rect.y + (cell.rect.h - size.h) / 2.0f, with_alpha(palette::text, 1.0f - cfg::number_fade));
    }
}

void paint_tile(ui::Canvas &canvas, OverviewModel &model, const OverviewTile &tile, const OverviewTileArt &art, float scale) {
    ui::Box rect = model.tile_rect(tile);
    float radius = metrics::radius_widget * scale;
    bool live = art && art(canvas, tile, rect, radius);
    if (!live) {
        canvas.rounded(rect, radius, palette::surface_alt, cfg::window_border_width, palette::ink);
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
    canvas.draw_image(icon, {rect.x + rect.w - size - cfg::corner_icon_inset, rect.y + rect.h - size - cfg::corner_icon_inset, size, size}, palette::white);
}

} // namespace

void paint_overview(ui::Canvas &canvas, OverviewModel &model, const OverviewTileArt &art) {
    if (!model.is_open()) {
        return;
    }
    const OverviewLayout &layout = model.layout();
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
    for (const OverviewCell &cell : layout.cells) {
        paint_cell(canvas, model, cell, scale);
    }
    for (const OverviewTile &tile : model.tiles()) {
        paint_tile(canvas, model, tile, art, scale);
    }
    if (const ui::Box *indicator = model.indicator()) {
        float offset = metrics::shadow_widget * scale;
        ui::Box shadow{indicator->x + offset, indicator->y + offset, indicator->w, indicator->h};
        canvas.rounded(shadow, metrics::radius_card * scale, transparent, cfg::indicator_border_width, palette::ink);
        canvas.rounded(*indicator, metrics::radius_card * scale, transparent, cfg::indicator_border_width, palette::accent);
    }
    canvas.set_opacity(1.0f);
}

} // namespace astralia
