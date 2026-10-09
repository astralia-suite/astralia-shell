#include <algorithm>
#include <cmath>
#include <string>

#include "config/overview_config.h"

#include "modules/overview/view.h"

#include "ui/tokens.h"

namespace astralia {

namespace {

namespace cfg = overview_config;

constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ui::TextStyle number_style{ui::FontFamily::text, cfg::number_px};

int centered_icon_size(const ui::Box &rect) {
    float wanted = std::min(rect.w, rect.h) * cfg::icon_to_tile_ratio;
    if (wanted < static_cast<float>(cfg::icon_min_size)) {
        return 0;
    }
    int size = static_cast<int>(wanted) / cfg::icon_size_step * cfg::icon_size_step;
    return std::clamp(size, cfg::icon_min_size, cfg::icon_max_size);
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
    float scale = layout.scale;

    for (const ui::Box &panel : layout.panels) {
        canvas.rounded(panel, cfg::screen_rounding * scale + cfg::padding, palette::field_bg, cfg::background_border_width, palette::accent);
    }

    for (const OverviewCell &cell : layout.cells) {
        bool hovered = model.dragging() && model.drag_target() == cell.workspace;
        canvas.rounded(cell.rect, cfg::screen_rounding * scale, palette::field_bg, cfg::workspace_border_width, hovered ? palette::text_alpha08 : palette::text_alpha20);
        std::string label = std::to_string(cell.workspace);
        ui::TextSize size = canvas.measure(label, number_style);
        if (size.w <= cell.rect.w && size.h <= cell.rect.h) {
            canvas.text(label, number_style, cell.rect.x + (cell.rect.w - size.w) / 2.0f, cell.rect.y + (cell.rect.h - size.h) / 2.0f, with_alpha(palette::text, 1.0f - cfg::number_fade));
        }
    }

    for (const OverviewTile &tile : model.tiles()) {
        ui::Box rect = model.tile_rect(tile);
        float radius = cfg::window_rounding * scale;
        bool live = art && art(canvas, tile, rect, radius);
        if (!live) {
            canvas.rounded(rect, radius, palette::surface_alt, cfg::window_border_width, palette::accent);
        }
        const std::string &path = model.icon_path(tile.window_class);
        if (path.empty()) {
            continue;
        }
        if (live) {
            float size = std::round(std::min(rect.w, rect.h) * cfg::corner_icon_ratio);
            if (size < 1.0f) {
                continue;
            }
            ui::ImageId icon = canvas.image(path, static_cast<int>(size));
            canvas.draw_image(icon, {rect.x + rect.w - size - cfg::corner_icon_inset, rect.y + rect.h - size - cfg::corner_icon_inset, size, size}, palette::text);
        } else if (int size = centered_icon_size(rect); size > 0) {
            ui::ImageId icon = canvas.image(path, size);
            ui::TextSize actual = canvas.image_size(icon);
            canvas.draw_image(icon, {std::round(rect.x + (rect.w - actual.w) / 2.0f), std::round(rect.y + (rect.h - actual.h) / 2.0f), actual.w, actual.h}, palette::text);
        }
    }

    if (const ui::Box *indicator = model.indicator()) {
        canvas.rounded(*indicator, cfg::screen_rounding * scale, transparent, cfg::indicator_border_width, palette::accent_alt);
    }
}

} // namespace astralia
