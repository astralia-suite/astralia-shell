//! Draws the settings card: the scroll, the header, and the tile menu or the page of a tab.

use super::model::{Act, Field, Model, Tab, field_text};
use super::pane::{DropEntry, ICON_STYLE, Pane, SMALL_STYLE, TEXT_STYLE, TRANSPARENT};
use super::wallpaper_columns::{wallpaper_chips, wallpaper_columns_panel};
use crate::config::settings::{BarStyle, Config, MonitorOverride, PaletteSource, RainMode, VisualizerShape};
use crate::config::settings_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextStyle};
use crate::render::field_view::{FieldDraw, draw_field_input};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::scroll::{ScrollEntry, ScrollMetrics, paint_scroll, scroll_bounds, scroll_geometry, scroll_slide_offset};
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{WHITE, metrics, with_alpha};
use crate::service::wallpaper::WallpaperSlot;

const TITLE_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TITLE_PX, bold: true, max_width: 0, wrap: false };
const BIG_ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::MENU_ICON_PX, bold: false, max_width: 0, wrap: false };

/// What the menu page shows about the user.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Art {
    pub user_name: String,
    pub uptime: String,
    /// The avatar image; empty draws a glyph.
    pub face: String,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Frame {
    pub caret: Rect,
    pub panel: Rect,
}

fn toggle_switch(pane: &mut Pane, x: f32, y: f32, on: bool, act: Act, a: i32, b: i32) {
    let p = palette();
    let track = Rect { x, y, w: cfg::TOGGLE_WIDTH, h: cfg::TOGGLE_HEIGHT };
    pane.selectable(track, on, p.surface_alt);
    let knob_x = if on { x + cfg::TOGGLE_WIDTH - cfg::TOGGLE_KNOB - cfg::TOGGLE_KNOB_INSET } else { x + cfg::TOGGLE_KNOB_INSET };
    let knob = Rect { x: knob_x, y: y + (cfg::TOGGLE_HEIGHT - cfg::TOGGLE_KNOB) / 2.0, w: cfg::TOGGLE_KNOB, h: cfg::TOGGLE_KNOB };
    pane.rounded(knob, metrics::RADIUS_WIDGET.min(cfg::TOGGLE_KNOB / 2.0), p.text, 0.0, TRANSPARENT);
    pane.region(track, act, a, b);
}

#[allow(clippy::too_many_arguments)]
fn toggle_row(pane: &mut Pane, x: f32, y: f32, w: f32, label: &str, value: bool, act: Act, a: i32, b: i32, tiled: bool) -> f32 {
    let p = palette();
    let h = if tiled { cfg::TILE_HEIGHT } else { cfg::TOGGLE_HEIGHT };
    let inset = if tiled { cfg::TILE_MARGIN } else { 0.0 };
    if tiled {
        pane.chip(Rect { x, y, w, h }, p.surface);
    }
    let color = if tiled { with_alpha(p.text, cfg::LABEL_OPACITY) } else { p.text };
    pane.left(label, &TEXT_STYLE, x + inset, Rect { x, y, w, h }, color);
    toggle_switch(pane, x + w - inset - cfg::TOGGLE_WIDTH, y + (h - cfg::TOGGLE_HEIGHT) / 2.0, value, act, a, b);
    h
}

#[allow(clippy::too_many_arguments)]
fn tiles(pane: &mut Pane, x: f32, y: f32, w: f32, labels: &[&str], active: usize, act: Act, b: i32) {
    let count = labels.len() as f32;
    let tile_w = (w - cfg::SELECTOR_SPACING * (count - 1.0)) / count;
    let mut cx = x;
    for (i, label) in labels.iter().enumerate() {
        let r = Rect { x: cx, y, w: tile_w, h: cfg::SELECTOR_HEIGHT };
        pane.selectable(r, i == active, palette().surface);
        pane.centered(label, &TEXT_STYLE, r, palette().text);
        pane.region(r, act, i as i32, b);
        cx += tile_w + cfg::SELECTOR_SPACING;
    }
}

