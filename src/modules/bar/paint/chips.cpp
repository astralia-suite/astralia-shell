#include <algorithm>
#include <string>

#include "config/bar_layout.h"

#include "render/sticker.h"

#include "modules/bar/paint/chips.h"

namespace astralia::bar {

namespace {

namespace cfg = bar_layout;

constexpr ui::TextStyle icon_style{ui::FontFamily::icon, static_cast<int>(cfg::icon_px)};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle name_style{ui::FontFamily::text, cfg::active_window_name_px};
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::active_window_title_px};

} // namespace

float icon_cell_extent(float icon_w, float label_w, float amount) {
    return label_w > 0.0f ? icon_w + (label_w + cfg::label_gap) * amount : icon_w;
}

Color chip_fill() { return palette::accent_container; }

void paint_pill(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box, const Color &fill) {
    if (style.has_pill_bg()) {
        paint_ringed(canvas, style, box, fill);
    }
}

void paint_ringed(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                  const Color &fill) {
    sticker(canvas, box, style.radius, fill, style.border, style.border_width, style.shadow,
            style.shadow_color, style.shadow_dx);
}

void paint_icon_label(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                      const char *glyph, std::string_view label, float label_alpha) {
    float pad = box.h * style.pad_ratio;
    float dy = style.text_lift;
    canvas.begin_group(box, {1.0f, true});
    ui::TextSize icon = canvas.measure(glyph, icon_style);
    canvas.text(glyph, icon_style, pad, (box.h - icon.h) / 2.0f - dy, palette::text);
    if (!label.empty() && label_alpha > 0.0f) {
        std::string text(label);
        ui::TextSize size = canvas.measure(text, text_style);
        canvas.text(text, text_style, pad + icon.w + cfg::label_gap, (box.h - size.h) / 2.0f - dy,
                    with_alpha(palette::text, label_alpha));
    }
    canvas.end_group();
}

void paint_two_lines(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                     const char *glyph, std::string_view title, std::string_view detail,
                     float amount) {
    float pad = box.h * style.pad_ratio;
    float dy = style.text_lift;
    canvas.begin_group(box, {1.0f, true});
    ui::TextSize icon = canvas.measure(glyph, icon_style);
    canvas.text(glyph, icon_style, pad, (box.h - icon.h) / 2.0f - dy, palette::text);
    std::string first(title);
    std::string second(detail);
    const ui::TextStyle &first_style = second.empty() ? text_style : name_style;
    ui::TextSize a = canvas.measure(first, first_style);
    ui::TextSize b = canvas.measure(second, title_style);
    float x = pad + icon.w + cfg::label_gap;
    float edge = style.border_width + metrics::ring_width;
    float inner = box.h - style.shadow - 2.0f * edge;
    float stacked = second.empty() ? a.h : a.h + b.h;
    float overlap = std::max(0.0f, stacked - inner);
    float y = edge + (inner - (stacked - overlap)) / 2.0f;
    canvas.text(first, first_style, x, y, with_alpha(palette::text, amount));
    if (!second.empty()) {
        canvas.text(second, title_style, x, y + a.h - overlap,
                    with_alpha(palette::text_muted, amount));
    }
    canvas.end_group();
}

void paint_badge(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                 std::string_view text, const Color &fill) {
    paint_pill(canvas, style, box, fill);
    std::string number(text);
    ui::TextSize size = canvas.measure(number, text_style);
    canvas.text(number, text_style, box.x + (box.w - size.w) / 2.0f,
                box.y + (box.h - size.h) / 2.0f - style.text_lift, palette::text);
}

void paint_icon_row(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                    std::span<const IconCell> cells, float gap) {
    paint_pill(canvas, style, box, chip_fill());
    float x = box.h * style.pad_ratio;
    float dy = style.text_lift;
    canvas.begin_group(box, {1.0f, true});
    for (const IconCell &cell : cells) {
        ui::TextSize icon = canvas.measure(cell.glyph, icon_style);
        canvas.text(cell.glyph, icon_style, x, (box.h - icon.h) / 2.0f - dy, palette::text);
        if (cell.label_w > 0.0f && cell.amount > 0.0f) {
            std::string text(cell.label);
            ui::TextSize size = canvas.measure(text, text_style);
            canvas.text(text, text_style, x + cell.icon_w + cfg::label_gap,
                        (box.h - size.h) / 2.0f - dy, with_alpha(palette::text, cell.amount));
        }
        x += icon_cell_extent(cell.icon_w, cell.label_w, cell.amount) + gap;
    }
    canvas.end_group();
}

} // namespace astralia::bar
