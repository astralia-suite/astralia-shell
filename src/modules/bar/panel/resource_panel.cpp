#include <algorithm>
#include <cstdio>

#include "modules/bar/panel/resource_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

constexpr Color gauge_cpu = color("#ef4444");
constexpr Color gauge_gpu = color("#a855f7");
constexpr Color gauge_ram = color("#3b82f6");
constexpr Color gauge_disk = color("#22c55e");
constexpr Color temp_warn_color = color("#f97316");
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::text_px};

const Color &temp_color(float celsius, const Color &base) {
    if (celsius >= cfg::temp_critical) {
        return palette::critical;
    }
    if (celsius >= cfg::temp_warn) {
        return temp_warn_color;
    }
    return base;
}

const Color &usage_color(float value, const Color &base) {
    if (value >= cfg::usage_critical) {
        return palette::critical;
    }
    if (value >= cfg::usage_warn) {
        return temp_warn_color;
    }
    return base;
}

float card_height(float content) {
    return cfg::card_top + cfg::card_header + cfg::card_header_gap + content + cfg::card_bottom;
}

float device_content() {
    return 2.0f * cfg::gauge_diameter + cfg::gauge_row_gap;
}

float line_height(ui::Canvas &canvas) {
    return canvas.measure("RAM", text_style).h + cfg::memory_label_gap + cfg::memory_bar;
}

float memory_content(ui::Canvas &canvas, const SystemStatsState &stats) {
    int lines = stats.disk_pct >= 0.0f ? 2 : 1;
    return static_cast<float>(lines) * line_height(canvas) + static_cast<float>(lines - 1) * cfg::memory_line_gap;
}

ui::Box card(ui::Canvas &canvas, const ui::Box &box, const char *title) {
    canvas.rounded(box, cfg::card_radius, palette::overlay, 2.0f, palette::accent);
    ui::TextSize size = canvas.measure(title, title_style);
    canvas.text(title, title_style, box.x + cfg::card_side, box.y + cfg::card_top + (cfg::card_header - size.h) / 2.0f, palette::text);
    return {box.x + cfg::card_side, box.y + cfg::card_top + cfg::card_header + cfg::card_header_gap, box.w - 2.0f * cfg::card_side, box.h};
}

struct Device {
    const char *title;
    float usage;
    float clock_ghz;
    float celsius;
    const Color &base;
};

void device_card(ui::Canvas &canvas, const ui::Box &box, const Device &d) {
    ui::Box inner = card(canvas, box, d.title);
    float gx = inner.x + (inner.w - cfg::gauge_diameter) / 2.0f;
    int center_w = static_cast<int>(cfg::gauge_diameter - 2.0f * cfg::gauge_stroke);
    ui::TextStyle center = text_style;
    center.max_width = center_w;
    ui::Box top{gx, inner.y, cfg::gauge_diameter, cfg::gauge_diameter};
    canvas.gauge(top, cfg::gauge_stroke, std::max(0.0f, d.usage), usage_color(d.usage, d.base));
    std::string clock = resource_format_ghz(d.clock_ghz);
    std::string usage = resource_format_percent(d.usage);
    ui::TextSize clock_size = canvas.measure(clock, center);
    ui::TextSize usage_size = canvas.measure(usage, center);
    float stack = clock_size.h + cfg::gauge_line_gap + usage_size.h;
    float y = top.y + (top.h - stack) / 2.0f;
    canvas.text(clock, center, top.x + (top.w - clock_size.w) / 2.0f, y, palette::text_dim);
    canvas.text(usage, center, top.x + (top.w - usage_size.w) / 2.0f, y + clock_size.h + cfg::gauge_line_gap, palette::text);
    ui::Box bottom{gx, inner.y + cfg::gauge_diameter + cfg::gauge_row_gap, cfg::gauge_diameter, cfg::gauge_diameter};
    canvas.gauge(bottom, cfg::gauge_stroke, std::clamp(d.celsius / cfg::temp_gauge_max, 0.0f, 1.0f), temp_color(d.celsius, d.base));
    panel_widgets::centered_text(canvas, resource_format_celsius(d.celsius), center, bottom, palette::text);
}

} // namespace