fn monitor_row(pane: &mut Pane, x: f32, y: f32, w: f32, selected: &str, act: Act, id: i32) {
    let mut entries = vec![DropEntry { label: "Default".into(), a: -1, b: 0 }];
    let mut active = 0;
    for (i, name) in pane.model.monitors().iter().enumerate() {
        if name == selected {
            active = entries.len();
        }
        entries.push(DropEntry { label: name.clone(), a: i as i32, b: 0 });
    }
    let current = entries[active].label.clone();
    pane.dropdown(Rect { x, y, w, h: cfg::TILE_HEIGHT }, id, "Display", &current, entries, Some(active), act);
}

fn number_field(pane: &mut Pane, r: Rect, field: Field, value: &str) {
    let p = palette();
    let focused = pane.model.focused() == field;
    pane.control(r, metrics::RADIUS_WIDGET, if focused { p.pop_b } else { p.field_bg });
    if focused {
        let draw = FieldDraw {
            x: r.x + cfg::FIELD_INSET,
            center_y: r.y + r.h / 2.0,
            caret_height: r.h - 10.0,
            caret_width: 1.5,
            caret_gap: 2.0,
            color: pane.fade(p.text),
            ..FieldDraw::new(TEXT_STYLE)
        };
        let model = &*pane.model;
        pane.caret = draw_field_input(pane.canvas, &model.field().text, model.field(), model.field_anim(), &draw, None);
    } else {
        pane.left(value, &TEXT_STYLE, r.x + cfg::FIELD_INSET, r, p.text);
    }
    pane.region(r, Act::FieldFocus, field as i32, 0);
}

fn reset_icon(pane: &mut Pane, x: f32, y_center: f32, act: Act, a: i32, b: i32) {
    let r = Rect { x, y: y_center - cfg::RESET_ICON_SIZE / 2.0, w: cfg::RESET_ICON_SIZE, h: cfg::RESET_ICON_SIZE };
    let size = pane.canvas.measure(icons::REFRESH, &ICON_STYLE);
    let color = pane.fade(palette().text_dim);
    pane.canvas.text(icons::REFRESH, &ICON_STYLE, r.x + (r.w - size.w) / 2.0, r.y + (r.h - size.h) / 2.0, color);
    pane.region(r, act, a, b);
}

fn bar_tab(pane: &mut Pane, x: f32, y: f32, w: f32) {
    let current = pane.config.bar_style;
    let labels: Vec<&str> = BarStyle::ALL.iter().map(|s| s.label()).collect();
    let active = BarStyle::ALL.iter().position(|&s| s == current).unwrap_or(0);
    tiles(pane, x, y, w, &labels, active, Act::BarStyle, 0);
    let expand = pane.config.expand_widgets;
    toggle_row(pane, x, y + cfg::SELECTOR_HEIGHT + cfg::ROW_GAP, w, "Expand Widgets", expand, Act::ToggleExpandWidgets, 0, 0, true);
}

#[allow(clippy::too_many_arguments)]
fn idle_tier(pane: &mut Pane, x: f32, y: f32, w: f32, label: &str, field: Field, value: u32, default_value: u32, enabled: bool, show_reset: bool, reset_act: Act, toggle_act: Act) {
    let p = palette();
    let h = cfg::TILE_HEIGHT;
    pane.chip(Rect { x, y, w, h }, p.surface);
    let inset = cfg::TILE_MARGIN;
    pane.left(label, &TEXT_STYLE, x + inset, Rect { x, y, w, h }, with_alpha(p.text, cfg::LABEL_OPACITY));
    let switch_x = x + w - inset - cfg::TOGGLE_WIDTH;
    let divider_x = switch_x - cfg::TILE_SPACING;
    let reset_x = divider_x - cfg::TILE_SPACING - cfg::RESET_ICON_SIZE;
    let field_x = reset_x - cfg::TILE_SPACING - cfg::NUMBER_FIELD_WIDTH;
    number_field(pane, Rect { x: field_x, y: y + (h - cfg::FIELD_HEIGHT) / 2.0, w: cfg::NUMBER_FIELD_WIDTH, h: cfg::FIELD_HEIGHT }, field, &value.to_string());
    if show_reset && value != default_value {
        reset_icon(pane, reset_x, y + h / 2.0, reset_act, 0, 1);
    }
    pane.rect(Rect { x: divider_x, y: y + 10.0, w: 1.0, h: h - 20.0 }, p.text_alpha11);
    toggle_switch(pane, switch_x, y + (h - cfg::TOGGLE_HEIGHT) / 2.0, enabled, toggle_act, 0, 1);
}

