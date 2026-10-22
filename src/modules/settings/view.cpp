#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "config/bar_layout.h"
#include "config/rain_config.h"
#include "config/settings_config.h"
#include "config/visualizer_config.h"

#include "render/field_view.h"
#include "render/icons.h"
#include "render/scroll.h"
#include "render/sticker.h"
#include "render/tokens.h"

#include "modules/settings/pane.h"
#include "modules/settings/view.h"
#include "modules/settings/wallpaper_columns.h"

#include "service/wallpaper_service.h"

namespace astralia::settings {

namespace {

namespace cfg = settings_config;

using settings::DropEntry;
using settings::icon_style;
using settings::Pane;
using settings::small_style;
using settings::text_style;
using settings::transparent;

constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::title_px, true};
constexpr ui::TextStyle big_icon_style{ui::FontFamily::icon, cfg::menu_icon_px};

void toggle_switch(Pane &pane, float x, float y, bool on, Act act, int a, int b) {
    ui::Box track{x, y, cfg::toggle_width, cfg::toggle_height};
    pane.selectable(track, on, palette::surface_alt);
    float knob_x = on ? x + cfg::toggle_width - cfg::toggle_knob - cfg::toggle_knob_inset
                      : x + cfg::toggle_knob_inset;
    pane.rounded({knob_x, y + (cfg::toggle_height - cfg::toggle_knob) / 2.0f, cfg::toggle_knob,
                  cfg::toggle_knob},
                 std::min(metrics::radius_widget, cfg::toggle_knob / 2.0f), palette::text);
    pane.region(track, act, a, b);
}

float toggle_row(Pane &pane, float x, float y, float w, std::string_view label, bool value, Act act,
                 int a, int b, bool tiled) {
    float h = tiled ? cfg::tile_height : cfg::toggle_height;
    float inset = tiled ? cfg::tile_margin : 0.0f;
    if (tiled) {
        pane.chip({x, y, w, h}, palette::surface);
    }
    pane.left(label, text_style, x + inset, {x, y, w, h},
              tiled ? with_alpha(palette::text, cfg::label_opacity) : palette::text);
    toggle_switch(pane, x + w - inset - cfg::toggle_width, y + (h - cfg::toggle_height) / 2.0f,
                  value, act, a, b);
    return h;
}

void tiles(Pane &pane, float x, float y, float w, const std::vector<std::string> &labels,
           int active, Act act, int b) {
    float count = static_cast<float>(labels.size());
    float tile_w = (w - cfg::selector_spacing * (count - 1.0f)) / count;
    float cx = x;
    for (size_t i = 0; i < labels.size(); ++i) {
        ui::Box box{cx, y, tile_w, cfg::selector_height};
        bool is_active = static_cast<int>(i) == active;
        pane.selectable(box, is_active);
        pane.centered(labels[i], text_style, box, palette::text);
        pane.region(box, act, static_cast<int>(i), b);
        cx += tile_w + cfg::selector_spacing;
    }
}

void monitor_row(Pane &pane, float x, float y, float w, const std::string &selected, Act act,
                 int id) {
    const std::vector<std::string> &monitors = pane.model.monitors();
    std::vector<DropEntry> entries{{"Default", -1, 0}};
    size_t active = 0;
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i] == selected) {
            active = entries.size();
        }
        entries.push_back({monitors[i], static_cast<int>(i), 0});
    }
    std::string current = entries[active].label;
    pane.dropdown({x, y, w, cfg::tile_height}, id, "Display", current, std::move(entries), active,
                  act);
}

void number_field(Pane &pane, const ui::Box &box, Field field, const std::string &value) {
    bool focused = pane.model.focused() == field;
    pane.rounded(box, metrics::radius_widget, focused ? palette::pop_b : palette::field_bg,
                 metrics::border_thin, palette::ink);
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
        pane.caret = ui::draw_field_input(pane.canvas, pane.model.field().text, pane.model.field(),
                                          pane.model.field_anim(), draw);
    } else {
        pane.left(value, text_style, box.x + cfg::field_inset, box, palette::text);
    }
    pane.region(box, Act::field_focus, static_cast<int>(field), 0);
}

