#include <algorithm>
#include <cstring>

#include "config/icons.h"
#include "config/polkit_config.h"

#include "modules/bar/panel/network_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { wifi = 1,
              rescan,
              dismiss_error,
              network,
              forget,
              cancel,
              confirm };

constexpr uint64_t anim_owner = 10000;
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

int signal_band(int percent) {
    if (percent > 75) {
        return 3;
    }
    if (percent > 50) {
        return 2;
    }
    if (percent > 25) {
        return 1;
    }
    return 0;
}

std::vector<const NetworkInfo *> by_signal(std::vector<const NetworkInfo *> list) {
    std::ranges::sort(list, [](const NetworkInfo *a, const NetworkInfo *b) {
        int ba = signal_band(a->signal);
        int bb = signal_band(b->signal);
        return ba != bb ? ba > bb : a->signal > b->signal;
    });
    return list;
}

bool secured(const NetworkInfo &info) {
    return !info.security.empty() && info.security != "--";
}

std::string elide(const std::string &text, size_t max) {
    return text.size() <= max ? text : text.substr(0, max - 1) + "\xE2\x80\xA6";
}

} // namespace

const char *network_signal_glyph(int percent) {
    switch (signal_band(percent)) {
    case 3:
        return icon::wifi;
    case 2:
        return icon::wifi2;
    case 1:
        return icon::wifi1;
    default:
        return icon::wifi0;
    }
}

std::vector<NetworkRow> network_rows(const NetworkRowsInput &in) {
    using Kind = NetworkRow::Kind;
    std::vector<NetworkRow> rows;
    if (in.has_error) {
        rows.push_back({Kind::error, cfg::network_banner});
    }
    if (in.ethernet_connected) {
        rows.push_back({Kind::ethernet, cfg::network_ethernet});
    }
    if (!in.wifi_available) {
        rows.push_back({Kind::no_adapter, cfg::network_state});
    } else if (!in.wifi_enabled) {
        rows.push_back({Kind::disabled, cfg::network_state});
    }
    if (in.wifi_available && in.wifi_enabled && network_visible_count(in.networks) == 0) {
        rows.push_back({Kind::scanning, cfg::network_scanning});
    }
    std::vector<const NetworkInfo *> connected;
    std::vector<const NetworkInfo *> saved;
    std::vector<const NetworkInfo *> available;
    for (const auto &[ssid, info] : in.networks) {
        if (info.connected) {
            connected.push_back(&info);
        } else if (info.existing && info.in_range) {
            saved.push_back(&info);
        } else if (!info.existing) {
            available.push_back(&info);
        }
    }
    auto bucket = [&](Kind section, float gap, const std::vector<const NetworkInfo *> &list) {
        if (list.empty()) {
            return;
        }
        rows.push_back({section, gap});
        for (const NetworkInfo *info : by_signal(list)) {
            rows.push_back({Kind::network, cfg::device_row_height, info});
        }
    };
    bucket(Kind::section_connected, cfg::network_section_first, connected);
    bucket(Kind::section_known, cfg::network_section, saved);
    bucket(Kind::section_available, cfg::network_section, available);
    rows.push_back({Kind::spacer, cfg::trailing_spacer});
    return rows;
}

NetworkRowsInput NetworkPanel::rows_input() const {
    return {network_.networks(), !network_.last_error().empty(), network_.ethernet_connected(), network_.wifi_available(), network_.wifi_enabled()};
}

NetworkPanel::NetworkPanel(NetworkService &network) : network_(network) {
    network_.changed.connect(notifier());
}

void NetworkPanel::opened() {
    open_dialog(Dialog::none, {});
    network_.start_watch();
}

void NetworkPanel::closed() {
    open_dialog(Dialog::none, {});
    network_.stop_watch();
}

void NetworkPanel::open_dialog(Dialog dialog, const std::string &ssid) {
    if (!password_.text.empty()) {
        explicit_bzero(password_.text.data(), password_.text.size());
    }
    password_.text.clear();
    password_.preedit.clear();
    text_field_type_anim_clear(password_anim_, animations(), anim_owner);
    dialog_ = dialog;
    ssid_ = ssid;
}