fn section_label(pane: &mut Pane, label: &str, x: f32, y: f32) -> f32 {
    pane.text(label, &SMALL_STYLE, x, y, palette().text_dim);
    cfg::SECTION_HEIGHT
}

/// The display dropdown and, for a monitor, its override switch; the `y` below them, or `None`
/// when the monitor is not overridden and the page stops there.
#[allow(clippy::too_many_arguments)]
fn monitor_scope(pane: &mut Pane, x: f32, mut y: f32, w: f32, selected: &str, pick: Act, toggle: Act, overridden: bool) -> Option<f32> {
    monitor_row(pane, x, y, w, selected, pick, 2);
    y += cfg::TILE_HEIGHT + cfg::ROW_GAP;
    if selected.is_empty() {
        return Some(y);
    }
    y += toggle_row(pane, x, y, w, "Override default settings", overridden, toggle, 0, 0, false) + cfg::ROW_GAP;
    overridden.then_some(y)
}

fn displays_tab(pane: &mut Pane, x: f32, y: f32, w: f32) {
    let config = pane.config;
    let selected = pane.model.displays_monitor().to_string();
    let is_default = selected.is_empty();
    let o: Option<&MonitorOverride> = if is_default { None } else { config.monitor_overrides.get(&selected) };
    let Some(mut y) = monitor_scope(pane, x, y, w, &selected, Act::DisplaysMonitor, Act::ToggleOverride, o.is_some_and(|o| o.enabled)) else { return };
    // Shown only for the defaults or an enabled override, so `o` is present when not default.
    let value = |defaults: bool, field: fn(&MonitorOverride) -> bool| match o {
        Some(o) if !is_default => field(o),
        _ => defaults,
    };
    y += section_label(pane, "Overlays", x, y);
    let mut row = |pane: &mut Pane, label: &str, on: bool, act: Act| {
        y += toggle_row(pane, x, y, w, label, on, act, 0, 0, true) + cfg::GROUP_SPACING;
    };
    row(pane, "OSD", value(config.default_osd, |o| o.osd), Act::ToggleOsd);
    row(pane, "Notifications", value(config.default_notifications, |o| o.notifications), Act::ToggleNotifications);
    row(pane, "Bar Autohide", value(config.autohide, |o| o.autohide), Act::ToggleAutohide);
    y += section_label(pane, "Idle", x, y);
    y += toggle_row(pane, x, y, w, "Enable Idle Management", config.idle_enabled, Act::IdleEnable, 0, 1, true) + cfg::GROUP_SPACING;
    if !config.idle_enabled {
        return;
    }
    let ambient = value(config.ambient_enabled, |o| o.ambient_enabled);
    let screensaver = value(config.screensaver_enabled, |o| o.screensaver_enabled);
    let (ambient_timeout, screensaver_timeout) = match o {
        Some(o) if !is_default => (o.ambient_timeout, o.screensaver_timeout),
        _ => (config.ambient_timeout, config.screensaver_timeout),
    };
    idle_tier(pane, x, y, w, "Ambient Mode", Field::AmbientTimeout, ambient_timeout, config.ambient_timeout, ambient, !is_default, Act::IdleAmbientReset, Act::IdleAmbient);
    y += cfg::TILE_HEIGHT + cfg::GROUP_SPACING;
    idle_tier(
        pane,
        x,
        y,
        w,
        "Screensaver",
        Field::ScreensaverTimeout,
        screensaver_timeout,
        config.screensaver_timeout,
        screensaver,
        !is_default,
        Act::IdleScreensaverReset,
        Act::IdleScreensaver,
    );
}

fn rain_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32) -> f32 {
    let top = y;
    let rain = pane.config.rain.clone();
    tiles(pane, x, y, w, &["Matrix", "Stiletto"], usize::from(rain.mode != RainMode::Matrix), Act::RainMode, 0);
    y += cfg::SELECTOR_HEIGHT + cfg::ROW_GAP;
    y += toggle_row(pane, x, y, w, "Asynchronous fall speed", rain.async_speed, Act::RainAsync, 0, 0, true) + cfg::ROW_GAP;
    y - top
}

const KNOBS: [(Field, &str); 6] = [
    (Field::VisualizerFps, "Target framerate"),
    (Field::VisualizerThin, "Particle grid density"),
    (Field::VisualizerSize, "Particle size"),
    (Field::VisualizerComplexity, "Fractal complexity"),
    (Field::VisualizerGlowDirections, "Glow directions"),
    (Field::VisualizerGlowQuality, "Glow quality"),
];