void reset_icon(Pane &pane, float x, float y_center, Act act, int a, int b) {
    ui::Box box{x, y_center - cfg::reset_icon_size / 2.0f, cfg::reset_icon_size,
                cfg::reset_icon_size};
    ui::TextSize size = pane.canvas.measure(icon::refresh, icon_style);
    pane.canvas.text(icon::refresh, icon_style, box.x + (box.w - size.w) / 2.0f,
                     box.y + (box.h - size.h) / 2.0f, pane.fade(palette::text_dim));
    pane.region(box, act, a, b);
}

void bar_tab(Pane &pane, float x, float y, float w) {
    std::vector<std::string> labels;
    int active = 0;
    const Caps &caps = pane.model.caps();
    for (size_t i = 0; i < caps.bar_styles.size(); ++i) {
        labels.emplace_back(bar_style::labels[static_cast<size_t>(caps.bar_styles[i])]);
        if (caps.bar_styles[i] == pane.model.effective_bar_style()) {
            active = static_cast<int>(i);
        }
    }
    tiles(pane, x, y, w, labels, active, Act::bar_style, 0);
}

void idle_tier(Pane &pane, float x, float y, float w, std::string_view label, Field field,
               uint32_t value, uint32_t default_value, bool enabled, bool show_reset, Act reset_act,
               Act toggle_act) {
    float h = cfg::tile_height;
    pane.chip({x, y, w, h}, palette::surface);
    float inset = cfg::tile_margin;
    pane.left(label, text_style, x + inset, {x, y, w, h},
              with_alpha(palette::text, cfg::label_opacity));
    float switch_x = x + w - inset - cfg::toggle_width;
    float divider_x = switch_x - cfg::tile_spacing;
    float reset_x = divider_x - cfg::tile_spacing - cfg::reset_icon_size;
    float field_x = reset_x - cfg::tile_spacing - cfg::number_field_width;
    number_field(
        pane,
        {field_x, y + (h - cfg::field_height) / 2.0f, cfg::number_field_width, cfg::field_height},
        field, std::to_string(value));
    if (show_reset && value != default_value) {
        reset_icon(pane, reset_x, y + h / 2.0f, reset_act, 0, 1);
    }
    pane.rect({divider_x, y + 10.0f, 1.0f, h - 20.0f}, palette::text_alpha11);
    toggle_switch(pane, switch_x, y + (h - cfg::toggle_height) / 2.0f, enabled, toggle_act, 0, 1);
}

float section_label(Pane &pane, std::string_view label, float x, float y) {
    pane.text(label, small_style, x, y, palette::text_dim);
    return cfg::section_height;
}

void displays_tab(Pane &pane, float x, float y, float w) {
    const Config &config = pane.model.config();
    const std::string &selected = pane.model.displays_monitor();
    monitor_row(pane, x, y, w, selected, Act::displays_monitor, 2);
    y += cfg::tile_height + cfg::row_gap;
    bool is_default = selected.empty();
    const MonitorOverride *override = nullptr;
    if (!is_default) {
        auto it = config.monitor_overrides.find(selected);
        override = it != config.monitor_overrides.end() ? &it->second : nullptr;
    }
    bool overridden = override != nullptr && override->enabled;
    if (!is_default) {
        y += toggle_row(pane, x, y, w, "Override default settings", overridden,
                        Act::toggle_override, 0, 0, false) +
             cfg::row_gap;
    }
    if (!is_default && !overridden) {
        return;
    }
    auto value = [&](bool defaults, bool MonitorOverride::*field) {
        return is_default ? defaults : override->*field;
    };
    y += section_label(pane, "Overlays", x, y);
    auto row = [&](std::string_view label, bool on, Act act) {
        y += toggle_row(pane, x, y, w, label, on, act, 0, 0, true) + cfg::group_spacing;
    };
    row("OSD", value(config.default_osd, &MonitorOverride::osd), Act::toggle_osd);
    row("Notifications", value(config.default_notifications, &MonitorOverride::notifications),
        Act::toggle_notifications);
    if (pane.model.caps().autohide) {
        row("Bar Autohide", value(config.autohide, &MonitorOverride::autohide),
            Act::toggle_autohide);
    }
    if (!pane.model.caps().idle) {
        return;
    }
    y += section_label(pane, "Idle", x, y);
    y += toggle_row(pane, x, y, w, "Enable Idle Management", config.idle_enabled, Act::idle_enable,
                    0, 1, true) +
         cfg::group_spacing;
    if (!config.idle_enabled) {
        return;
    }
    bool ambient = is_default ? config.ambient_enabled : override->ambient_enabled;
    uint32_t ambient_timeout = is_default ? config.ambient_timeout : override->ambient_timeout;
    bool screensaver = is_default ? config.screensaver_enabled : override->screensaver_enabled;
    uint32_t screensaver_timeout =
        is_default ? config.screensaver_timeout : override->screensaver_timeout;
    idle_tier(pane, x, y, w, "Ambient Mode", Field::ambient_timeout, ambient_timeout,
              config.ambient_timeout, ambient, !is_default, Act::idle_ambient_reset,
              Act::idle_ambient);
    y += cfg::tile_height + cfg::group_spacing;
    idle_tier(pane, x, y, w, "Screensaver", Field::screensaver_timeout, screensaver_timeout,
              config.screensaver_timeout, screensaver, !is_default, Act::idle_screensaver_reset,
              Act::idle_screensaver);
}

