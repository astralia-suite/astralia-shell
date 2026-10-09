#pragma once

#include <string>
#include <vector>

#include "modules/bar/panel/panel.h"

#include "service/bluetooth_service.h"

namespace astralia {

struct BluetoothRow {
    enum class Kind { message,
                      section,
                      device } kind;
    float height;
    std::string text;
    const BluetoothDevice *device = nullptr;
};

std::vector<BluetoothRow> bluetooth_rows(const BluetoothService &bluetooth);
const char *bluetooth_kind_glyph(BluetoothDeviceKind kind);

class BluetoothPanel final : public PanelContent {
  public:
    explicit BluetoothPanel(BluetoothService &bluetooth);

    std::string_view title() const override { return "Bluetooth"; }
    void opened() override;
    void closed() override;
    float content_height(ui::Canvas &canvas) override;
    void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) override;
    void paint_header(ui::Canvas &canvas, const ui::Box &area, PanelPaint &paint) override;
    float dialog_height() override;
    void paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) override;
    bool dismiss_dialog() override;
    bool activate(const PanelRegion &region, double x, double y) override;
    bool key(const input::KeyEvent &event) override;

  private:
    const BluetoothDevice *dialog_device() const;

    BluetoothService &bluetooth_;
    std::string dialog_path_;
    int dialog_action_ = 0;
};

} // namespace astralia
