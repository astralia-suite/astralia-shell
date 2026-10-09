#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "config/bar_style.h"
#include "config/icons.h"
#include "config/rain_config.h"
#include "config/settings_config.h"
#include "config/visualizer_config.h"

#include "core/path_home.h"

#include "modules/settings/view.h"

#include "service/wallpaper_service.h"

#include "ui/field_view.h"
#include "ui/tokens.h"

namespace astralia {

namespace {

namespace cfg = settings_config;

constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::title_px};
constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

struct Pane {
    ui::Canvas &canvas;
    SettingsModel &model;
    float alpha;
    ui::Box caret{};

    Color fade(const Color &c) const { return with_alpha(c, c.a * alpha); }
    void rect(const ui::Box &box, const Color &c) { canvas.rect(box, fade(c)); }
    void rounded(const ui::Box &box, float radius, const Color &fill, float border_width = 0.0f, const Color &border = transparent) { canvas.rounded(box, radius, fade(fill), border_width, fade(border)); }
    ui::TextSize text(std::string_view value, const ui::TextStyle &style, float x, float y, const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, x, y, fade(c));
        return size;
    }
    void centered(std::string_view value, const ui::TextStyle &style, const ui::Box &box, const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, box.x + (box.w - size.w) / 2.0f, box.y + (box.h - size.h) / 2.0f, fade(c));
    }
    void left(std::string_view value, const ui::TextStyle &style, float x, const ui::Box &box, const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, x, box.y + (box.h - size.h) / 2.0f, fade(c));
    }
    void region(const ui::Box &box, SettingsAct act, int a = 0, int b = 0) { model.add_region(box, act, a, b); }
};

void toggle_switch(Pane &pane, float x, float y, bool on, SettingsAct act, int a, int b) {
    ui::Box track{x, y, cfg::toggle_width, cfg::toggle_height};
    pane.rounded(track, cfg::toggle_height / 2.0f, on ? palette::accent : palette::text_alpha11);
    float knob_x = on ? x + cfg::toggle_width - cfg::toggle_knob - cfg::toggle_knob_inset : x + cfg::toggle_knob_inset;
    pane.rounded({knob_x, y + (cfg::toggle_height - cfg::toggle_knob) / 2.0f, cfg::toggle_knob, cfg::toggle_knob}, cfg::toggle_knob / 2.0f, palette::text);
    pane.region(track, act, a, b);
}

float toggle_row(Pane &pane, float x, float y, float w, std::string_view label, bool value, SettingsAct act, int a, int b, bool tiled) {
    float h = tiled ? cfg::tile_height : cfg::toggle_height;
    float inset = tiled ? cfg::tile_margin : 0.0f;
    if (tiled) {
        pane.rounded({x, y, w, h}, cfg::tile_radius, palette::text_alpha04, cfg::tile_border, palette::text_alpha07);
    }
    pane.left(label, text_style, x + inset, {x, y, w, h}, tiled ? with_alpha(palette::text, cfg::label_opacity) : palette::text);
    toggle_switch(pane, x + w - inset - cfg::toggle_width, y + (h - cfg::toggle_height) / 2.0f, value, act, a, b);
    return h;
}

void tiles(Pane &pane, float x, float y, float w, const std::vector<std::string> &labels, int active, SettingsAct act, int b) {
    float count = static_cast<float>(labels.size());
    float tile_w = (w - cfg::selector_spacing * (count - 1.0f)) / count;
    float cx = x;
    for (size_t i = 0; i < labels.size(); ++i) {
        ui::Box box{cx, y, tile_w, cfg::selector_height};
        bool is_active = static_cast<int>(i) == active;
        pane.rounded(box, cfg::tile_radius, palette::lavender_alpha20, cfg::selector_border, is_active ? palette::accent_alt : transparent);
        pane.centered(labels[i], text_style, box, palette::text);
        pane.region(box, act, static_cast<int>(i), b);
        cx += tile_w + cfg::selector_spacing;
    }
}