float rain_tab(Pane &pane, float x, float y, float w) {
    float top = y;
    const RainParams &rain = pane.model.config().rain;
    tiles(pane, x, y, w, {"Matrix", "Stiletto"}, rain.mode == RainMode::Matrix ? 0 : 1,
          Act::rain_mode, 0);
    y += cfg::selector_height + cfg::row_gap;
    y += toggle_row(pane, x, y, w, "Asynchronous fall speed", rain.async_speed, Act::rain_async, 0,
                    0, true) +
         cfg::row_gap;
    return y - top;
}

struct Knob {
    Field field;
    const char *label;
};

constexpr std::array<Knob, 6> knobs{{
    {Field::visualizer_fps, "Target framerate"},
    {Field::visualizer_thin, "Particle grid density"},
    {Field::visualizer_size, "Particle size"},
    {Field::visualizer_complexity, "Fractal complexity"},
    {Field::visualizer_glow_directions, "Glow directions"},
    {Field::visualizer_glow_quality, "Glow quality"},
}};

float visualizer_tab(Pane &pane, float x, float y, float w) {
    float top = y;
    const Config &config = pane.model.config();
    tiles(pane, x, y, w, {"Bar", "Sphere"},
          config.visualizer.visualizer_shape == VisualizerShape::Bar ? 0 : 1, Act::visualizer_shape,
          0);
    y += cfg::selector_height + cfg::row_gap;
    if (config.visualizer.visualizer_shape != VisualizerShape::Sphere) {
        return y - top;
    }
    Config defaults;
    for (const Knob &knob : knobs) {
        float h = cfg::tile_height;
        pane.chip({x, y, w, h}, palette::surface);
        pane.left(knob.label, text_style, x + cfg::tile_margin, {x, y, w, h},
                  with_alpha(palette::text, cfg::label_opacity));
        float field_x = x + w - cfg::tile_margin - cfg::number_field_width;
        float reset_x = field_x - cfg::tile_spacing - cfg::reset_icon_size;
        std::string value = field_text(config, knob.field, "");
        number_field(pane,
                     {field_x, y + (h - cfg::field_height) / 2.0f, cfg::number_field_width,
                      cfg::field_height},
                     knob.field, value);
        if (value != field_text(defaults, knob.field, "")) {
            reset_icon(pane, reset_x, y + h / 2.0f, Act::visualizer_reset,
                       static_cast<int>(knob.field), 0);
        }
        y += h + cfg::row_gap;
    }
    return y - top;
}

void ambient_tab(Pane &pane, float x, float y, float w) {
    const Caps &caps = pane.model.caps();
    if (caps.rain) {
        y += section_label(pane, "Rain", x, y);
        y += rain_tab(pane, x, y, w);
    }
    if (caps.visualizer) {
        y += section_label(pane, "Visualizer", x, y);
        visualizer_tab(pane, x, y, w);
    }
}

