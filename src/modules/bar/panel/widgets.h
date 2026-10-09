#pragma once

#include <string>
#include <string_view>

#include "modules/bar/panel/panel.h"

#include "ui/canvas.h"
#include "ui/tokens.h"

namespace astralia::panel_widgets {

void slider(ui::Canvas &canvas, const ui::Box &box, float value, bool dimmed);
float slider_value(const ui::Box &box, double x);
void flat_bar(ui::Canvas &canvas, const ui::Box &box, float fraction, float min_fill, const Color &track, const Color &fill);
void centered_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, const ui::Box &box, const Color &color);
void text_in_row(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x, const ui::Box &row, const Color &color);
float right_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float right, const ui::Box &row, const Color &color);

void toggle(ui::Canvas &canvas, const ui::Box &box, bool on);

struct DeviceRow {
    const char *glyph = nullptr;
    std::string title;
    std::string subtitle;
    Color background;
    Color foreground;
    float reserve_right = 0.0f;
};

void device_row(ui::Canvas &canvas, const ui::Box &box, const DeviceRow &row);

float confirm_height();
void confirm_dialog(ui::Canvas &canvas, const ui::Box &box, std::string_view title, std::string_view prompt, std::string_view confirm_label, PanelPaint &paint, int cancel_id, int confirm_id);

} // namespace astralia::panel_widgets
