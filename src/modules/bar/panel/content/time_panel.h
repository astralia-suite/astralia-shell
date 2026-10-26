#pragma once

#include "modules/bar/panel/core/panel.h"

namespace astralia::bar {

class TimePanel final : public PanelContent {
  public:
    std::string_view title() const override { return "Time"; }
    const Color &tint() const override { return palette::pop_a; }
    PanelAnchor anchor() const override { return PanelAnchor::center; }
    bool has_header() const override { return false; }
    float width() const override { return panel_config::time_width; }
    std::chrono::milliseconds refresh_interval() const override {
        return std::chrono::milliseconds(panel_config::time_poll_ms);
    }
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    bool scrollable() const override { return false; }
};

} // namespace astralia::bar