void monitor_row(Pane &pane, float x, float y, float w, const std::string &selected, SettingsAct act) {
    const std::vector<std::string> &monitors = pane.model.monitors();
    float count = static_cast<float>(monitors.size()) + 1.0f;
    float tile_w = (w - cfg::selector_spacing * (count - 1.0f)) / count;
    float cx = x;
    auto draw = [&](const std::string &label, int index, bool active) {
        ui::Box box{cx, y, tile_w, cfg::selector_height};
        pane.rounded(box, cfg::tile_radius, palette::lavender_alpha20, cfg::selector_border, active ? palette::accent_alt : transparent);
        pane.centered(label, text_style, box, palette::text);
        pane.region(box, act, index, 0);
        cx += tile_w + cfg::selector_spacing;
    };
    draw("Default", -1, selected.empty());
    for (size_t i = 0; i < monitors.size(); ++i) {
        draw(monitors[i], static_cast<int>(i), monitors[i] == selected);
    }
}

void number_field(Pane &pane, const ui::Box &box, SettingsField field, const std::string &value) {
    bool focused = pane.model.focused() == field;
    pane.rounded(box, metrics::radius_sm, palette::field_bg, metrics::border_thin, focused ? palette::accent : transparent);
    float cy = box.y + box.h / 2.0f;
    if (focused) {
        ui::FieldDraw draw;
        draw.style = text_style;
        draw.x = box.x + cfg::field_inset;
        draw.center_y = cy;
        draw.caret_height = box.h - 10.0f;
        draw.caret_width = 1.5f;
        draw.caret_gap = 2.0f;
        draw.color = pane.fade(palette::text);
        pane.caret = ui::draw_field_input(pane.canvas, pane.model.field().text, pane.model.field(), pane.model.field_anim(), draw);
    } else {
        pane.left(value, text_style, box.x + cfg::field_inset, box, palette::text);
    }
    pane.region(box, SettingsAct::field_focus, static_cast<int>(field), 0);
}

void reset_icon(Pane &pane, float x, float y_center, SettingsAct act, int a, int b) {
    ui::Box box{x, y_center - cfg::reset_icon_size / 2.0f, cfg::reset_icon_size, cfg::reset_icon_size};
    ui::TextSize size = pane.canvas.measure(icon::refresh, icon_style);
    pane.canvas.text(icon::refresh, icon_style, box.x + (box.w - size.w) / 2.0f, box.y + (box.h - size.h) / 2.0f, pane.fade(palette::text_dim));
    pane.region(box, act, a, b);
}

void bar_tab(Pane &pane, float x, float y, float w) {
    std::vector<std::string> labels;
    int active = 0;
    const SettingsCaps &caps = pane.model.caps();
    for (size_t i = 0; i < caps.bar_styles.size(); ++i) {
        labels.emplace_back(bar_style::labels[static_cast<size_t>(caps.bar_styles[i])]);
        if (caps.bar_styles[i] == pane.model.effective_bar_style()) {
            active = static_cast<int>(i);
        }
    }
    tiles(pane, x, y, w, labels, active, SettingsAct::bar_style, 0);
}

void displays_tab(Pane &pane, float x, float y, float w) {
    const Config &config = pane.model.config();
    const std::string &selected = pane.model.displays_monitor();
    monitor_row(pane, x, y, w, selected, SettingsAct::displays_monitor);
    y += cfg::selector_height + cfg::row_gap;
    bool is_default = selected.empty();
    const MonitorOverride *override = nullptr;
    if (!is_default) {
        auto it = config.monitor_overrides.find(selected);
        override = it != config.monitor_overrides.end() ? &it->second : nullptr;
    }
    bool overridden = override != nullptr && override->enabled;
    if (!is_default) {
        y += toggle_row(pane, x, y, w, "Override default settings", overridden, SettingsAct::toggle_override, 0, 0, false) + cfg::row_gap;
    }
    if (!is_default && !overridden) {
        return;
    }
    auto value = [&](bool defaults, bool MonitorOverride::*field) { return is_default ? defaults : override->*field; };
    bool first = true;
    auto row = [&](std::string_view label, bool on, SettingsAct act) {
        if (!first) {
            y += cfg::group_spacing;
        }
        first = false;
        y += toggle_row(pane, x, y, w, label, on, act, 0, 0, true);
    };
    if (pane.model.caps().bar_toggle) {
        row("Bar", value(config.default_bar_enabled, &MonitorOverride::bar), SettingsAct::toggle_bar);
    }
    row("OSD", value(config.default_osd_enabled, &MonitorOverride::osd), SettingsAct::toggle_osd);
    row("Notifications", value(config.default_notifications_enabled, &MonitorOverride::notifications), SettingsAct::toggle_notifications);
    if (pane.model.caps().autohide) {
        row("Bar Autohide", value(config.autohide, &MonitorOverride::autohide), SettingsAct::toggle_autohide);
    }
}

