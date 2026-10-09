#pragma once

#include "modules/bar/panel/panel.h"

#include "service/brightness_service.h"

namespace astralia {

class BrightnessPanel final : public PanelContent {
  public:
    explicit BrightnessPanel(BrightnessService &brightness);

    std::string_view title() const override { return "Brightness"; }
    void opened() override;
    float content_height(ui::Canvas &) override { return panel_config::brightness_row; }
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool drag(const PanelRegion &region, double x, double y) override;
    void drop(const PanelRegion &) override {
        dragging_ = false;
        sync();
    }
    bool hover(int id, int a) override;
    bool key(const input::KeyEvent &event) override;
    bool wheel(double x, double y, double dy) override;
    bool scrollable() const override { return false; }

    int percent() const { return percent_; }
    bool enabled() const { return enabled_; }
    void apply(int percent);

  private:
    void sync();

    BrightnessService &brightness_;
    int percent_ = 0;
    bool enabled_ = false;
    bool dragging_ = false;
    bool hovered_ = false;
};

} // namespace astralia
