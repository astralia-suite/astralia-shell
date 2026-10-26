#pragma once

#include <span>
#include <string_view>

#include "modules/bar/paint/style.h"

#include "render/canvas.h"

namespace astralia::bar {

struct IconCell {
    const char *glyph;
    float icon_w;
    std::string_view label;
    float label_w;
    float amount;
};

float icon_cell_extent(float icon_w, float label_w, float amount);

Color chip_fill();

void paint_ringed(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                  const Color &fill);
void paint_pill(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box, const Color &fill);
void paint_icon_label(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                      const char *glyph, std::string_view label, float label_alpha);
void paint_two_lines(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                     const char *glyph, std::string_view title, std::string_view detail,
                     float amount);
void paint_badge(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                 std::string_view text, const Color &fill);
void paint_icon_row(ui::Canvas &canvas, const StyleSpec &style, const ui::Box &box,
                    std::span<const IconCell> cells, float gap);

} // namespace astralia::bar