void idle_tier(Pane &pane, float x, float y, float w, std::string_view label, SettingsField field, uint32_t value, uint32_t default_value, bool enabled, bool show_reset, SettingsAct reset_act, SettingsAct toggle_act) {
    float h = cfg::tile_height;
    pane.rounded({x, y, w, h}, cfg::tile_radius, palette::text_alpha04, cfg::tile_border, palette::text_alpha07);
    float inset = cfg::tile_margin;
    pane.left(label, text_style, x + inset, {x, y, w, h}, with_alpha(palette::text, cfg::label_opacity));
    float switch_x = x + w - inset - cfg::toggle_width;
    float divider_x = switch_x - cfg::tile_spacing;
    float reset_x = divider_x - cfg::tile_spacing - cfg::reset_icon_size;
    float field_x = reset_x - cfg::tile_spacing - cfg::number_field_width;
    number_field(pane, {field_x, y + (h - cfg::field_height) / 2.0f, cfg::number_field_width, cfg::field_height}, field, std::to_string(value));
    if (show_reset && value != default_value) {
        reset_icon(pane, reset_x, y + h / 2.0f, reset_act, 0, 1);
    }
    pane.rect({divider_x, y + 10.0f, 1.0f, h - 20.0f}, palette::text_alpha11);
    toggle_switch(pane, switch_x, y + (h - cfg::toggle_height) / 2.0f, enabled, toggle_act, 0, 1);
}

void idle_tab(Pane &pane, float x, float y, float w) {
    const Config &config = pane.model.config();
    y += toggle_row(pane, x, y, w, "Enable Idle Management", config.idle_management_enabled, SettingsAct::idle_enable, 0, 1, true) + cfg::row_gap;
    if (!config.idle_management_enabled) {
        return;
    }
    const std::string &selected = pane.model.idle_monitor();
    monitor_row(pane, x, y, w, selected, SettingsAct::idle_monitor);
    y += cfg::selector_height + cfg::row_gap;
    bool is_default = selected.empty();
    const MonitorOverride *override = nullptr;
    if (!is_default) {
        auto it = config.monitor_overrides.find(selected);
        override = it != config.monitor_overrides.end() ? &it->second : nullptr;
    }
    bool overridden = override != nullptr && override->enabled;
    if (!is_default) {
        y += toggle_row(pane, x, y, w, "Override default settings", overridden, SettingsAct::toggle_override, 0, 1, false) + cfg::row_gap;
    }
    if (!is_default && !overridden) {
        return;
    }
    bool ambient = is_default ? config.ambient_enabled : override->ambient_enabled;
    uint32_t ambient_timeout = is_default ? config.ambient_timeout_seconds : override->ambient_timeout_seconds;
    bool screensaver = is_default ? config.screensaver_enabled : override->screensaver_enabled;
    uint32_t screensaver_timeout = is_default ? config.screensaver_timeout_seconds : override->screensaver_timeout_seconds;
    idle_tier(pane, x, y, w, "Ambient Mode", SettingsField::ambient_timeout, ambient_timeout, config.ambient_timeout_seconds, ambient, !is_default, SettingsAct::idle_ambient_reset, SettingsAct::idle_ambient);
    y += cfg::tile_height + cfg::group_spacing;
    idle_tier(pane, x, y, w, "Screensaver", SettingsField::screensaver_timeout, screensaver_timeout, config.screensaver_timeout_seconds, screensaver, !is_default, SettingsAct::idle_screensaver_reset, SettingsAct::idle_screensaver);
}

void rain_tab(Pane &pane, float x, float y, float w) {
    const RainParams &rain = pane.model.config().rain;
    tiles(pane, x, y, w, {"Matrix", "Stiletto"}, rain.mode == RainMode::Matrix ? 0 : 1, SettingsAct::rain_mode, 0);
    y += cfg::selector_height + cfg::row_gap;
    toggle_row(pane, x, y, w, "Asynchronous fall speed", rain.async_speed, SettingsAct::rain_async, 0, 0, true);
}

