#pragma once

#include "modules/bar/panel/panel.h"

#include "service/media_service.h"

#include "render/marquee.h"

namespace astralia {

class MediaPanel final : public PanelContent {
  public:
    explicit MediaPanel(MediaService &media);

    std::string_view title() const override { return "Media"; }
    PanelAnchor anchor() const override { return PanelAnchor::center; }
    void opened() override { media_.poll_position(); }
    std::chrono::milliseconds refresh_interval() const override { return std::chrono::milliseconds(panel_config::media_poll_ms); }
    void refresh() override { media_.poll_position(); }
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool scrollable() const override { return false; }

  private:
    MediaService &media_;
    MarqueeTextState title_marquee_;
    MarqueeTextState artist_marquee_;
};

} // namespace astralia
