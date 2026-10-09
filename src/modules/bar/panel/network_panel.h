#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "modules/bar/panel/panel.h"

#include "service/network_service.h"

#include "ui/marquee.h"
#include "ui/text_field.h"

namespace astralia {

struct NetworkRow {
    enum class Kind { error,
                      ethernet,
                      no_adapter,
                      disabled,
                      scanning,
                      section_connected,
                      section_known,
                      section_available,
                      network,
                      spacer } kind;
    float height;
    const NetworkInfo *info = nullptr;
};

const char *network_signal_glyph(int percent);
struct NetworkRowsInput {
    const NetworkMap &networks;
    bool has_error = false;
    bool ethernet_connected = false;
    bool wifi_available = false;
    bool wifi_enabled = false;
};

std::vector<NetworkRow> network_rows(const NetworkRowsInput &input);

class NetworkPanel final : public PanelContent {
  public:
    explicit NetworkPanel(NetworkService &network);

    std::string_view title() const override { return "Network"; }
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
    bool wants_text() const override { return dialog_ == Dialog::password; }
    ui::Box cursor() const override;

  private:
    enum class Dialog { none,
                        password,
                        disconnect,
                        forget };

    NetworkRowsInput rows_input() const;
    void open_dialog(Dialog dialog, const std::string &ssid);
    void submit();

    NetworkService &network_;
    Dialog dialog_ = Dialog::none;
    std::string ssid_;
    TextFieldState password_;
    TextFieldTypeAnim password_anim_;
    std::unordered_map<std::string, MarqueeTextState> marquees_;
};

} // namespace astralia