struct Knob {
    SettingsField field;
    const char *label;
};

constexpr std::array<Knob, 6> knobs{{
    {SettingsField::visualizer_fps, "Target framerate"},
    {SettingsField::visualizer_thin, "Particle grid density"},
    {SettingsField::visualizer_size, "Particle size"},
    {SettingsField::visualizer_complexity, "Fractal complexity"},
    {SettingsField::visualizer_glow_directions, "Glow directions"},
    {SettingsField::visualizer_glow_quality, "Glow quality"},
}};

void visualizer_tab(Pane &pane, float x, float y, float w) {
    const Config &config = pane.model.config();
    tiles(pane, x, y, w, {"Bar", "Sphere"}, config.visualizer.visualizer_shape == VisualizerShape::Bar ? 0 : 1, SettingsAct::visualizer_shape, 0);
    y += cfg::selector_height + cfg::row_gap;
    if (config.visualizer.visualizer_shape != VisualizerShape::Sphere) {
        return;
    }
    Config defaults;
    for (const Knob &knob : knobs) {
        float h = cfg::tile_height;
        pane.rounded({x, y, w, h}, cfg::tile_radius, palette::text_alpha04, cfg::tile_border, palette::text_alpha07);
        pane.left(knob.label, text_style, x + cfg::tile_margin, {x, y, w, h}, with_alpha(palette::text, cfg::label_opacity));
        float field_x = x + w - cfg::tile_margin - cfg::number_field_width;
        float reset_x = field_x - cfg::tile_spacing - cfg::reset_icon_size;
        std::string value = settings_field_text(config, knob.field, "");
        number_field(pane, {field_x, y + (h - cfg::field_height) / 2.0f, cfg::number_field_width, cfg::field_height}, knob.field, value);
        if (value != settings_field_text(defaults, knob.field, "")) {
            reset_icon(pane, reset_x, y + h / 2.0f, SettingsAct::visualizer_reset, static_cast<int>(knob.field), 0);
        }
        y += h + cfg::row_gap;
    }
}

struct Chip {
    int monitor;
    int column;
    std::string label;
};

