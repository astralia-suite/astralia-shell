#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "config/settings_config.h"

#include "render/field_view.h"
#include "render/icons.h"
#include "render/tokens.h"

#include "core/path_home.h"

#include "modules/settings/wallpaper_columns.h"

namespace astralia::settings {

namespace {

namespace cfg = settings_config;

using settings::DropEntry;
using settings::Pane;
using settings::small_style;
using settings::text_style;
using settings::transparent;

Field dir_field_for(WallpaperSlot slot) {
    switch (slot) {
    case WallpaperSlot::animated:
        return Field::wallpaper_animated_dir;
    case WallpaperSlot::lock:
        return Field::lock_wallpaper_dir;
    case WallpaperSlot::still:
        break;
    }
    return Field::wallpaper_dir;
}

} // namespace

std::vector<WallpaperChip> wallpaper_chips(const Model &model, WallpaperSlot slot) {
    std::vector<WallpaperChip> chips;
    const Caps &caps = model.caps();
    const Config &config = model.config();
    const std::vector<std::string> &monitors = model.monitors();
    for (size_t m = 0; m < monitors.size(); ++m) {
        int count = caps.wallpaper_columns ? wallpaper_column_count(config, monitors[m], slot) : 1;
        for (int col = 0; col < count; ++col) {
            chips.push_back(
                {static_cast<int>(m), col,
                 count > 1 ? monitors[m] + "-" + std::to_string(col + 1) : monitors[m]});
        }
    }
    return chips;
}

float wallpaper_columns_panel(Pane &pane, WallpaperSlot slot, float x, float y, float w,
                              float bottom) {
    Model &model = pane.model;
    const Caps &caps = model.caps();
    const Config &config = model.config();
    bool animated = slot == WallpaperSlot::animated;
    WallpaperPicker &picker = model.picker(slot);
    int slot_flag = static_cast<int>(slot);
    picker.grid_width = w;

    std::vector<WallpaperChip> chips = wallpaper_chips(model, slot);
    const std::vector<std::string> &monitors = model.monitors();
    {
        std::vector<DropEntry> region_entries;
        size_t active = std::string::npos;
        for (const WallpaperChip &chip : chips) {
            if (monitors[static_cast<size_t>(chip.monitor)] == picker.region &&
                chip.column == picker.column) {
                active = region_entries.size();
            }
            region_entries.push_back({chip.label, chip.monitor, chip.column | (slot_flag << 8)});
        }
        std::string current = active < region_entries.size() ? region_entries[active].label : "";
        pane.dropdown({x, y, w, cfg::tile_height}, 4, "Display", current, std::move(region_entries),
                      active, Act::wallpaper_region);
    }
    y += cfg::tile_height + cfg::row_gap;

    Field dir_field = dir_field_for(slot);
    bool focused = model.focused() == dir_field;
    ui::Box bar{x, y, w, cfg::dir_bar_height};
    pane.rounded(bar, metrics::radius_widget, focused ? palette::pop_b : palette::field_bg,
                 metrics::border_thin, palette::ink);
    float input_x = x + cfg::dir_bar_label_margin;
    input_x +=
        pane.text("Dir", text_style, input_x,
                  y + (cfg::dir_bar_height - pane.canvas.measure("Dir", text_style).h) / 2.0f,
                  palette::lavender)
            .w +
        cfg::dir_bar_field_margin;
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
        pane.caret = ui::draw_field_input(pane.canvas, model.field().text, model.field(),
                                          model.field_anim(), draw);
    } else {
        ui::TextStyle clipped = text_style;
        clipped.max_width = static_cast<int>(input_w);
        std::string shown = path_collapse_home(wallpaper_slot_dir(config, slot));
        ui::TextSize size = pane.canvas.measure(shown, clipped);
        pane.text(shown, clipped, input_x, y + (cfg::dir_bar_height - size.h) / 2.0f,
                  palette::text);
    }
    pane.region({input_x, y, input_w, cfg::dir_bar_height}, Act::field_focus,
                static_cast<int>(dir_field), 0);
    ui::Box button{button_x, y + (cfg::dir_bar_height - cfg::dir_bar_button_height) / 2.0f,
                   cfg::dir_bar_button_width, cfg::dir_bar_button_height};
    pane.selectable(button, false, palette::pop_b);
    pane.centered(picker.scanning ? "\xE2\x80\xA6" : "Rescan", text_style, button, palette::text);
    pane.region(button, Act::wallpaper_rescan, 0, slot_flag);
    y += cfg::dir_bar_height + cfg::row_gap;