void general_tab(Pane &pane, float x, float y, float w) {
    const Caps &caps = pane.model.caps();
    const Config &config = pane.model.config();
    if (caps.animation) {
        y += toggle_row(pane, x, y, w, "Disable Animations", config.animations_disabled,
                        Act::animation_disable, 0, 0, true) +
             cfg::row_gap;
    }
    if (caps.launcher) {
        toggle_row(pane, x, y, w, "Launcher lightning entrance", config.launcher_lightning,
                   Act::launcher_lightning, 0, 0, true);
    }
}

void wallpaper_tab(Pane &pane, float x, float y, float w, float bottom) {
    Model &model = pane.model;
    const Caps &caps = model.caps();
    const Config &config = model.config();
    bool animated = caps.animated_wallpaper && config.wallpaper_animated_enabled;
    WallpaperSlot slot = animated ? WallpaperSlot::animated : WallpaperSlot::still;
    WallpaperPicker &picker = model.picker(slot);

    if (caps.default_wallpaper) {
        y += toggle_row(pane, x, y, w, "Use Default Wallpaper", config.default_wallpaper,
                        Act::wallpaper_default, 0, 0, true) +
             cfg::row_gap;
    }
    if (caps.animated_wallpaper) {
        y += toggle_row(pane, x, y, w, "Enable animated wallpaper",
                        config.wallpaper_animated_enabled, Act::wallpaper_animated, 0, 0, true) +
             cfg::row_gap;
    }

    {
        const std::vector<std::string> &monitors = model.monitors();
        std::vector<DropEntry> entries{{"Auto", -1, 0}};
        size_t selected = 0;
        for (const WallpaperChip &chip : wallpaper_chips(model, slot)) {
            if (monitors[static_cast<size_t>(chip.monitor)] == config.capture_monitor &&
                chip.column == config.capture_column) {
                selected = entries.size();
            }
            entries.push_back({chip.label, chip.monitor, chip.column});
        }
        std::string current = entries[selected].label;
        pane.dropdown({x, y, w, cfg::tile_height}, 1, "Region Capture", current, std::move(entries),
                      selected, Act::capture_pick);
        y += cfg::tile_height + cfg::row_gap;
    }

    y = wallpaper_columns_panel(pane, slot, x, y, w, bottom);
    if (animated && model.cpu_fallback(picker.region, picker.column) && !picker.files.empty()) {
        y += cfg::row_gap;
        std::string warning = "Warning: Animated wallpaper is running on CPU.";
        ui::TextSize size = pane.canvas.measure(warning, text_style);
        float h = size.h + cfg::warning_pad * 2.0f;
        pane.chip({x, y, w, h}, palette::critical_container);
        pane.text(warning, text_style, x + cfg::warning_pad, y + (h - size.h) / 2.0f,
                  palette::text);
    }
}

void lock_tab(Pane &pane, float x, float y, float w, float bottom) {
    const Caps &caps = pane.model.caps();
    const Config &config = pane.model.config();
    tiles(pane, x, y, w, {"Auto", "Custom"}, config.lock_wallpaper_custom ? 1 : 0,
          Act::lock_wallpaper_mode, 0);
    y += cfg::selector_height + cfg::row_gap;
    if (!config.lock_wallpaper_custom) {
        return;
    }
    if (caps.default_wallpaper) {
        y += toggle_row(pane, x, y, w, "Use Default Wallpaper", config.default_wallpaper,
                        Act::wallpaper_default, 0, 0, true) +
             cfg::row_gap;
    }
    wallpaper_columns_panel(pane, WallpaperSlot::lock, x, y, w, bottom);
}

void tab_content(Pane &pane, Tab tab, float x, float y, float w, float bottom) {
    switch (tab) {
    case Tab::ambient:
        ambient_tab(pane, x, y, w);
        break;
    case Tab::bar:
        bar_tab(pane, x, y, w);
        break;
    case Tab::displays:
        displays_tab(pane, x, y, w);
        break;
    case Tab::general:
        general_tab(pane, x, y, w);
        break;
    case Tab::lock:
        lock_tab(pane, x, y, w, bottom);
        break;
    case Tab::wallpaper:
        wallpaper_tab(pane, x, y, w, bottom);
        break;
    }
    if (pane.overlay) {
        pane.overlay();
    }
}