void wallpaper_tab(Pane &pane, float x, float y, float w, float bottom) {
    SettingsModel &model = pane.model;
    const SettingsCaps &caps = model.caps();
    const Config &config = model.config();
    bool animated = caps.animated_wallpaper && config.wallpaper_animated_enabled;
    WallpaperPicker &picker = model.picker(animated);
    int animated_flag = animated ? 1 : 0;
    picker.grid_width = w;

    if (caps.default_wallpaper) {
        y += toggle_row(pane, x, y, w, "Use Default Wallpaper", config.default_wallpaper_enabled, SettingsAct::wallpaper_default, 0, 0, true) + cfg::row_gap;
    }
    if (caps.animated_wallpaper) {
        y += toggle_row(pane, x, y, w, "Enable animated wallpaper", config.wallpaper_animated_enabled, SettingsAct::wallpaper_animated, 0, 0, true) + cfg::row_gap;
    }

    std::vector<Chip> chips;
    const std::vector<std::string> &monitors = model.monitors();
    for (size_t m = 0; m < monitors.size(); ++m) {
        int count = caps.wallpaper_columns ? wallpaper_column_count(config, monitors[m], animated) : 1;
        for (int col = 0; col < count; ++col) {
            chips.push_back({static_cast<int>(m), col, count > 1 ? monitors[m] + "-" + std::to_string(col + 1) : monitors[m]});
        }
    }
    if (!chips.empty()) {
        float chip_w = (w - cfg::region_chip_gap * static_cast<float>(chips.size() - 1)) / static_cast<float>(chips.size());
        float cx = x;
        for (const Chip &chip : chips) {
            ui::Box box{cx, y, chip_w, cfg::region_chip_height};
            bool active = monitors[static_cast<size_t>(chip.monitor)] == picker.region && chip.column == picker.column;
            pane.rounded(box, metrics::radius_sm, active ? palette::accent_alpha19 : palette::field_bg, metrics::border_thin, active ? palette::accent_alt : transparent);
            pane.centered(chip.label, text_style, box, palette::text);
            pane.region(box, SettingsAct::wallpaper_region, chip.monitor, chip.column | (animated ? 0x100 : 0));
            cx += chip_w + cfg::region_chip_gap;
        }
    }
    y += cfg::region_chip_height + cfg::row_gap;

    SettingsField dir_field = animated ? SettingsField::wallpaper_animated_dir : SettingsField::wallpaper_dir;
    bool focused = model.focused() == dir_field;
    ui::Box bar{x, y, w, cfg::dir_bar_height};
    pane.rounded(bar, metrics::radius_sm, palette::field_bg, metrics::border_thin, focused ? palette::accent : transparent);
    float input_x = x + cfg::dir_bar_label_margin;
    input_x += pane.text("Dir", text_style, input_x, y + (cfg::dir_bar_height - pane.canvas.measure("Dir", text_style).h) / 2.0f, palette::accent).w + cfg::dir_bar_field_margin;
    float button_x = x + w - cfg::dir_bar_edge_margin - cfg::dir_bar_button_width;
    float input_w = button_x - cfg::dir_bar_edge_margin - input_x;
    float center_y = y + cfg::dir_bar_height / 2.0f;
    if (focused) {
        ui::FieldDraw draw;
        draw.style = text_style;
        draw.x = input_x;
        draw.center_y = center_y;
        draw.caret_height = cfg::dir_bar_height - 16.0f;
        draw.caret_width = 1.5f;
        draw.caret_gap = 2.0f;
        draw.color = pane.fade(palette::text);
        pane.caret = ui::draw_field_input(pane.canvas, model.field().text, model.field(), model.field_anim(), draw);
    } else {
        ui::TextStyle clipped = text_style;
        clipped.max_width = static_cast<int>(input_w);
        std::string shown = path_collapse_home(animated ? config.wallpaper_animated_dir : config.wallpaper_dir);
        ui::TextSize size = pane.canvas.measure(shown, clipped);
        pane.text(shown, clipped, input_x, y + (cfg::dir_bar_height - size.h) / 2.0f, palette::text);
    }
    pane.region({input_x, y, input_w, cfg::dir_bar_height}, SettingsAct::field_focus, static_cast<int>(dir_field), 0);
    ui::Box button{button_x, y + (cfg::dir_bar_height - cfg::dir_bar_button_height) / 2.0f, cfg::dir_bar_button_width, cfg::dir_bar_button_height};
    pane.rounded(button, metrics::radius_sm, palette::text_alpha11);
    pane.centered(picker.scanning ? "\xE2\x80\xA6" : "Rescan", text_style, button, palette::text);
    pane.region(button, SettingsAct::wallpaper_rescan, 0, animated_flag);
    y += cfg::dir_bar_height + cfg::row_gap;

    std::string own = wallpaper_column_override(config, picker.region, picker.column, animated);
    float control_y = y;
    if (caps.wallpaper_columns) {
        int count = picker.region.empty() ? 1 : wallpaper_column_count(config, picker.region, animated);
        ui::Box minus{x, control_y, cfg::stepper_button, cfg::field_height};
        pane.rounded(minus, metrics::radius_sm, palette::field_bg, metrics::border_thin);
        pane.centered("-", text_style, minus, palette::text);
        pane.region(minus, SettingsAct::wallpaper_count, -1, animated_flag);
        std::string count_text = std::to_string(count);
        float count_w = pane.canvas.measure(count_text, text_style).w + 12.0f;
        pane.centered(count_text, text_style, {x + cfg::stepper_button, control_y, count_w, cfg::field_height}, palette::text);
        ui::Box plus{x + cfg::stepper_button + count_w, control_y, cfg::stepper_button, cfg::field_height};
        pane.rounded(plus, metrics::radius_sm, palette::field_bg, metrics::border_thin);
        pane.centered("+", text_style, plus, palette::text);
        pane.region(plus, SettingsAct::wallpaper_count, 1, animated_flag);

        std::string mode = wallpaper_fill_mode(config, picker.region, picker.column, animated);
        const char *labels[2] = {"Crop", "Fit"};
        float widths[2];
        float pair = 0.0f;
        for (int i = 0; i < 2; ++i) {
            widths[i] = pane.canvas.measure(labels[i], text_style).w + 20.0f;
            pair += widths[i] + (i == 0 ? 0.0f : 6.0f);
        }
        float fx = x + w - pair;
        for (int i = 0; i < 2; ++i) {
            bool active = mode == (i == 0 ? "crop" : "fit");
            ui::Box box{fx, control_y, widths[i], cfg::field_height};
            pane.rounded(box, metrics::radius_sm, active ? palette::accent_alpha19 : palette::field_bg, metrics::border_thin, active ? palette::accent : transparent);
            pane.centered(labels[i], text_style, box, active ? palette::accent : palette::text);
            pane.region(box, SettingsAct::wallpaper_fill, i, animated_flag);
            fx += widths[i] + 6.0f;
        }
    }
    if (!own.empty()) {
        std::string label = caps.wallpaper_columns ? "Remove" : "Reset";
        float rw = pane.canvas.measure(label, text_style).w + 20.0f;
        float rx = caps.wallpaper_columns ? x + (w - rw) / 2.0f : x + w - rw;
        ui::Box box{rx, control_y, rw, cfg::field_height};
        pane.rounded(box, metrics::radius_sm, palette::field_bg, metrics::border_thin, palette::critical);
        pane.centered(label, text_style, box, palette::critical);
        pane.region(box, SettingsAct::wallpaper_remove, 0, animated_flag);
    }
    y += cfg::row_height;

    float available = bottom - y;
    float inset_w = w - cfg::grid_inset * 2.0f;
    int columns = cfg::thumb_columns;
    float cell = cfg::thumb_size + cfg::thumb_gap;
    size_t rows = (picker.files.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
    float content_h = rows == 0 ? 0.0f : static_cast<float>(rows) * cell - cfg::thumb_gap;
    picker.grid_height = std::min(available, content_h + cfg::grid_inset * 2.0f);
    if (picker.files.empty()) {
        pane.text(picker.scanning ? "Scanning\xE2\x80\xA6" : (animated ? "No videos found" : "No images found"), text_style, x, y, palette::text_dim);
        return;
    }
    pane.rounded({x, y, w, picker.grid_height}, metrics::radius_sm, transparent, metrics::border_thin, palette::accent);
    float visible_h = std::min(picker.grid_height - cfg::grid_inset * 2.0f, content_h);
    float row_w = static_cast<float>(columns) * cell - cfg::thumb_gap;
    float grid_x = x + cfg::grid_inset + (inset_w - row_w) / 2.0f;
    float grid_y = y + cfg::grid_inset;
    picker.scroll = std::clamp(picker.scroll, 0.0f, std::max(0.0f, content_h - visible_h));
    std::string selected = wallpaper_column_path(config, picker.region, picker.column, animated);

    pane.canvas.begin_group({grid_x, grid_y, row_w, visible_h}, {1.0f, true});
    int total_rows = static_cast<int>(rows);
    int first_row = std::clamp(static_cast<int>(picker.scroll / cell), 0, std::max(0, total_rows - 1));
    int last_row = std::clamp(static_cast<int>((picker.scroll + visible_h) / cell), 0, std::max(0, total_rows - 1));
    for (int row = first_row; row <= last_row; ++row) {
        for (int col = 0; col < columns; ++col) {
            size_t index = static_cast<size_t>(row) * static_cast<size_t>(columns) + static_cast<size_t>(col);
            if (index >= picker.files.size()) {
                break;
            }
            const std::string &path = picker.files[index];
            ui::Box tile{static_cast<float>(col) * cell, static_cast<float>(row) * cell - picker.scroll, cfg::thumb_size, cfg::thumb_size};
            bool active = path == selected;
            pane.rounded(tile, cfg::thumb_radius, palette::field_bg);
            ui::ImageId image = pane.canvas.thumbnail(path, static_cast<int>(cfg::thumb_size));
            if (image != ui::no_image) {
                ui::TextSize size = pane.canvas.image_size(image);
                if (size.w > 0 && size.h > 0) {
                    float scale = std::max(cfg::thumb_size / size.w, cfg::thumb_size / size.h);
                    pane.canvas.begin_group(tile, {1.0f, true});
                    pane.canvas.draw_image(image, {(cfg::thumb_size - size.w * scale) / 2.0f, (cfg::thumb_size - size.h * scale) / 2.0f, size.w * scale, size.h * scale}, pane.fade(palette::text));
                    pane.canvas.end_group();
                }
            }
            ui::TextStyle clipped = small_style;
            clipped.max_width = static_cast<int>(cfg::thumb_size - cfg::thumb_label_pad);
            std::string name = std::filesystem::path(path).filename().string();
            ui::TextSize name_size = pane.canvas.measure(name, clipped);
            float label_h = name_size.h + cfg::thumb_label_pad;
            pane.rect({tile.x, tile.y + cfg::thumb_size - label_h, cfg::thumb_size, label_h}, palette::overlay);
            pane.text(name, clipped, tile.x + cfg::thumb_label_pad / 2.0f, tile.y + cfg::thumb_size - (label_h + name_size.h) / 2.0f, palette::text);
            pane.rounded(tile, cfg::thumb_radius, transparent, active ? metrics::border_thick : 0.0f, active ? palette::accent_alt : transparent);
            ui::Box hit{grid_x + tile.x, grid_y + tile.y, tile.w, tile.h};
            float top = std::max(hit.y, grid_y);
            float bottom_edge = std::min(hit.y + hit.h, grid_y + visible_h);
            if (bottom_edge > top) {
                pane.region({hit.x, top, hit.w, bottom_edge - top}, SettingsAct::wallpaper_pick, static_cast<int>(index), animated_flag);
            }
        }
    }
    pane.canvas.end_group();
    y += picker.grid_height;
    if (animated && model.cpu_fallback(picker.region, picker.column)) {
        y += cfg::row_gap;
        std::string warning = "Warning: Animated wallpaper is running on CPU.";
        ui::TextSize size = pane.canvas.measure(warning, text_style);
        float h = size.h + cfg::warning_pad * 2.0f;
        pane.rounded({x, y, w, h}, metrics::radius_sm, palette::critical_alpha15, metrics::border_thin, palette::critical);
        pane.text(warning, text_style, x + cfg::warning_pad, y + (h - size.h) / 2.0f, palette::critical);
    }
}

void tab_content(Pane &pane, SettingsTab tab, float x, float y, float w, float bottom) {
    switch (tab) {
    case SettingsTab::animation:
        toggle_row(pane, x, y, w, "Disable Animations", pane.model.config().animations_disabled, SettingsAct::animation_disable, 0, 0, true);
        break;
    case SettingsTab::bar:
        bar_tab(pane, x, y, w);
        break;
    case SettingsTab::displays:
        displays_tab(pane, x, y, w);
        break;
    case SettingsTab::idle:
        idle_tab(pane, x, y, w);
        break;
    case SettingsTab::logout:
        toggle_row(pane, x, y, w, "Animated central logo", pane.model.config().logout_animated_logo, SettingsAct::logout_logo, 0, 0, true);
        break;
    case SettingsTab::rain:
        rain_tab(pane, x, y, w);
        break;
    case SettingsTab::visualizer:
        visualizer_tab(pane, x, y, w);
        break;
    case SettingsTab::wallpaper:
        wallpaper_tab(pane, x, y, w, bottom);
        break;
    }
}

} // namespace