fn visualizer_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32) -> f32 {
    let p = palette();
    let top = y;
    let config = pane.config;
    let shape = config.visualizer.visualizer_shape;
    tiles(pane, x, y, w, &["Bar", "Sphere"], usize::from(shape != VisualizerShape::Bar), Act::VisualizerShape, 0);
    y += cfg::SELECTOR_HEIGHT + cfg::ROW_GAP;
    if shape != VisualizerShape::Sphere {
        return y - top;
    }
    let defaults = Config::default();
    for (field, label) in KNOBS {
        let h = cfg::TILE_HEIGHT;
        pane.chip(Rect { x, y, w, h }, p.surface);
        pane.left(label, &TEXT_STYLE, x + cfg::TILE_MARGIN, Rect { x, y, w, h }, with_alpha(p.text, cfg::LABEL_OPACITY));
        let field_x = x + w - cfg::TILE_MARGIN - cfg::NUMBER_FIELD_WIDTH;
        let reset_x = field_x - cfg::TILE_SPACING - cfg::RESET_ICON_SIZE;
        let value = field_text(config, field, "");
        number_field(pane, Rect { x: field_x, y: y + (h - cfg::FIELD_HEIGHT) / 2.0, w: cfg::NUMBER_FIELD_WIDTH, h: cfg::FIELD_HEIGHT }, field, &value);
        if value != field_text(&defaults, field, "") {
            reset_icon(pane, reset_x, y + h / 2.0, Act::VisualizerReset, field as i32, 0);
        }
        y += h + cfg::ROW_GAP;
    }
    y - top
}

fn ambient_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32) {
    y += section_label(pane, "Rain", x, y);
    y += rain_tab(pane, x, y, w);
    y += section_label(pane, "Visualizer", x, y);
    visualizer_tab(pane, x, y, w);
}

fn general_tab(pane: &mut Pane, x: f32, y: f32, w: f32) {
    toggle_row(pane, x, y, w, "Disable Animations", pane.config.animations_disabled, Act::AnimationDisable, 0, 0, true);
}

fn desktop_slot(pane: &Pane) -> WallpaperSlot {
    if pane.config.wallpaper_animated_enabled { WallpaperSlot::Animated } else { WallpaperSlot::Still }
}

fn wallpaper_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32, bottom: f32) {
    let p = palette();
    let config = pane.config;
    let slot = desktop_slot(pane);
    let animated = slot == WallpaperSlot::Animated;

    let toggles: Vec<(&str, bool, Act)> = vec![
        ("Use Default Wallpaper", config.default_wallpaper, Act::WallpaperDefault),
        ("Enable animated wallpaper", config.wallpaper_animated_enabled, Act::WallpaperAnimated),
    ];

    let count = (toggles.len() + 1) as f32;
    let share = (w - cfg::TILE_SPACING * (count - 1.0)) / count;
    let mut equal = true;
    let mut natural = 0.0;
    let mut needs = Vec::new();
    for (label, _, _) in &toggles {
        let need = pane.canvas.measure(label, &TEXT_STYLE).w + 2.0 * cfg::TILE_MARGIN + cfg::TILE_SPACING + cfg::TOGGLE_WIDTH;
        needs.push(need);
        natural += need + cfg::TILE_SPACING;
        equal = equal && need <= share;
    }
    let inline_row = !toggles.is_empty() && (if equal { share } else { w - natural }) >= cfg::DISPLAY_TILE_MIN_WIDTH;

    if !inline_row {
        for (label, value, act) in &toggles {
            y += toggle_row(pane, x, y, w, label, *value, *act, 0, 0, true) + cfg::ROW_GAP;
        }
        y = wallpaper_columns_panel(pane, slot, x, y, w, bottom, None);
    } else {
        let mut cx = x;
        for (i, (label, value, act)) in toggles.iter().enumerate() {
            let tile_w = if equal { share } else { needs[i] };
            toggle_row(pane, cx, y, tile_w, label, *value, *act, 0, 0, true);
            cx += tile_w + cfg::TILE_SPACING;
        }
        let display = Rect { x: cx, y, w: x + w - cx, h: cfg::TILE_HEIGHT };
        y = wallpaper_columns_panel(pane, slot, x, y + cfg::TILE_HEIGHT + cfg::ROW_GAP, w, bottom, Some(display));
    }
    let picker = pane.model.picker(slot);
    if animated && !picker.files.is_empty() && pane.model.cpu_fallback(&picker.region, picker.column) {
        y += cfg::ROW_GAP;
        let warning = "Warning: Animated wallpaper is running on CPU.";
        let size = pane.canvas.measure(warning, &TEXT_STYLE);
        let h = size.h + cfg::WARNING_PAD * 2.0;
        pane.chip(Rect { x, y, w, h }, p.critical_container);
        pane.text(warning, &TEXT_STYLE, x + cfg::WARNING_PAD, y + (h - size.h) / 2.0, p.text);
    }
}

