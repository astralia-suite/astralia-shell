#pragma once

#include <string>
#include <string_view>

#include "modules/bar/panel/core/panel.h"

#include "render/canvas.h"
#include "render/marquee.h"
#include "render/tokens.h"

namespace astralia::bar::widgets {

void slider(ui::Canvas &canvas, const ui::Box &box, float value, bool dimmed, bool focused = false,
            float track = panel_config::slider_track);
float slider_value(const ui::Box &box, double x);
void flat_bar(ui::Canvas &canvas, const ui::Box &box, float fraction, float min_fill,
              const Color &track, const Color &fill);
void centered_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style,
                   const ui::Box &box, const Color &color);
void text_in_row(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x,
                 const ui::Box &row, const Color &color);
float right_text(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float right,
                 const ui::Box &row, const Color &color);

void toggle(ui::Canvas &canvas, const ui::Box &box, bool on);

void icon_button(ui::Canvas &canvas, const ui::Box &box, const char *glyph, const Color &color);

struct DeviceRow {
    const char *glyph = nullptr;
    std::string title;
    std::string subtitle;
    Color background;
    Color glyph_color;
    Color title_color;
    Color subtitle_color;
    bool connected = false;
    bool busy = false;
    bool can_forget = false;
    MarqueeTextState *marquee = nullptr;
    AnimationManager *animations = nullptr;
};

void device_row(ui::Canvas &canvas, const ui::Box &box, const DeviceRow &row, PanelPaint &paint,
                int connect_id, int forget_id, int index);

void dialog_top(ui::Canvas &canvas, const ui::Box &box, std::string_view label, PanelPaint &paint,
                int close_id);
void confirm_dialog(ui::Canvas &canvas, const ui::Box &box, std::string_view label,
                    std::string_view prompt, std::string_view confirm_label, PanelPaint &paint,
                    int cancel_id, int confirm_id);
void pairing_dialog(ui::Canvas &canvas, const ui::Box &box, std::string_view title,
                    std::string_view code, std::string_view hint, PanelPaint &paint, int cancel_id,
                    int yes_id = 0);

} // namespace astralia::bar::widgets
