#pragma once

#include <string>

#include "modules/bar/panel/panel.h"

#include "service/telemetry_service.h"

namespace astralia {

std::string resource_format_ghz(float ghz);
std::string resource_format_percent(float value01);
std::string resource_format_celsius(float celsius);
std::string resource_format_used_cap(float used_gb, float total_gb);

class ResourcePanel final : public PanelContent {
  public:
    ResourcePanel(CpuTempState &cpu, GpuTempState &gpu, SystemStatsState &stats);

    std::string_view title() const override { return "Resource"; }
    float max_height() const override { return panel_config::resource_max_height; }
    void opened() override { refresh(); }
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    std::chrono::milliseconds refresh_interval() const override { return std::chrono::milliseconds(panel_config::resource_poll_ms); }
    void refresh() override;

  private:
    CpuTempState &cpu_;
    GpuTempState &gpu_;
    SystemStatsState &stats_;
};

} // namespace astralia