    std::string own = wallpaper_column_override(config, picker.region, picker.column, slot);
    float control_y = y;
    if (caps.wallpaper_columns) {
        int count = picker.region.empty() ? 1 : wallpaper_column_count(config, picker.region, slot);
        ui::Box minus{x, control_y, cfg::stepper_button, cfg::field_height};
        pane.selectable(minus, false);
        pane.centered("-", text_style, minus, palette::text);
        pane.region(minus, Act::wallpaper_count, -1, slot_flag);
        std::string count_text = std::to_string(count);
        float count_w = pane.canvas.measure(count_text, text_style).w + 12.0f;
        pane.centered(count_text, text_style,
                      {x + cfg::stepper_button, control_y, count_w, cfg::field_height},
                      palette::text);
        ui::Box plus{x + cfg::stepper_button + count_w, control_y, cfg::stepper_button,
                     cfg::field_height};
        pane.selectable(plus, false);
        pane.centered("+", text_style, plus, palette::text);
        pane.region(plus, Act::wallpaper_count, 1, slot_flag);

        std::string mode = wallpaper_fill_mode(config, picker.region, picker.column, slot);
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
            pane.selectable(box, active);
            pane.centered(labels[i], text_style, box, palette::text);
            pane.region(box, Act::wallpaper_fill, i, slot_flag);
            fx += widths[i] + 6.0f;
        }
    }
    if (!own.empty()) {
        std::string label = caps.wallpaper_columns ? "Remove" : "Reset";
        float rw = pane.canvas.measure(label, text_style).w + 20.0f;
        float rx = caps.wallpaper_columns ? x + (w - rw) / 2.0f : x + w - rw;
        ui::Box box{rx, control_y, rw, cfg::field_height};
        pane.chip(box, palette::critical_container);
        pane.centered(label, text_style, box, palette::text);
        pane.region(box, Act::wallpaper_remove, 0, slot_flag);
    }
    y += cfg::row_height;

    float available = bottom - y;
    float inset_w = w - cfg::grid_inset * 2.0f;
    int columns = cfg::thumb_columns;
    float cell = cfg::thumb_size + cfg::thumb_gap;
    size_t rows =
        (picker.files.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
    float content_h = rows == 0 ? 0.0f : static_cast<float>(rows) * cell - cfg::thumb_gap;
    picker.grid_height = std::min(available, content_h + cfg::grid_inset * 2.0f);
    if (picker.files.empty()) {
        ui::TextSize size =
            pane.text(picker.scanning ? "Scanning\xE2\x80\xA6"
                                      : (animated ? "No videos found" : "No images found"),
                      text_style, x, y, palette::text_dim);
        return y + size.h;
    }
    pane.rounded({x, y, w, picker.grid_height}, metrics::radius_widget, transparent,
                 metrics::border_thin, palette::ink);
    float visible_h = std::min(picker.grid_height - cfg::grid_inset * 2.0f, content_h);
    float row_w = static_cast<float>(columns) * cell - cfg::thumb_gap;
    float grid_x = x + cfg::grid_inset + (inset_w - row_w) / 2.0f;
    float grid_y = y + cfg::grid_inset;
    picker.scroll = std::clamp(picker.scroll, 0.0f, std::max(0.0f, content_h - visible_h));
    std::string selected = wallpaper_column_path(config, picker.region, picker.column, slot);

    pane.canvas.begin_group({grid_x, grid_y, row_w, visible_h}, {1.0f, true});
    int total_rows = static_cast<int>(rows);
    int first_row =
        std::clamp(static_cast<int>(picker.scroll / cell), 0, std::max(0, total_rows - 1));
    int last_row = std::clamp(static_cast<int>((picker.scroll + visible_h) / cell), 0,
                              std::max(0, total_rows - 1));
    for (int row = first_row; row <= last_row; ++row) {
        for (int col = 0; col < columns; ++col) {
            size_t index =
                static_cast<size_t>(row) * static_cast<size_t>(columns) + static_cast<size_t>(col);
            if (index >= picker.files.size()) {
                break;
            }
            const std::string &path = picker.files[index];
            ui::Box tile{static_cast<float>(col) * cell,
                         static_cast<float>(row) * cell - picker.scroll, cfg::thumb_size,
                         cfg::thumb_size};
            bool active = path == selected;
            pane.rounded(tile, metrics::radius_widget, palette::field_bg);
            ui::ImageId image = pane.canvas.thumbnail(path, static_cast<int>(cfg::thumb_size));
            if (image != ui::no_image) {
                ui::TextSize size = pane.canvas.image_size(image);
                if (size.w > 0 && size.h > 0) {
                    float scale = std::max(cfg::thumb_size / size.w, cfg::thumb_size / size.h);
                    pane.canvas.begin_group(tile, {1.0f, true});
                    pane.canvas.draw_image(image,
                                           {(cfg::thumb_size - size.w * scale) / 2.0f,
                                            (cfg::thumb_size - size.h * scale) / 2.0f,
                                            size.w * scale, size.h * scale},
                                           pane.fade(palette::white));
                    pane.canvas.end_group();
                }
            }
            ui::TextStyle clipped = small_style;
            clipped.max_width = static_cast<int>(cfg::thumb_size - cfg::thumb_label_pad);
            std::string name = std::filesystem::path(path).filename().string();
            ui::TextSize name_size = pane.canvas.measure(name, clipped);
            float label_h = name_size.h + cfg::thumb_label_pad;
            pane.rect({tile.x, tile.y + cfg::thumb_size - label_h, cfg::thumb_size, label_h},
                      palette::overlay);
            pane.text(name, clipped, tile.x + cfg::thumb_label_pad / 2.0f,
                      tile.y + cfg::thumb_size - (label_h + name_size.h) / 2.0f, palette::text);
            pane.rounded(tile, metrics::radius_widget, transparent,
                         active ? metrics::border_thick : 0.0f,
                         active ? palette::ink : transparent);
            ui::Box hit{grid_x + tile.x, grid_y + tile.y, tile.w, tile.h};
            float top = std::max(hit.y, grid_y);
            float bottom_edge = std::min(hit.y + hit.h, grid_y + visible_h);
            if (bottom_edge > top) {
                pane.region({hit.x, top, hit.w, bottom_edge - top}, Act::wallpaper_pick,
                            static_cast<int>(index), slot_flag);
            }
        }
    }
    pane.canvas.end_group();
    return y + picker.grid_height;
}

} // namespace astralia::settings
