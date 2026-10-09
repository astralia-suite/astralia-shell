#pragma once

#include <string>
#include <vector>

#include "modules/bar/panel/panel.h"

#include "service/audio_service.h"

namespace astralia {

struct VolumeRow {
    enum class Kind { label,
                      slider,
                      section,
                      divider,
                      app,
                      device } kind;
    float height;
    std::string text{};
    std::string detail{};
    AudioNode node{};
    bool selected = false;
};

std::vector<VolumeRow> volume_rows(const AudioService &audio);

class VolumePanel final : public PanelContent {
  public:
    explicit VolumePanel(AudioService &audio);

    std::string_view title() const override { return "Volume"; }
    void opened() override;
    void closed() override;
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool drag(const PanelRegion &region, double x, double y) override;
    void drop(const PanelRegion &) override { dragging_ = 0; }
    bool hover(int id, int a) override;
    bool key(const input::KeyEvent &event) override;
    bool wheel(double x, double y, double dy) override;

    uint32_t selected() const { return selected_; }

  private:
    void step(uint32_t id, int delta);
    void set_percent(uint32_t id, int percent);

    AudioService &audio_;
    uint32_t selected_ = 0;
    uint32_t dragging_ = 0;
    uint32_t hovered_ = 0;
};

} // namespace astralia