fn palette_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32) {
    let config = pane.config;
    let region = config.palette_source == PaletteSource::RegionCapture;
    let sources = vec![DropEntry { label: "Default".into(), a: 0, b: 0 }, DropEntry { label: "Region capture".into(), a: 1, b: 0 }];
    let selected = usize::from(region);
    let current = sources[selected].label.clone();
    pane.dropdown(Rect { x, y, w, h: cfg::TILE_HEIGHT }, 2, "Palette", &current, sources, Some(selected), Act::PaletteSource);
    if !region {
        return;
    }
    y += cfg::TILE_HEIGHT + cfg::ROW_GAP;
    let slot = desktop_slot(pane);
    let mut entries = vec![DropEntry { label: "Auto".into(), a: -1, b: 0 }];
    let mut selected = 0;
    for chip in wallpaper_chips(pane.model, slot) {
        if pane.model.monitors()[chip.monitor] == config.capture_monitor && chip.column == config.capture_column {
            selected = entries.len();
        }
        entries.push(DropEntry { label: chip.label, a: chip.monitor as i32, b: chip.column });
    }
    let current = entries[selected].label.clone();
    pane.dropdown(Rect { x, y, w, h: cfg::TILE_HEIGHT }, 1, "Region Capture", &current, entries, Some(selected), Act::CapturePick);
}

fn lock_tab(pane: &mut Pane, x: f32, mut y: f32, w: f32, bottom: f32) {
    let config = pane.config;
    tiles(pane, x, y, w, &["Auto", "Custom"], usize::from(config.lock_wallpaper_custom), Act::LockWallpaperMode, 0);
    y += cfg::SELECTOR_HEIGHT + cfg::ROW_GAP;
    if !config.lock_wallpaper_custom {
        return;
    }
    y += toggle_row(pane, x, y, w, "Use Default Wallpaper", config.default_wallpaper, Act::WallpaperDefault, 0, 0, true) + cfg::ROW_GAP;
    wallpaper_columns_panel(pane, WallpaperSlot::Lock, x, y, w, bottom, None);
}

fn tab_content(pane: &mut Pane, tab: Tab, x: f32, y: f32, w: f32, bottom: f32) {
    match tab {
        Tab::Ambient => ambient_tab(pane, x, y, w),
        Tab::Bar => bar_tab(pane, x, y, w),
        Tab::Displays => displays_tab(pane, x, y, w),
        Tab::General => general_tab(pane, x, y, w),
        Tab::Lock => lock_tab(pane, x, y, w, bottom),
        Tab::Wallpaper => wallpaper_tab(pane, x, y, w, bottom),
        Tab::Palette => palette_tab(pane, x, y, w),
    }
    pane.finish();
}

/// The one-line state a menu tile shows under its name.
fn tab_summary(model: &Model, config: &Config, tab: Tab) -> String {
    match tab {
        Tab::Ambient => {
            let rain = if config.rain.mode == RainMode::Matrix { "Matrix" } else { "Stiletto" };
            let shape = if config.visualizer.visualizer_shape == VisualizerShape::Bar { "Bars" } else { "Sphere" };
            format!("{rain} / {shape}")
        }
        Tab::Bar => config.bar_style.label().into(),
        Tab::Displays => format!("{} connected", model.monitors().len()),
        Tab::General => (if config.animations_disabled { "Animations off" } else { "Animations on" }).into(),
        Tab::Lock => (if config.lock_wallpaper_custom { "Custom" } else { "Auto" }).into(),
        Tab::Wallpaper => (if config.wallpaper_animated_enabled { "Animated" } else { "Static" }).into(),
        Tab::Palette => (if config.palette_source == PaletteSource::RegionCapture { "Region capture" } else { "Default" }).into(),
    }
}

