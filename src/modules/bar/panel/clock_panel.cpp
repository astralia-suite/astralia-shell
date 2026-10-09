#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#include "config/icons.h"

#include "modules/bar/panel/clock_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { previous = 1,
              today,
              next };

constexpr std::array<const char *, 12> month_names = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr std::array<const char *, 7> weekday_names = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

constexpr float left_width = (cfg::clock_width - 2.0f * cfg::padding - cfg::clock_column_gap) / 2.0f;
constexpr float cell_size = (cfg::clock_width - 2.0f * cfg::padding - left_width - cfg::clock_column_gap) / 7.0f;
constexpr float left_height = cfg::clock_weekday_line + cfg::clock_line_gap + cfg::clock_date_line + cfg::clock_line_gap + cfg::clock_date_line + cfg::clock_big_day_gap + cfg::clock_big_day_row + cfg::clock_big_day_gap + cfg::clock_week_line;
constexpr float grid_height = cfg::clock_grid_header + cfg::clock_grid_header_gap + cfg::clock_weekday_row + cfg::clock_grid_top_gap + 6.0f * cell_size;
constexpr float total_height = left_height > grid_height ? left_height : grid_height;

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle weekday_style{ui::FontFamily::text, cfg::clock_weekday_px};
constexpr ui::TextStyle big_style{ui::FontFamily::text, cfg::clock_big_day_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

std::chrono::year_month_day to_ymd(int year, int month, int day) {
    return std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month + 1)} / std::chrono::day{static_cast<unsigned>(day)};
}

void nav_button(ui::Canvas &canvas, PanelPaint &paint, float x, float y, const char *glyph, int action) {
    float size = cfg::clock_nav_button;
    ui::Box box{x, y, size, size};
    canvas.rounded(box, size / 2.0f, palette::text_alpha08);
    if (glyph == nullptr) {
        float dot = cfg::clock_today_dot;
        canvas.rounded({x + (size - dot) / 2.0f, y + (size - dot) / 2.0f, dot, dot}, dot / 2.0f, palette::accent);
    } else {
        panel_widgets::centered_text(canvas, glyph, icon_style, box, palette::text);
    }
    paint.region(box, action);
}

} // namespace

std::array<CalendarDay, 42> clock_panel_cells(int year, int month) {
    std::chrono::sys_days first{to_ymd(year, month, 1)};
    unsigned offset = std::chrono::weekday{first}.iso_encoding() - 1;
    std::chrono::sys_days start = first - std::chrono::days{offset};
    std::array<CalendarDay, 42> cells{};
    for (int i = 0; i < 42; ++i) {
        std::chrono::year_month_day ymd{start + std::chrono::days{i}};
        int cell_month = static_cast<int>(static_cast<unsigned>(ymd.month())) - 1;
        cells[static_cast<std::size_t>(i)] = {static_cast<int>(ymd.year()), cell_month, static_cast<int>(static_cast<unsigned>(ymd.day())), cell_month == month};
    }
    return cells;
}

CalendarMonth clock_panel_month_shifted(int year, int month, int delta) {
    std::chrono::year_month shifted = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month + 1)} + std::chrono::months{delta};
    return {static_cast<int>(shifted.year()), static_cast<int>(static_cast<unsigned>(shifted.month())) - 1};
}

bool clock_panel_same_day(const CalendarDay &cell, int year, int month, int day) {
    return cell.year == year && cell.month == month && cell.day == day;
}

int clock_panel_iso_week(int year, int month, int day) {
    std::chrono::sys_days date{to_ymd(year, month, day)};
    std::chrono::sys_days thursday = date + std::chrono::days{4 - static_cast<int>(std::chrono::weekday{date}.iso_encoding())};
    std::chrono::sys_days jan1{std::chrono::year_month_day{thursday}.year() / std::chrono::January / 1};
    return static_cast<int>((thursday - jan1).count() / 7) + 1;
}

float ClockPanel::content_height(ui::Canvas &) {
    return total_height + 1.0f;
}