std::string tab_summary(const Model &model, Tab tab) {
    const Config &config = model.config();
    const Caps &caps = model.caps();
    switch (tab) {
    case Tab::ambient: {
        std::string rain = config.rain.mode == RainMode::Matrix ? "Matrix" : "Stiletto";
        std::string shape =
            config.visualizer.visualizer_shape == VisualizerShape::Bar ? "Bars" : "Sphere";
        return caps.rain && caps.visualizer ? rain + " / " + shape : caps.rain ? rain : shape;
    }
    case Tab::bar:
        return std::string(bar_style::labels[static_cast<size_t>(model.effective_bar_style())]);
    case Tab::displays:
        return std::to_string(model.monitors().size()) + " connected";
    case Tab::general:
        return config.animations_disabled ? "Animations off" : "Animations on";
    case Tab::lock:
        return config.lock_wallpaper_custom ? "Custom" : "Auto";
    case Tab::wallpaper:
        return config.wallpaper_animated_enabled ? "Animated" : "Static";
    }
    return {};
}

void profile_strip(Pane &pane, const Art &art, float x, float y) {
    ui::Canvas &canvas = pane.canvas;
    ui::Box avatar{x, y, cfg::avatar_size, cfg::avatar_size};
    ui::ImageId face = art.face.empty()
                           ? ui::no_image
                           : canvas.image(art.face, static_cast<int>(cfg::avatar_size) * 2);
    if (face != ui::no_image) {
        canvas.rounded(avatar, cfg::avatar_size / 2.0f, palette::surface);
        canvas.begin_group(avatar, {1.0f, true, cfg::avatar_size / 2.0f});
        canvas.draw_image(face, {0.0f, 0.0f, avatar.w, avatar.h}, palette::white);
        canvas.end_group();
        canvas.rounded(avatar, cfg::avatar_size / 2.0f, transparent, cfg::avatar_border,
                       palette::ink);
    } else {
        canvas.rounded(avatar, cfg::avatar_size / 2.0f, palette::text_alpha04, cfg::avatar_border,
                       palette::ink);
        ui::TextSize glyph = canvas.measure(icon::user, icon_style);
        canvas.text(icon::user, icon_style, avatar.x + (avatar.w - glyph.w) / 2.0f,
                    avatar.y + (avatar.h - glyph.h) / 2.0f, palette::text);
    }
    float text_x = x + cfg::avatar_size + cfg::profile_label_gap;
    ui::TextSize name = canvas.measure(art.user_name, text_style);
    canvas.text(art.user_name, text_style, text_x, y, palette::text);
    canvas.text(art.uptime, small_style, text_x, y + name.h + cfg::profile_line_gap,
                palette::text_dim);
}

void menu_page(Pane &pane, const Art &art, float x, float y, float w) {
    profile_strip(pane, art, x, y);
    y += cfg::avatar_size + cfg::menu_tile_gap;
    int columns =
        w > cfg::menu_collapse_breakpoint ? cfg::menu_columns_wide : cfg::menu_columns_narrow;
    float stagger = cfg::menu_stagger;
    float tile_w = (w - stagger - cfg::menu_tile_gap * static_cast<float>(columns - 1)) /
                   static_cast<float>(columns);
    const Color *tints[] = {&palette::pop_a, &palette::pop_b, &palette::pop_c};
    int index = 0;
    for (const TabInfo &info : pane.model.tabs()) {
        int row = index / columns;
        int col = index % columns;
        ui::Box box{x + (row % 2 == 1 ? stagger : 0.0f) +
                        static_cast<float>(col) * (tile_w + cfg::menu_tile_gap),
                    y + static_cast<float>(row) * (cfg::menu_tile_height + cfg::menu_tile_gap),
                    tile_w, cfg::menu_tile_height};
        pane.chip(box, *tints[index % 3]);
        float ix = box.x + cfg::menu_tile_pad;
        float iy = box.y + cfg::menu_tile_pad;
        pane.text(info.icon, big_icon_style, ix, iy, palette::text);
        ui::TextSize label =
            pane.text(info.label, title_style, ix,
                      box.y + box.h - cfg::menu_tile_pad - cfg::menu_label_block, palette::text);
        pane.text(tab_summary(pane.model, info.tab), small_style, ix,
                  box.y + box.h - cfg::menu_tile_pad - cfg::menu_label_block + label.h,
                  with_alpha(palette::text, cfg::label_opacity));
        pane.region(box, Act::tab, static_cast<int>(info.tab));
        ++index;
    }
}

} // namespace