std::string resource_format_ghz(float ghz) {
    if (ghz < 0.0f) {
        return "--";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f GHz", ghz);
    return buf;
}

std::string resource_format_percent(float value01) {
    return value01 < 0.0f ? "--" : std::to_string(static_cast<int>(value01 * 100.0f)) + "%";
}

std::string resource_format_celsius(float celsius) {
    return (celsius < 0.0f ? std::string("--") : std::to_string(static_cast<int>(celsius))) + "\xC2\xB0"
                                                                                              "C";
}

std::string resource_format_used_cap(float used_gb, float total_gb) {
    if (used_gb < 0.0f || total_gb < 0.0f) {
        return "--";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f / %.2f GB", used_gb, total_gb);
    return buf;
}

ResourcePanel::ResourcePanel(CpuTempState &cpu, GpuTempState &gpu, SystemStatsState &stats) : cpu_(cpu), gpu_(gpu), stats_(stats) {}

void ResourcePanel::refresh() {
    cpu_temp_poll(cpu_);
    gpu_temp_poll(gpu_);
    system_stats_poll(stats_);
}

float ResourcePanel::content_height(ui::Canvas &canvas) {
    return card_height(device_content()) + cfg::resource_card_gap + card_height(memory_content(canvas, stats_)) + cfg::resource_card_gap + cfg::trailing_spacer;
}

void ResourcePanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &) {
    float y = view.y - scroll;
    float half = (view.w - cfg::resource_card_gap) / 2.0f;
    float devices = card_height(device_content());
    Device cpu{"CPU", stats_.cpu_usage, stats_.cpu_freq_ghz, cpu_temp_available(cpu_) ? cpu_.celsius : -1.0f, gauge_cpu};
    Device gpu{"GPU", gpu_.usage_percent < 0.0f ? -1.0f : gpu_.usage_percent / 100.0f, gpu_.clock_ghz, gpu_temp_available(gpu_) ? gpu_.celsius : -1.0f, gauge_gpu};
    device_card(canvas, {view.x, y, half, devices}, cpu);
    device_card(canvas, {view.x + half + cfg::resource_card_gap, y, half, devices}, gpu);
    y += devices + cfg::resource_card_gap;

    float memory = card_height(memory_content(canvas, stats_));
    ui::Box inner = card(canvas, {view.x, y, view.w, memory}, "Memory");
    struct Line {
        const char *label;
        std::string detail;
        float value;
        const Color *color;
    };
    std::vector<Line> lines;
    lines.push_back({"RAM", resource_format_used_cap(stats_.mem_used_gb, stats_.mem_total_gb), stats_.mem_usage, &usage_color(stats_.mem_usage, gauge_ram)});
    if (stats_.disk_pct >= 0.0f) {
        float disk = std::clamp(stats_.disk_pct / 100.0f, 0.0f, 1.0f);
        lines.push_back({"Disk", resource_format_used_cap(stats_.disk_used_gb, stats_.disk_total_gb), disk, &usage_color(disk, gauge_disk)});
    }
    float line_y = inner.y;
    for (const Line &line : lines) {
        ui::TextSize label = canvas.measure(line.label, text_style);
        ui::TextSize detail = canvas.measure(line.detail, text_style);
        canvas.text(line.label, text_style, inner.x, line_y, palette::text);
        canvas.text(line.detail, text_style, inner.x + inner.w - detail.w, line_y, palette::text_dim);
        float bar_y = line_y + std::max(label.h, detail.h) + cfg::memory_label_gap;
        panel_widgets::flat_bar(canvas, {inner.x, bar_y, inner.w, cfg::memory_bar}, std::max(0.0f, line.value), cfg::memory_min_fill, palette::text_alpha08, *line.color);
        line_y = bar_y + cfg::memory_bar + cfg::memory_line_gap;
    }
}

} // namespace astralia