fn profile_strip(canvas: &mut dyn Canvas, art: &Art, x: f32, y: f32) {
    let p = palette();
    let size = cfg::AVATAR_SIZE;
    let avatar = Rect { x, y, w: size, h: size };
    let face = if art.face.is_empty() { NO_IMAGE } else { canvas.image(&art.face, size as i32 * 2) };
    let ring = |fill| (fill, StickerStyle { outline: p.border_outer, border_width: cfg::AVATAR_BORDER, shadow: 0.0, ..StickerStyle::default() });
    if face != NO_IMAGE {
        canvas.rounded(avatar, size / 2.0, p.surface, 0.0, TRANSPARENT);
        canvas.begin_group(avatar, GroupOptions { clip: true, radius: size / 2.0, ..GroupOptions::default() });
        canvas.draw_image(face, Rect { x: 0.0, y: 0.0, w: avatar.w, h: avatar.h }, WHITE);
        canvas.end_group();
        let (fill, style) = ring(TRANSPARENT);
        sticker(canvas, avatar, size / 2.0, fill, &style);
    } else {
        let (fill, style) = ring(p.text_alpha04);
        sticker(canvas, avatar, size / 2.0, fill, &style);
        let glyph = canvas.measure(icons::USER, &ICON_STYLE);
        canvas.text(icons::USER, &ICON_STYLE, avatar.x + (avatar.w - glyph.w) / 2.0, avatar.y + (avatar.h - glyph.h) / 2.0, p.text);
    }
    let text_x = x + size + cfg::PROFILE_LABEL_GAP;
    let name = canvas.measure(&art.user_name, &TEXT_STYLE);
    canvas.text(&art.user_name, &TEXT_STYLE, text_x, y, p.text);
    canvas.text(&art.uptime, &SMALL_STYLE, text_x, y + name.h + cfg::PROFILE_LINE_GAP, p.text_dim);
}

fn menu_page(pane: &mut Pane, art: &Art, x: f32, mut y: f32, w: f32) {
    let p = palette();
    profile_strip(pane.canvas, art, x, y);
    y += cfg::AVATAR_SIZE + cfg::MENU_TILE_GAP;
    let columns = if w > cfg::MENU_COLLAPSE_BREAKPOINT { cfg::MENU_COLUMNS_WIDE } else { cfg::MENU_COLUMNS_NARROW } as usize;
    let stagger = cfg::MENU_STAGGER;
    let tile_w = (w - stagger - cfg::MENU_TILE_GAP * (columns - 1) as f32) / columns as f32;
    let tints = [p.pop_a, p.pop_b, p.pop_c];
    let tabs = pane.model.tabs().to_vec();
    for (index, info) in tabs.iter().enumerate() {
        let (row, col) = (index / columns, index % columns);
        let r = Rect {
            x: x + if row % 2 == 1 { stagger } else { 0.0 } + col as f32 * (tile_w + cfg::MENU_TILE_GAP),
            y: y + row as f32 * (cfg::MENU_TILE_HEIGHT + cfg::MENU_TILE_GAP),
            w: tile_w,
            h: cfg::MENU_TILE_HEIGHT,
        };
        pane.chip(r, tints[index % 3]);
        let (ix, iy) = (r.x + cfg::MENU_TILE_PAD, r.y + cfg::MENU_TILE_PAD);
        pane.text(info.icon, &BIG_ICON_STYLE, ix, iy, p.text);
        let label_y = r.y + r.h - cfg::MENU_TILE_PAD - cfg::MENU_LABEL_BLOCK;
        let label = pane.text(info.label, &TITLE_STYLE, ix, label_y, p.text);
        let summary = tab_summary(pane.model, pane.config, info.tab);
        pane.text(&summary, &SMALL_STYLE, ix, label_y + label.h, with_alpha(p.text, cfg::LABEL_OPACITY));
        pane.region(r, Act::Tab, info.tab as i32, 0);
    }
}