SettingsFrame paint_settings(ui::Canvas &canvas, SettingsModel &model, const SettingsArt &art, float width, float height) {
    model.begin_frame();
    SettingsFrame frame;
    float pw = std::min(width - 2.0f * cfg::card_margin, cfg::card_max_width);
    float ph = std::min(height - 2.0f * cfg::card_margin, cfg::card_max_height);
    float px = (width - pw) / 2.0f;
    float py = (height - ph) / 2.0f;
    frame.panel = {px, py, pw, ph};
    model.set_panel(frame.panel);
    canvas.rounded(frame.panel, metrics::radius_md, palette::overlay, metrics::border_thick, palette::accent);

    float pad = cfg::card_padding;
    float header_y = py + pad;
    ui::TextSize title = canvas.measure("Settings", title_style);
    canvas.text("Settings", title_style, px + pad, header_y + (cfg::header_height - title.h) / 2.0f, palette::text);
    ui::Box close{px + pw - pad - cfg::action_button_size, header_y + (cfg::header_height - cfg::action_button_size) / 2.0f, cfg::action_button_size, cfg::action_button_size};
    canvas.rounded(close, cfg::action_button_size / 2.0f, palette::overlay, 0.0f, palette::overlay);
    ui::TextSize close_size = canvas.measure(icon::close, icon_style);
    canvas.text(icon::close, icon_style, close.x + (close.w - close_size.w) / 2.0f, close.y + (close.h - close_size.h) / 2.0f, palette::text);
    model.add_region(close, SettingsAct::close);

    float content_y = header_y + cfg::header_height + cfg::header_divider_gap;
    canvas.rect({px + pad, content_y, pw - 2.0f * pad, 1.0f}, palette::text_alpha11);
    content_y += 1.0f + cfg::content_gap;

    bool expanded = pw > cfg::rail_collapse_breakpoint;
    float rail_w = expanded ? cfg::rail_expanded_width : cfg::rail_collapsed_width;
    float rail_x = px + pad;

    ui::Box avatar{rail_x + (rail_w - cfg::avatar_size) / 2.0f, content_y + cfg::profile_top_padding, cfg::avatar_size, cfg::avatar_size};
    bool drawn = art.avatar && art.avatar(canvas, avatar);
    if (!drawn) {
        canvas.rounded(avatar, cfg::avatar_size / 2.0f, palette::text_alpha04, cfg::avatar_border, palette::accent);
        ui::TextSize glyph = canvas.measure(icon::user, icon_style);
        canvas.text(icon::user, icon_style, avatar.x + (avatar.w - glyph.w) / 2.0f, avatar.y + (avatar.h - glyph.h) / 2.0f, palette::text);
    }
    float block_h = cfg::profile_top_padding + cfg::avatar_size;
    if (expanded) {
        float info_y = avatar.y + cfg::avatar_size + cfg::profile_label_gap;
        ui::TextSize name = canvas.measure(art.user_name, text_style);
        canvas.text(art.user_name, text_style, rail_x + (rail_w - name.w) / 2.0f, info_y, palette::text);
        info_y += name.h + cfg::profile_line_gap;
        ui::TextSize uptime = canvas.measure(art.uptime, small_style);
        canvas.text(art.uptime, small_style, rail_x + (rail_w - uptime.w) / 2.0f, info_y, palette::text_dim);
        info_y += uptime.h;
        block_h = info_y - content_y;
    }
    block_h += cfg::profile_bottom_padding;
    canvas.rect({rail_x, content_y + block_h, rail_w, 1.0f}, palette::text_alpha11);
    float rail_y = content_y + block_h + cfg::profile_divider_gap;
    float rail_h = py + ph - pad - rail_y;

    canvas.rounded({rail_x, rail_y, rail_w, rail_h}, metrics::radius_md, palette::text_alpha04);
    float row_y = rail_y + cfg::rail_padding;
    for (const SettingsTabInfo &info : model.tabs()) {
        bool active = info.tab == model.active_tab();
        const Color &color = active ? palette::accent : palette::text_dim;
        ui::Box row{rail_x, row_y, rail_w, cfg::rail_item_height};
        if (active) {
            canvas.rounded(row, metrics::radius_sm, palette::accent_alpha19);
        }
        float icon_x = rail_x + cfg::rail_padding;
        ui::TextSize glyph = canvas.measure(info.icon, icon_style);
        canvas.text(info.icon, icon_style, icon_x, row_y + (cfg::rail_item_height - glyph.h) / 2.0f, color);
        if (expanded) {
            ui::TextSize label = canvas.measure(info.label, text_style);
            canvas.text(info.label, text_style, icon_x + glyph.w + cfg::rail_icon_label_gap, row_y + (cfg::rail_item_height - label.h) / 2.0f, color);
        }
        model.add_region(row, SettingsAct::tab, static_cast<int>(info.tab));
        row_y += cfg::rail_item_height + cfg::rail_item_gap;
    }

    float divider_x = rail_x + rail_w + cfg::rail_divider_gap;
    canvas.rect({divider_x, content_y, 1.0f, rail_h + block_h + cfg::profile_divider_gap}, palette::text_alpha11);

    float content_x = divider_x + cfg::rail_divider_gap;
    float content_w = px + pw - pad - content_x;
    Pane pane{canvas, model, model.tab_alpha()};
    tab_content(pane, model.active_tab(), content_x, content_y, content_w, py + ph - pad);
    frame.caret = pane.caret;
    return frame;
}

} // namespace astralia
