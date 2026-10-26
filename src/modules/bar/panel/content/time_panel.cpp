#include <ctime>

#include "modules/bar/panel/content/time_panel.h"
#include "modules/bar/panel/core/widgets.h"

namespace astralia::bar {

namespace {

namespace cfg = panel_config;

constexpr float total_height =
    cfg::time_big_row + cfg::time_gap + cfg::time_line + cfg::time_gap + cfg::time_zone_line;

constexpr ui::TextStyle big_style{ui::FontFamily::text, cfg::time_big_px};
constexpr ui::TextStyle seconds_style{ui::FontFamily::text, cfg::time_seconds_px};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};

std::string stamp(const char *pattern, const std::tm &local) {
    char buffer[48]{};
    return std::string(buffer, std::strftime(buffer, sizeof buffer, pattern, &local));
}

} // namespace

float TimePanel::content_height(ui::Canvas &) { return total_height; }

void TimePanel::paint(ui::Canvas &canvas, const ui::Box &view, float, PanelPaint &) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    float y = view.y;
    auto line = [&](const std::string &text, const ui::TextStyle &style, float height,
                    const Color &color) {
        widgets::centered_text(canvas, text, style, {view.x, y, view.w, height}, color);
        y += height + cfg::time_gap;
    };
    line(stamp("%I:%M", local), big_style, cfg::time_big_row, palette::text);
    line(stamp("%S %p", local), seconds_style, cfg::time_line, palette::text);
    line(stamp("%Z (UTC%z)", local), text_style, cfg::time_zone_line, palette::text_muted);
}

} // namespace astralia::bar
