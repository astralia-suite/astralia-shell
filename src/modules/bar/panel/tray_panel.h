#pragma once

#include <string>
#include <vector>

#include "modules/bar/panel/panel.h"

#include "service/tray_service.h"

namespace astralia {

struct TrayMenuRow {
    enum class Kind { back,
                      loading,
                      separator,
                      entry } kind;
    float height;
    const TrayMenuEntry *entry = nullptr;
};

int tray_columns(float width);
std::vector<TrayMenuRow> tray_menu_rows(const std::vector<TrayMenuEntry> *level, bool show_back);
const std::vector<TrayMenuEntry> *tray_menu_level(const std::vector<TrayMenuEntry> *root, const std::vector<int32_t> &path);

class TrayPanel final : public PanelContent {
  public:
    explicit TrayPanel(TrayService &tray);

    std::string_view title() const override { return "Tray"; }
    void closed() override;
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    float dialog_height() override;
    void paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) override;
    bool dismiss_dialog() override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool wheel(double x, double y, double dy) override;
    bool key(const input::KeyEvent &event) override;

  private:
    const std::vector<TrayMenuEntry> *level() const;
    void close_menu();

    TrayService &tray_;
    std::string menu_key_;
    std::vector<int32_t> path_;
    float menu_scroll_ = 0.0f;
};

} // namespace astralia