Frame paint(ui::Canvas &canvas, Model &model, const Art &art, float width, float height) {
    model.begin_frame();
    Frame frame;
    float pw = std::min(width - 2.0f * cfg::card_margin, cfg::card_max_width);
    float ph = std::min(height - 2.0f * cfg::card_margin, cfg::card_max_height);
    float px = (width - pw) / 2.0f;
    float py = (height - ph) / 2.0f;
    frame.panel = {px, py, pw, ph};
    model.set_panel(frame.panel);

    constexpr ScrollMetrics scroll_style{};
    const ScrollSequence &sequence = model.sequence();
    ScrollOffset slide = scroll_slide_offset(
        ScrollEntry::from_bottom, sequence.slide(),
        scroll_bounds(scroll_geometry(frame.panel, 0.0f, scroll_style)), width, height);
    paint_scroll(canvas,
                 scroll_geometry({px, py + slide.dy, pw, ph}, sequence.scroll(), scroll_style),
                 scroll_style);
    if (sequence.grid() <= 0.0f) {
        return frame;
    }
    canvas.set_opacity(sequence.grid());

    float pad = cfg::card_padding;
    float header_y = py + pad;
    bool menu = model.in_menu();
    float title_x = px + pad;
    std::string_view title_text = "Settings";
    if (!menu) {
        ui::Box back{px + pad, header_y + (cfg::header_height - cfg::back_height) / 2.0f,
                     cfg::back_width, cfg::back_height};
        selectable(canvas, back, metrics::radius_widget, false, false, palette::accent_container);
        ui::TextSize arrow = canvas.measure(icon::chevron_left, icon_style);
        canvas.text(icon::chevron_left, icon_style, back.x + cfg::tile_margin / 2.0f,
                    back.y + (back.h - arrow.h) / 2.0f, palette::text);
        ui::TextSize back_label = canvas.measure("Menu", text_style);
        canvas.text("Menu", text_style, back.x + cfg::tile_margin + arrow.w,
                    back.y + (back.h - back_label.h) / 2.0f, palette::text);
        model.add_region(back, Act::menu);
        title_x = back.x + back.w + cfg::tile_margin;
        for (const TabInfo &info : model.tabs()) {
            if (info.tab == model.active_tab()) {
                title_text = info.label;
            }
        }
    }
    ui::TextSize title = canvas.measure(title_text, title_style);
    canvas.text(title_text, title_style, title_x, header_y + (cfg::header_height - title.h) / 2.0f,
                palette::text);
    ui::Box close{px + pw - pad - cfg::action_button_size,
                  header_y + (cfg::header_height - cfg::action_button_size) / 2.0f,
                  cfg::action_button_size, cfg::action_button_size};
    selectable(canvas, close, metrics::radius_widget, false, false, palette::accent_container);
    ui::TextSize close_size = canvas.measure(icon::close, icon_style);
    canvas.text(icon::close, icon_style, close.x + (close.w - close_size.w) / 2.0f,
                close.y + (close.h - close_size.h) / 2.0f, palette::text);
    model.add_region(close, Act::close);

    float content_y = header_y + cfg::header_height + cfg::header_divider_gap;
    canvas.rect({px + pad, content_y, pw - 2.0f * pad, 1.0f}, palette::text_alpha11);
    content_y += 1.0f + cfg::content_gap;

    float content_x = px + pad;
    float content_w = pw - 2.0f * pad;
    float bottom = py + ph - pad;
    Pane pane{canvas, model, model.tab_alpha(), {}, {}};
    if (menu) {
        menu_page(pane, art, content_x, content_y, content_w);
    } else {
        tab_content(pane, model.active_tab(), content_x, content_y, content_w, bottom);
    }
    frame.caret = pane.caret;
    canvas.set_opacity(1.0f);
    return frame;
}

} // namespace astralia::settings
