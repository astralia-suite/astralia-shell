#pragma once

#include <array>

#include "modules/bar/panel/panel.h"

namespace astralia {

struct CalendarDay {
    int year;
    int month;
    int day;
    bool in_month;
};

struct CalendarMonth {
    int year;
    int month;
};

std::array<CalendarDay, 42> clock_panel_cells(int year, int month);
CalendarMonth clock_panel_month_shifted(int year, int month, int delta);
bool clock_panel_same_day(const CalendarDay &cell, int year, int month, int day);
int clock_panel_iso_week(int year, int month, int day);

class ClockPanel final : public PanelContent {
  public:
    std::string_view title() const override { return "Clock"; }
    PanelAnchor anchor() const override { return PanelAnchor::center; }
    bool has_header() const override { return false; }
    float width() const override { return panel_config::clock_width; }
    void closed() override { offset_ = 0; }
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool scrollable() const override { return false; }

    int month_offset() const { return offset_; }

  private:
    int offset_ = 0;
};

} // namespace astralia
