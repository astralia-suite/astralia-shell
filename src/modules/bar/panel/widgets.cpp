#include <algorithm>

#include "config/panel_config.h"
#include "render/icons.h"

#include "modules/bar/panel/widgets.h"

namespace astralia::panel_widgets {

namespace cfg = panel_config;

void slider(ui::Canvas &canvas, const ui::Box &box, float value, bool dimmed, bool focused, float track) {
    float track_y = box.y + (box.h - track) / 2.0f;
    float radius = track / 2.0f;
    canvas.rounded({box.x, track_y, box.w, track}, radius, palette::text_alpha11);
    float fill = box.w * std::clamp(value, 0.0f, 1.0f);
    if (fill > 0.0f) {
        canvas.rounded({box.x, track_y, fill, track}, radius, dimmed ? palette::text_muted : palette::accent);
    }
    float knob_x = std::clamp(fill - cfg::slider_knob / 2.0f, 0.0f, box.w - cfg::slider_knob);
    float knob_y = box.y + (box.h - cfg::slider_knob) / 2.0f;
    canvas.rounded({box.x + knob_x, knob_y, cfg::slider_knob, cfg::slider_knob}, cfg::slider_knob / 2.0f, palette::text);
    if (focused) {
        float inner = cfg::slider_knob - 2.0f * cfg::slider_focus_ring;
        canvas.rounded({box.x + knob_x + cfg::slider_focus_ring, knob_y + cfg::slider_focus_ring, inner, inner}, inner / 2.0f, palette::accent);
    }
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
    canvas.rounded(box, box.h / 2.0f, on ? palette::accent : palette::text_alpha11);
    float knob = box.h - 4.0f;
    float x = on ? box.x + box.w - 2.0f - knob : box.x + 2.0f;
    canvas.rounded({x, box.y + 2.0f, knob, knob}, knob / 2.0f, palette::text);
}

void icon_button(ui::Canvas &canvas, const ui::Box &box, const char *glyph, const Color &color) {
    constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};
    canvas.rounded(box, box.w / 2.0f, palette::overlay);
    centered_text(canvas, glyph, icon_style, box, color);
}

void device_row(ui::Canvas &canvas, const ui::Box &box, const DeviceRow &row, PanelPaint &paint, int connect_id, int forget_id, int index) {
    constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
    constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};
    canvas.rounded(box, metrics::radius_md - 2.0f, row.background);
    ui::TextSize glyph = canvas.measure(row.glyph, icon_style);
    canvas.text(row.glyph, icon_style, box.x + cfg::row_icon_gap, box.y + (box.h - glyph.h) / 2.0f, row.glyph_color);

    float right = box.x + box.w - cfg::row_icon_gap;
    float actions = 0.0f;
    if (row.busy) {
        constexpr std::string_view busy = "Connecting\xE2\x80\xA6";
        ui::TextSize size = canvas.measure(busy, text_style);
        actions = size.w;
        canvas.text(busy, text_style, right - size.w, box.y + (box.h - size.h) / 2.0f, palette::accent);
    } else {
        float size = cfg::close_button;
        float y = box.y + (box.h - size) / 2.0f;
        ui::Box connect{right - size, y, size, size};
        icon_button(canvas, connect, row.connected ? icon::network_disconnect : icon::network_connect, row.connected ? palette::accent : palette::text);
        paint.region(connect, connect_id, index);
        actions = size;
        if (row.can_forget) {
            ui::Box forget{connect.x - cfg::action_gap - size, y, size, size};
            icon_button(canvas, forget, icon::close, palette::text_muted);
            paint.region(forget, forget_id, index);
            actions += cfg::action_gap + size;
        }
    }

    float text_x = box.x + cfg::row_text_left;
    float text_w = std::max(0.0f, right - actions - cfg::action_gap - text_x);
    ui::TextStyle title_style = text_style;
    title_style.max_width = static_cast<int>(text_w);
    ui::TextSize title = canvas.measure(row.title, title_style);
    float title_h = row.marquee != nullptr ? canvas.measure(row.title, text_style).h : title.h;
    float title_y = row.subtitle.empty() ? box.y + box.h / 2.0f - title_h / 2.0f : box.y + box.h / 2.0f - title_h - 1.0f;
    if (row.marquee != nullptr && row.animations != nullptr) {
        draw_marquee_text(canvas, *row.animations, *row.marquee, row.title, text_style, text_x, title_y, text_w, row.title_color);
    } else {
        canvas.text(row.title, title_style, text_x, title_y, row.title_color);
    }
    if (!row.subtitle.empty()) {
        ui::TextStyle sub_style = text_style;
        sub_style.max_width = static_cast<int>(text_w);
        canvas.text(row.subtitle, sub_style, text_x, box.y + box.h / 2.0f + 1.0f, row.subtitle_color);
    }
}

void dialog_top(ui::Canvas &canvas, const ui::Box &box, std::string_view label, PanelPaint &paint, int close_id) {
    constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
    canvas.rounded(box, metrics::radius_md, palette::overlay, metrics::border_thin, palette::accent);
    size_t chars = static_cast<size_t>(cfg::dialog_label_chars);
    std::string shown = label.size() <= chars ? std::string(label) : std::string(label.substr(0, chars - 1)) + "\xE2\x80\xA6";
    canvas.text(shown, text_style, box.x + cfg::padding, box.y + cfg::dialog_top_margin, palette::text_dim);
    ui::Box close{box.x + box.w - cfg::dialog_top_margin - cfg::dialog_button, box.y + cfg::dialog_top_margin, cfg::dialog_button, cfg::dialog_button};
    icon_button(canvas, close, icon::close, palette::text);
    paint.region(close, close_id);
}

void confirm_dialog(ui::Canvas &canvas, const ui::Box &box, std::string_view label, std::string_view prompt, std::string_view confirm_label, PanelPaint &paint, int cancel_id, int confirm_id) {
    constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
    dialog_top(canvas, box, label, paint, cancel_id);
    float x = box.x + cfg::padding;
    float width = box.w - 2.0f * cfg::padding;
    float y = box.y + cfg::padding + cfg::dialog_spacer;
    panel_widgets::centered_text(canvas, prompt, text_style, {x, y, width, cfg::dialog_label}, palette::text);
    y += cfg::dialog_label + cfg::row_gap;
    float ok_w = canvas.measure(confirm_label, text_style).w + cfg::dialog_button_pad;
    float cancel_w = canvas.measure("Cancel", text_style).w + cfg::dialog_button_pad;
    float bx = x + (width - (ok_w + cfg::row_gap + cancel_w)) / 2.0f;
    ui::Box ok{bx, y, ok_w, cfg::dialog_button};
    canvas.rounded(ok, cfg::dialog_button_radius, palette::critical_alpha15);
    panel_widgets::centered_text(canvas, confirm_label, text_style, ok, palette::critical);
    paint.region(ok, confirm_id);
    ui::Box cancel{bx + ok_w + cfg::row_gap, y, cancel_w, cfg::dialog_button};
    canvas.rounded(cancel, cfg::dialog_button_radius, palette::overlay);
    panel_widgets::centered_text(canvas, "Cancel", text_style, cancel, palette::text);
    paint.region(cancel, cancel_id);
}

} // namespace astralia::panel_widgets