pub fn paint(canvas: &mut dyn Canvas, model: &mut Model, art: &Art, width: f32, height: f32) -> Frame {
    let p = palette();
    model.begin_frame();
    let pw = (width - 2.0 * cfg::CARD_MARGIN).min(cfg::CARD_MAX_WIDTH);
    let ph = (height - 2.0 * cfg::CARD_MARGIN).min(cfg::CARD_MAX_HEIGHT);
    let px = (width - pw) / 2.0;
    let py = (height - ph) / 2.0;
    let mut frame = Frame { panel: Rect { x: px, y: py, w: pw, h: ph }, ..Frame::default() };
    model.set_panel(frame.panel);

    let style = ScrollMetrics::default();
    let sequence = model.sequence();
    let slide = scroll_slide_offset(ScrollEntry::FromBottom, sequence.slide(), scroll_bounds(&scroll_geometry(frame.panel, 0.0, &style)), width, height);
    paint_scroll(canvas, &scroll_geometry(Rect { x: px, y: py + slide.dy, w: pw, h: ph }, sequence.scroll(), &style), &style);
    let grid = sequence.grid();
    if grid <= 0.0 {
        return frame;
    }
    canvas.set_opacity(grid);

    let pad = cfg::CARD_PADDING;
    let header_y = py + pad;
    let menu = model.in_menu();
    let mut title_x = px + pad;
    let mut title_text = "Settings";
    if !menu {
        let back = Rect { x: px + pad, y: header_y + (cfg::HEADER_HEIGHT - cfg::BACK_HEIGHT) / 2.0, w: cfg::BACK_WIDTH, h: cfg::BACK_HEIGHT };
        selectable(canvas, back, metrics::RADIUS_WIDGET, false, false, p.accent_container, 1.0);
        let arrow = canvas.measure(icons::CHEVRON_LEFT, &ICON_STYLE);
        canvas.text(icons::CHEVRON_LEFT, &ICON_STYLE, back.x + cfg::TILE_MARGIN / 2.0, back.y + (back.h - arrow.h) / 2.0, p.text);
        let back_label = canvas.measure("Menu", &TEXT_STYLE);
        canvas.text("Menu", &TEXT_STYLE, back.x + cfg::TILE_MARGIN + arrow.w, back.y + (back.h - back_label.h) / 2.0, p.text);
        model.add_region(back, Act::Menu, 0, 0);
        title_x = back.x + back.w + cfg::TILE_MARGIN;
        if let Some(info) = model.tabs().iter().find(|t| t.tab == model.active_tab()) {
            title_text = info.label;
        }
    }
    let title = canvas.measure(title_text, &TITLE_STYLE);
    canvas.text(title_text, &TITLE_STYLE, title_x, header_y + (cfg::HEADER_HEIGHT - title.h) / 2.0, p.text);
    let close = Rect {
        x: px + pw - pad - cfg::ACTION_BUTTON_SIZE,
        y: header_y + (cfg::HEADER_HEIGHT - cfg::ACTION_BUTTON_SIZE) / 2.0,
        w: cfg::ACTION_BUTTON_SIZE,
        h: cfg::ACTION_BUTTON_SIZE,
    };
    selectable(canvas, close, metrics::RADIUS_WIDGET, false, false, p.accent_container, 1.0);
    let close_size = canvas.measure(icons::CLOSE, &ICON_STYLE);
    canvas.text(icons::CLOSE, &ICON_STYLE, close.x + (close.w - close_size.w) / 2.0, close.y + (close.h - close_size.h) / 2.0, p.text);
    model.add_region(close, Act::Close, 0, 0);

    let mut content_y = header_y + cfg::HEADER_HEIGHT + cfg::HEADER_DIVIDER_GAP;
    canvas.rect(Rect { x: px + pad, y: content_y, w: pw - 2.0 * pad, h: 1.0 }, p.text_alpha11);
    content_y += 1.0 + cfg::CONTENT_GAP;

    let content_x = px + pad;
    let content_w = pw - 2.0 * pad;
    let bottom = py + ph - pad;
    let config = model.config().clone();
    let alpha = model.tab_alpha();
    let active = model.active_tab();
    let mut pane = Pane::new(canvas, model, &config, alpha);
    if menu {
        menu_page(&mut pane, art, content_x, content_y, content_w);
    } else {
        tab_content(&mut pane, active, content_x, content_y, content_w, bottom);
    }
    frame.caret = pane.caret;
    canvas.set_opacity(1.0);
    frame
}
