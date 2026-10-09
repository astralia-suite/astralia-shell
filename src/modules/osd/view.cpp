#include <algorithm>

#include "config/osd_config.h"

#include "modules/osd/view.h"

#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = osd_config;

constexpr float width = cfg::width;
constexpr float height = cfg::height;

} // namespace

void paint_osd(ui::Canvas &canvas, const OsdModel &model) {
    canvas.set_opacity(model.opacity());
    if (model.opacity() <= 0.0f) {
        return;
    }
    canvas.rounded({0, 0, width, height}, height / 2.0f, palette::overlay, cfg::border_width, palette::electro);

    ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};
    ui::TextSize icon_size = canvas.measure(model.glyph(), icon_style);
    bool has_icon = icon_size.w > 0.0f;
    if (has_icon) {
        canvas.text(model.glyph(), icon_style, cfg::content_margin, (height - icon_size.h) / 2.0f, lerp_color(palette::text, palette::text_muted, model.icon_mix()));
    }

    float track_x = cfg::content_margin + (has_icon ? cfg::icon_px + cfg::bar_margin : 0.0f);
    float track_w = width - track_x - cfg::bar_margin - cfg::label_width - cfg::content_margin;
    float track_y = (height - cfg::track_height) / 2.0f;
    float radius = cfg::track_height / 2.0f;
    canvas.rounded({track_x, track_y, track_w, cfg::track_height}, radius, palette::text_alpha11);
    float fill_w = track_w * std::clamp(model.bar_fill(), 0.0f, 1.0f);
    if (fill_w > 0.0f) {
        canvas.rounded({track_x, track_y, fill_w, cfg::track_height}, radius, model.muted() ? palette::text_muted : palette::accent);
    }

    ui::TextStyle label_style{ui::FontFamily::text, cfg::label_px, false, cfg::label_width};
    std::string label = model.label();
    ui::TextSize label_size = canvas.measure(label, label_style);
    canvas.text(label, label_style, width - cfg::content_margin - label_size.w, (height - label_size.h) / 2.0f, palette::text);
}

} // namespace astralia