float NetworkPanel::content_height(ui::Canvas &) {
    float height = 0.0f;
    std::vector<NetworkRow> rows = network_rows(rows_input());
    for (size_t i = 0; i < rows.size(); ++i) {
        height += (i > 0 ? cfg::list_spacing : 0.0f) + rows[i].height;
    }
    return height;
}

void NetworkPanel::paint_header(ui::Canvas &canvas, const ui::Box &area, PanelPaint &paint) {
    if (!network_.wifi_available()) {
        return;
    }
    ui::Box toggle{area.x + area.w - 36.0f, area.y + (area.h - 20.0f) / 2.0f, 36.0f, 20.0f};
    panel_widgets::toggle(canvas, toggle, network_.wifi_enabled());
    paint.region(toggle, wifi);
    float size = cfg::close_button;
    ui::Box scan{toggle.x - cfg::row_gap - size, area.y + (area.h - size) / 2.0f, size, size};
    canvas.rounded(scan, size / 2.0f, palette::overlay);
    panel_widgets::centered_text(canvas, icon::refresh, icon_style, scan, network_.scanning() ? palette::accent : palette::text);
    paint.region(scan, rescan);
}

void NetworkPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) {
    using Kind = NetworkRow::Kind;
    std::vector<NetworkRow> rows = network_rows(rows_input());
    float y = view.y - scroll;
    for (size_t i = 0; i < rows.size(); ++i) {
        const NetworkRow &row = rows[i];
        if (i > 0) {
            y += cfg::list_spacing;
        }
        ui::Box box{view.x, y, view.w, row.height};
        y += row.height;
        if (box.y + box.h < view.y || box.y > view.y + view.h) {
            continue;
        }
        switch (row.kind) {
        case Kind::error: {
            canvas.rounded(box, metrics::radius_md - 2.0f, palette::critical_alpha15);
            ui::Box glyph{box.x + cfg::row_icon_gap, box.y, cfg::icon_px, box.h};
            panel_widgets::centered_text(canvas, icon::alert_triangle, icon_style, glyph, palette::critical);
            ui::Box close{box.x + box.w - cfg::row_icon_gap - 18.0f, box.y + (box.h - 18.0f) / 2.0f, 18.0f, 18.0f};
            panel_widgets::centered_text(canvas, icon::close, icon_style, close, palette::critical);
            panel_widgets::text_in_row(canvas, elide(network_.last_error(), cfg::network_error_chars), small_style, glyph.x + glyph.w + cfg::row_gap, box, palette::critical);
            paint.region(close, dismiss_error);
            break;
        }
        case Kind::ethernet:
            canvas.rounded(box, metrics::radius_md - 2.0f, palette::accent_alpha19);
            panel_widgets::centered_text(canvas, "Ethernet", text_style, box, palette::text);
            break;
        case Kind::no_adapter:
            panel_widgets::centered_text(canvas, "No Wi-Fi adapter found", text_style, box, palette::text_dim);
            break;
        case Kind::disabled:
            panel_widgets::centered_text(canvas, "WiFi is disabled", text_style, box, palette::text_dim);
            break;
        case Kind::scanning:
            panel_widgets::centered_text(canvas, "Scanning\xE2\x80\xA6", text_style, box, palette::text_dim);
            break;
        case Kind::section_connected:
        case Kind::section_known:
        case Kind::section_available: {
            const char *label = row.kind == Kind::section_connected ? "Connected" : row.kind == Kind::section_known ? "Known"
                                                                                                                    : "Available";
            ui::TextSize size = canvas.measure(label, small_style);
            canvas.text(label, small_style, box.x, box.y + box.h - size.h, palette::text_dim);
            break;
        }
        case Kind::network: {
            const NetworkInfo &info = *row.info;
            bool portal = info.connected && (network_.connectivity() == "portal" || network_.status().portal);
            bool busy = network_.connecting_to() == info.ssid;
            bool can_forget = info.existing && !info.connected && !busy;
            panel_widgets::DeviceRow device;
            device.glyph = network_signal_glyph(info.signal);
            device.title = info.ssid;
            device.subtitle = portal ? "Sign in required" : busy        ? "Connecting\xE2\x80\xA6"
                                                        : secured(info) ? info.security
                                                                        : "Open";
            device.background = portal ? with_alpha(palette::warn, 0.15f) : info.connected ? palette::accent_alpha25
                                                                        : busy             ? palette::accent_alpha12
                                                                                           : palette::overlay;
            device.foreground = portal ? palette::warn : info.connected ? palette::accent
                                                                        : palette::text;
            device.reserve_right = can_forget ? cfg::close_button + cfg::row_icon_gap : 0.0f;
            panel_widgets::device_row(canvas, box, device);
            std::string key = info.ssid;
            int index = static_cast<int>(std::ranges::distance(network_.networks().begin(), network_.networks().find(key)));
            if (can_forget) {
                ui::Box button{box.x + box.w - cfg::close_button - cfg::row_icon_gap, box.y + (box.h - cfg::close_button) / 2.0f, cfg::close_button, cfg::close_button};
                panel_widgets::centered_text(canvas, icon::close, icon_style, button, palette::text_muted);
                paint.region(button, forget, index);
            }
            paint.region(box, network, index);
            break;
        }
        case Kind::spacer:
            break;
        }
    }
}

