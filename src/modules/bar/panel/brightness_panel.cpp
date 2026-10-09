#include <algorithm>
#include <cmath>
#include <format>

#include "render/icons.h"

#include "modules/bar/panel/brightness_panel.h"
#include "modules/bar/panel/widgets.h"

#include "render/glyphs.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

constexpr int slider_id = 1;
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

} // namespace

BrightnessPanel::BrightnessPanel(BrightnessService &brightness) : brightness_(brightness) {
    brightness_.changed.connect(guarded([this] {
        if (!dragging_) {
            sync();
            if (changed) {
                changed();
            }
        }
    }));
}

void BrightnessPanel::sync() {
    enabled_ = brightness_.available();
    percent_ = brightness_.percent();
}

void BrightnessPanel::opened() {
    dragging_ = false;
    hovered_ = false;
    sync();
}

void BrightnessPanel::apply(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == percent_) {
        return;
    }
    percent_ = percent;
    brightness_.set(percent);
}

void BrightnessPanel::paint(ui::Canvas &canvas, const ui::Box &view, float, PanelPaint &paint) {
    ui::Box row{view.x, view.y, view.w, cfg::brightness_row};
    const char *glyph = icon::brightness_threshold(percent_);
    const Color &color = enabled_ ? palette::text : palette::text_dim;
    ui::TextSize icon_size = canvas.measure(glyph, icon_style);
    canvas.text(glyph, icon_style, row.x, row.y + (row.h - icon_size.h) / 2.0f, color);

    std::string label = enabled_ ? std::format("{}%", percent_) : std::string("-");
    float pct_right = row.x + row.w;
    panel_widgets::right_text(canvas, label, text_style, pct_right, row, palette::text_dim);

    float slider_x = row.x + icon_size.w + cfg::brightness_icon_gap;
    float slider_right = pct_right - cfg::brightness_pct_width - cfg::brightness_slider_gap;
    ui::Box track{slider_x, row.y, std::max(0.0f, slider_right - slider_x), row.h};
    panel_widgets::slider(canvas, track, enabled_ ? static_cast<float>(percent_) / 100.0f : 0.0f, false, enabled_ && (dragging_ || hovered_));
    if (enabled_) {
        paint.region(track, slider_id, 0, 0, true);
    }
}

bool BrightnessPanel::hover(int id, int) {
    bool next = id == slider_id;
    if (next == hovered_) {
        return false;
    }
    hovered_ = next;
    return true;
}

bool BrightnessPanel::activate(const PanelRegion &region, double x, double) {
    dragging_ = true;
    apply(static_cast<int>(std::lround(panel_widgets::slider_value(region.box, x) * 100.0f)));
    return true;
}

bool BrightnessPanel::drag(const PanelRegion &region, double x, double) {
    apply(static_cast<int>(std::lround(panel_widgets::slider_value(region.box, x) * 100.0f)));
    return true;
}

bool BrightnessPanel::key(const input::KeyEvent &event) {
    if (!enabled_) {
        return false;
    }
    if (event.kind == input::KeyKind::Left) {
        apply(percent_ - 1);
        return true;
    }
    if (event.kind == input::KeyKind::Right) {
        apply(percent_ + 1);
        return true;
    }
    return false;
}

bool BrightnessPanel::wheel(double, double, double dy) {
    if (!enabled_ || dy == 0.0) {
        return false;
    }
    apply(percent_ + (dy < 0 ? cfg::brightness_wheel_step : -cfg::brightness_wheel_step));
    return true;
}

} // namespace astralia