void ClockPanel::paint(ui::Canvas &canvas, const ui::Box &view, float, PanelPaint &paint) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    int year = local.tm_year + 1900;
    int month = local.tm_mon;
    int day = local.tm_mday;
    CalendarMonth shown = clock_panel_month_shifted(year, month, offset_);

    char weekday[24]{};
    std::strftime(weekday, sizeof weekday, "%A", &local);
    char month_name[24]{};
    std::strftime(month_name, sizeof month_name, "%B", &local);
    float y = view.y + (total_height - left_height) / 2.0f;
    auto line = [&](const std::string &text, const ui::TextStyle &style, float height, const Color &color) {
        panel_widgets::centered_text(canvas, text, style, {view.x, y, left_width, height}, color);
    };
    line(weekday, weekday_style, cfg::clock_weekday_line, palette::text);
    y += cfg::clock_weekday_line + cfg::clock_line_gap;
    line(month_name, text_style, cfg::clock_date_line, palette::text_muted);
    y += cfg::clock_date_line + cfg::clock_line_gap;
    line(std::to_string(year), text_style, cfg::clock_date_line, palette::text_muted);
    y += cfg::clock_date_line + cfg::clock_big_day_gap;
    line(std::to_string(day), big_style, cfg::clock_big_day_row, palette::text);
    y += cfg::clock_big_day_row + cfg::clock_big_day_gap;
    line("Week " + std::to_string(clock_panel_iso_week(year, month, day)), text_style, cfg::clock_week_line, palette::text_dim);

    float grid_x = view.x + left_width + cfg::clock_column_gap;
    ui::TextSize sample = canvas.measure(weekday_names[0], text_style);
    float inset = (cell_size - sample.w) / 2.0f;
    std::string heading = std::string(month_names[static_cast<std::size_t>(shown.month)]) + " " + std::to_string(shown.year);
    panel_widgets::text_in_row(canvas, heading, text_style, grid_x + inset, {grid_x, view.y, 7.0f * cell_size, cfg::clock_grid_header}, palette::text);

    float nav = cfg::clock_nav_button;
    float nav_y = view.y + (cfg::clock_grid_header - nav) / 2.0f;
    float nav_x = grid_x + 7.0f * cell_size - nav;
    nav_button(canvas, paint, nav_x, nav_y, icon::chevron_right, next);
    nav_x -= nav + cfg::clock_nav_gap;
    nav_button(canvas, paint, nav_x, nav_y, nullptr, today);
    nav_x -= nav + cfg::clock_nav_gap;
    nav_button(canvas, paint, nav_x, nav_y, icon::chevron_left, previous);

    float row_y = view.y + cfg::clock_grid_header + cfg::clock_grid_header_gap;
    for (size_t col = 0; col < weekday_names.size(); ++col) {
        panel_widgets::centered_text(canvas, weekday_names[col], text_style, {grid_x + static_cast<float>(col) * cell_size, row_y, cell_size, cfg::clock_weekday_row}, palette::text);
    }
    float grid_y = row_y + cfg::clock_weekday_row + cfg::clock_grid_top_gap;
    std::array<CalendarDay, 42> cells = clock_panel_cells(shown.year, shown.month);
    for (size_t i = 0; i < cells.size(); ++i) {
        const CalendarDay &entry = cells[i];
        float cx = grid_x + static_cast<float>(i % 7) * cell_size;
        float cy = grid_y + static_cast<float>(i / 7) * cell_size;
        bool is_today = clock_panel_same_day(entry, year, month, day);
        if (is_today) {
            float radius = cell_size / 2.0f - cfg::clock_cell_padding;
            canvas.rounded({cx + cell_size / 2.0f - radius, cy + cell_size / 2.0f - radius, 2.0f * radius, 2.0f * radius}, radius, palette::accent);
        }
        panel_widgets::centered_text(canvas, std::to_string(entry.day), text_style, {cx, cy, cell_size, cell_size}, is_today || entry.in_month ? palette::text : palette::text_dim);
    }
}

bool ClockPanel::activate(const PanelRegion &region, double, double) {
    offset_ = region.id == previous ? offset_ - 1 : region.id == next ? offset_ + 1
                                                                      : 0;
    return true;
}

} // namespace astralia
