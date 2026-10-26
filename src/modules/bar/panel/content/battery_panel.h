#pragma once

#include <string>
#include <vector>

#include "modules/bar/panel/core/panel.h"

#include "service/battery_service.h"

namespace astralia::bar {

enum class BatteryRowKind { empty_plugged_in, empty_no_battery, device, spacer };

struct BatteryRow {
    BatteryRowKind kind;
    float height;
    const BatteryDevice *device = nullptr;
};

std::string battery_time_text(int seconds);
std::vector<BatteryRow> battery_rows(const BatteryService &battery);

class BatteryPanel final : public PanelContent {
  public:
    explicit BatteryPanel(BatteryService &battery);

    std::string_view title() const override { return "Battery"; }
    const Color &tint() const override { return palette::pop_b; }
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;

  private:
    BatteryService &battery_;
};

} // namespace astralia::bar
