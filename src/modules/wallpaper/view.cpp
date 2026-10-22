#include "config/wallpaper_config.h"

#include "modules/wallpaper/view.h"

#include "render/sticker.h"
#include "render/tokens.h"

namespace astralia::wallpaper {

namespace {

namespace cfg = wallpaper_config;

constexpr ui::TextStyle icon_style{ui::FontFamily::icon, static_cast<int>(cfg::menu_icon_size)};
constexpr ui::TextStyle label_style{ui::FontFamily::text, cfg::menu_text_px, true};

} // namespace

void paint(ui::Canvas &canvas, const DesktopMenuModel &model, float x, float y) {
    if (!model.is_open()) {
        return;
    }
    canvas.set_opacity(1.0f);
    const ui::Box &box = model.box();
    sticker(canvas, {x, y, box.w, box.h}, metrics::radius_card, palette::paper);
    const std::vector<DesktopMenuEntry> &entries = DesktopMenuModel::entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        ui::Box row = model.row(i);
        row.x += x;
        row.y += y;
        selectable(canvas, row, metrics::radius_widget, false, i == model.hovered());
        ui::TextSize icon_size = canvas.measure(entries[static_cast<size_t>(i)].icon, icon_style);
        ui::TextSize label_size =
            canvas.measure(entries[static_cast<size_t>(i)].label, label_style);
        float icon_x = row.x + cfg::menu_padding + 2.0f;
        canvas.text(entries[static_cast<size_t>(i)].icon, icon_style, icon_x,
                    row.y + (row.h - icon_size.h) / 2.0f, palette::text);
        canvas.text(entries[static_cast<size_t>(i)].label, label_style,
                    icon_x + cfg::menu_icon_size + cfg::menu_icon_gap,
                    row.y + (row.h - label_size.h) / 2.0f, palette::text);
    }
}

} // namespace astralia::wallpaper