float NetworkPanel::dialog_height() {
    if (dialog_ == Dialog::none) {
        return 0.0f;
    }
    if (dialog_ == Dialog::password) {
        return cfg::padding + cfg::header_height + cfg::network_field_height + cfg::row_gap + 28.0f + cfg::padding;
    }
    return panel_widgets::confirm_height();
}

void NetworkPanel::paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) {
    if (dialog_ == Dialog::disconnect || dialog_ == Dialog::forget) {
        bool leaving = dialog_ == Dialog::disconnect;
        panel_widgets::confirm_dialog(canvas, box, ssid_, leaving ? "Disconnect?" : "Forget?", leaving ? "Disconnect" : "Forget", paint, cancel, confirm);
        return;
    }
    if (dialog_ != Dialog::password) {
        return;
    }
    canvas.rounded(box, metrics::radius_md, palette::overlay, metrics::border_thin, palette::accent);
    float x = box.x + cfg::padding;
    float width = box.w - 2.0f * cfg::padding;
    float y = box.y + cfg::padding;
    ui::TextStyle title_style = text_style;
    title_style.max_width = static_cast<int>(width);
    panel_widgets::text_in_row(canvas, ssid_, title_style, x, {x, y, width, cfg::header_height}, palette::text);
    y += cfg::header_height;
    ui::Box field{x, y, width, cfg::network_field_height};
    canvas.rounded(field, 6.0f, palette::text_alpha08, 1.0f, palette::text_alpha15);
    constexpr float dot = 8.0f;
    constexpr float gap = 4.0f;
    float cy = field.y + field.h / 2.0f;
    size_t count = text_field_utf8_len(password_.text);
    if (count == 0 && password_.preedit.empty()) {
        panel_widgets::centered_text(canvas, "Password\xE2\x80\xA6", small_style, field, palette::text_dim);
    } else {
        ui::ImageId echo = canvas.image(polkit_config::echo_file_path, static_cast<int>(dot));
        size_t visible = std::min(count, static_cast<size_t>(std::max(1.0f, (width - 2.0f * cfg::row_icon_gap + gap) / (dot + gap))));
        float row = static_cast<float>(visible) * dot + static_cast<float>(visible > 0 ? visible - 1 : 0) * gap;
        float dx = field.x + (field.w - row) / 2.0f;
        size_t first = count - visible;
        for (size_t i = 0; i < visible; ++i) {
            size_t index = first + i;
            float scale = index < password_anim_.chars.size() ? password_anim_.chars[index].scale : 1.0f;
            float slide = index < password_anim_.chars.size() ? password_anim_.chars[index].slide_x : 0.0f;
            float size = dot * scale;
            ui::Box d{dx + static_cast<float>(i) * (dot + gap) + (dot - size) / 2.0f + slide, cy - size / 2.0f, size, size};
            if (echo != ui::no_image) {
                canvas.draw_image(echo, d, palette::text);
            } else {
                canvas.rounded(d, size / 2.0f, palette::text);
            }
        }
        if (!password_.preedit.empty()) {
            ui::TextSize size = canvas.measure(password_.preedit, small_style);
            canvas.text(password_.preedit, small_style, field.x + (field.w - size.w) / 2.0f, field.y + field.h + 2.0f, palette::text_dim);
        }
    }
    y += cfg::network_field_height + cfg::row_gap;
    bool ready = count >= static_cast<size_t>(cfg::network_password_min);
    float button_w = canvas.measure("Connect", text_style).w + 32.0f;
    ui::Box button{x + (width - button_w) / 2.0f, y, button_w, 28.0f};
    canvas.rounded(button, 6.0f, with_alpha(palette::accent, ready ? 1.0f : 0.4f));
    panel_widgets::centered_text(canvas, "Connect", text_style, button, palette::text);
    if (ready) {
        paint.region(button, confirm);
    }
    paint.region({box.x, box.y, box.w, cfg::padding}, cancel);
}

