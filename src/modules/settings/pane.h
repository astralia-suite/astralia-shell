#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "config/settings_config.h"

#include "render/canvas.h"
#include "render/icons.h"
#include "render/sticker.h"
#include "render/tokens.h"

#include "modules/settings/model.h"

namespace astralia::settings_view {

namespace cfg = settings_config;

inline constexpr Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
inline constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
inline constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

struct DropEntry {
    std::string label;
    int a;
    int b;
};

struct Pane {
    ui::Canvas &canvas;
    SettingsModel &model;
    float alpha;
    ui::Box caret{};
    std::function<void()> overlay;

    Color fade(const Color &c) const { return with_alpha(c, c.a * alpha); }
    void rect(const ui::Box &box, const Color &c) { canvas.rect(box, fade(c)); }
    void rounded(const ui::Box &box, float radius, const Color &fill, float border_width = 0.0f,
                 const Color &border = transparent) {
        canvas.rounded(box, radius, fade(fill), border_width, fade(border));
    }
    void selectable(const ui::Box &box, bool selected, const Color &idle = palette::surface) {
        astralia::selectable(canvas, box, metrics::radius_widget, selected, false, idle, alpha);
    }
    void chip(const ui::Box &box, const Color &fill) {
        astralia::sticker(canvas, box, metrics::radius_widget, fade(fill), fade(palette::ink),
                          metrics::border_thin, metrics::shadow_widget);
    }
    ui::TextSize text(std::string_view value, const ui::TextStyle &style, float x, float y,
                      const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, x, y, fade(c));
        return size;
    }
    void centered(std::string_view value, const ui::TextStyle &style, const ui::Box &box,
                  const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, box.x + (box.w - size.w) / 2.0f, box.y + (box.h - size.h) / 2.0f,
                    fade(c));
    }
    void left(std::string_view value, const ui::TextStyle &style, float x, const ui::Box &box,
              const Color &c) {
        ui::TextSize size = canvas.measure(value, style);
        canvas.text(value, style, x, box.y + (box.h - size.h) / 2.0f, fade(c));
    }
    void region(const ui::Box &box, SettingsAct act, int a = 0, int b = 0) {
        model.add_region(box, act, a, b);
    }
    void dropdown(const ui::Box &row, int id, std::string_view label, const std::string &current,
                  std::vector<DropEntry> entries, size_t selected, SettingsAct pick) {
        chip(row, palette::surface);
        left(label, text_style, row.x + cfg::tile_margin, row,
             with_alpha(palette::text, cfg::label_opacity));
        float box_w = std::min(row.w * 0.5f, cfg::dropdown_max_width);
        ui::Box box{row.x + row.w - cfg::tile_margin - box_w,
                    row.y + (row.h - cfg::field_height) / 2.0f, box_w, cfg::field_height};
        bool open = model.dropdown_open() == id;
        selectable(box, open);
        left(current, text_style, box.x + cfg::dropdown_pad, box, palette::text);
        const char *glyph = open ? icon::chevron_up : icon::chevron_down;
        ui::TextSize arrow = canvas.measure(glyph, icon_style);
        canvas.text(glyph, icon_style, box.x + box.w - cfg::dropdown_pad - arrow.w,
                    box.y + (box.h - arrow.h) / 2.0f, fade(palette::text));
        region(box, SettingsAct::dropdown_toggle, id);
        if (!open) {
            return;
        }
        overlay = [this, box, entries = std::move(entries), selected, pick] {
            float row_h = cfg::field_height;
            ui::Box list{box.x, box.y + box.h + cfg::dropdown_gap, box.w,
                         row_h * static_cast<float>(entries.size())};
            chip(list, palette::paper);
            model.add_region_front({0.0f, 0.0f, 1.0e5f, 1.0e5f}, SettingsAct::dropdown_dismiss);
            for (size_t i = 0; i < entries.size(); ++i) {
                ui::Box item{list.x, list.y + row_h * static_cast<float>(i), list.w, row_h};
                if (i == selected) {
                    selectable(item, true);
                }
                left(entries[i].label, text_style, item.x + cfg::dropdown_pad, item, palette::text);
                model.add_region_front(item, pick, entries[i].a, entries[i].b);
            }
        };
    }
};

} // namespace astralia::settings_view
