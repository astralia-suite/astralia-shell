#include <algorithm>

#include "config/panel_config.h"

#include "modules/bar/panel/widgets.h"

namespace astralia::panel_widgets {

namespace cfg = panel_config;

void slider(ui::Canvas &canvas, const ui::Box &box, float value, bool dimmed) {
    float track_y = box.y + (box.h - cfg::slider_track) / 2.0f;
    float radius = cfg::slider_track / 2.0f;
    canvas.rounded({box.x, track_y, box.w, cfg::slider_track}, radius, palette::text_alpha11);
    float fill = box.w * std::clamp(value, 0.0f, 1.0f);
    if (fill > 0.0f) {
        canvas.rounded({box.x, track_y, fill, cfg::slider_track}, radius, dimmed ? palette::text_muted : palette::accent);
    }
    float knob_x = std::clamp(fill - cfg::slider_knob / 2.0f, 0.0f, box.w - cfg::slider_knob);
    canvas.rounded({box.x + knob_x, box.y + (box.h - cfg::slider_knob) / 2.0f, cfg::slider_knob, cfg::slider_knob}, cfg::slider_knob / 2.0f, palette::text);
}

float slider_value(const ui::Box &box, double x) {
    if (box.w <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>((x - box.x) / box.w), 0.0f, 1.0f);
}

void flat_bar(ui::Canvas &canvas, const ui::Box &box, float fraction, float min_fill, const Color &track, const Color &fill) {
    float radius = box.h / 2.0f;
    canvas.rounded(box, radius, track);
    float width = std::max(min_fill, box.w * std::clamp(fraction, 0.0f, 1.0f));
    canvas.rounded({box.x, box.y, std::min(width, box.w), box.h}, radius, fill);
}

void centered_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, const ui::Box &box, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, box.x + (box.w - size.w) / 2.0f, box.y + (box.h - size.h) / 2.0f, color);
}

void text_in_row(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x, const ui::Box &row, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, x, row.y + (row.h - size.h) / 2.0f, color);
}

float right_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float right, const ui::Box &row, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, right - size.w, row.y + (row.h - size.h) / 2.0f, color);
    return size.w;
}

void toggle(ui::Canvas &canvas, const ui::Box &box, bool on) {
    canvas.rounded(box, box.h / 2.0f, on ? palette::accent : palette::text_alpha20);
    float knob = box.h - 4.0f;
    float x = on ? box.x + box.w - 2.0f - knob : box.x + 2.0f;
    canvas.rounded({x, box.y + 2.0f, knob, knob}, knob / 2.0f, palette::text);
}

void device_row(ui::Canvas &canvas, const ui::Box &box, const DeviceRow &row) {
    constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
    constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
    constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};
    canvas.rounded(box, metrics::radius_md - 2.0f, row.background);
    float icon_x = box.x + cfg::row_icon_gap;
    ui::TextSize glyph = canvas.measure(row.glyph, icon_style);
    canvas.text(row.glyph, icon_style, icon_x, box.y + (box.h - glyph.h) / 2.0f, row.foreground);
    float text_x = box.x + 38.0f;
    float text_w = std::max(20.0f, box.x + box.w - cfg::row_icon_gap - row.reserve_right - text_x);
    ui::TextStyle title_style = text_style;
    title_style.max_width = static_cast<int>(text_w);
    ui::TextSize title = canvas.measure(row.title, title_style);
    if (row.subtitle.empty()) {
        canvas.text(row.title, title_style, text_x, box.y + (box.h - title.h) / 2.0f, row.foreground);
        return;
    }
    ui::TextStyle sub_style = small_style;
    sub_style.max_width = static_cast<int>(text_w);
    ui::TextSize sub = canvas.measure(row.subtitle, sub_style);
    float top = box.y + (box.h - title.h - sub.h - 2.0f) / 2.0f;
    canvas.text(row.title, title_style, text_x, top, row.foreground);
    canvas.text(row.subtitle, sub_style, text_x, top + title.h + 2.0f, palette::text_dim);
}

float confirm_height() {
    return cfg::padding + cfg::header_height + 24.0f + cfg::row_gap + 28.0f + cfg::padding;
}

void confirm_dialog(ui::Canvas &canvas, const ui::Box &box, std::string_view title, std::string_view prompt, std::string_view confirm_label, PanelPaint &paint, int cancel_id, int confirm_id) {
    constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
    canvas.rounded(box, metrics::radius_md, palette::overlay, metrics::border_thin, palette::accent);
    float x = box.x + cfg::padding;
    float width = box.w - 2.0f * cfg::padding;
    float y = box.y + cfg::padding;
    ui::TextStyle title_style = text_style;
    title_style.max_width = static_cast<int>(width);
    panel_widgets::text_in_row(canvas, title, title_style, x, {x, y, width, cfg::header_height}, palette::text);
    y += cfg::header_height;
    panel_widgets::centered_text(canvas, prompt, text_style, {x, y, width, 24.0f}, palette::text);
    y += 24.0f + cfg::row_gap;
    float pad = 16.0f;
    float ok_w = canvas.measure(confirm_label, text_style).w + pad;
    float cancel_w = canvas.measure("Cancel", text_style).w + pad;
    float bx = x + (width - (ok_w + cfg::row_gap + cancel_w)) / 2.0f;
    ui::Box ok{bx, y, ok_w, 28.0f};
    canvas.rounded(ok, 6.0f, palette::critical_alpha15);
    panel_widgets::centered_text(canvas, confirm_label, text_style, ok, palette::critical);
    paint.region(ok, confirm_id);
    ui::Box cancel{bx + ok_w + cfg::row_gap, y, cancel_w, 28.0f};
    canvas.rounded(cancel, 6.0f, palette::text_alpha08);
    panel_widgets::centered_text(canvas, "Cancel", text_style, cancel, palette::text);
    paint.region(cancel, cancel_id);
}

} // namespace astralia::panel_widgets