ui::Box NetworkPanel::cursor() const {
    float width = this->width();
    return {width / 2.0f, cfg::padding + cfg::header_height, 1.0f, cfg::network_field_height};
}

bool NetworkPanel::dismiss_dialog() {
    if (dialog_ == Dialog::none) {
        return false;
    }
    open_dialog(Dialog::none, {});
    return true;
}

void NetworkPanel::submit() {
    if (text_field_utf8_len(password_.text) < static_cast<size_t>(cfg::network_password_min)) {
        return;
    }
    network_.connect(ssid_, password_.text);
    open_dialog(Dialog::none, {});
}

bool NetworkPanel::activate(const PanelRegion &region, double, double) {
    switch (region.id) {
    case wifi:
        network_.set_wifi_enabled(!network_.wifi_enabled());
        return true;
    case rescan:
        network_.scan();
        return true;
    case dismiss_error:
        network_.clear_error();
        return true;
    case network:
    case forget: {
        const NetworkMap &map = network_.networks();
        if (region.a < 0 || static_cast<size_t>(region.a) >= map.size()) {
            return true;
        }
        const NetworkInfo &info = std::next(map.begin(), region.a)->second;
        std::string ssid = info.ssid;
        if (region.id == forget) {
            open_dialog(Dialog::forget, ssid);
        } else if (info.connected) {
            open_dialog(Dialog::disconnect, ssid);
        } else if (info.existing || !secured(info)) {
            network_.connect(ssid, "");
        } else {
            open_dialog(Dialog::password, ssid);
        }
        return true;
    }
    case cancel:
        open_dialog(Dialog::none, {});
        return true;
    case confirm:
        if (dialog_ == Dialog::password) {
            submit();
        } else {
            std::string ssid = ssid_;
            Dialog dialog = dialog_;
            open_dialog(Dialog::none, {});
            if (dialog == Dialog::disconnect) {
                network_.disconnect(ssid);
            } else if (dialog == Dialog::forget) {
                network_.forget(ssid);
            }
        }
        return true;
    default:
        return false;
    }
}

bool NetworkPanel::key(const input::KeyEvent &event) {
    if (dialog_ == Dialog::password) {
        switch (text_field_handle_key(password_, event)) {
        case TextFieldResult::Committed:
            submit();
            return true;
        case TextFieldResult::Cancelled:
            open_dialog(Dialog::none, {});
            return true;
        case TextFieldResult::Changed:
            text_field_type_anim_sync(password_anim_, animations(), anim_owner, password_.text);
            return true;
        case TextFieldResult::None:
            return true;
        }
    }
    return event.kind == input::KeyKind::Escape && dismiss_dialog();
}

} // namespace astralia
