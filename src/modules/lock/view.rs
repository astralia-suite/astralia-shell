//! Paints the lock card: battery, system, media, clock, avatar, password, resources and notifications.

use crate::config::lock_config as cfg;
use crate::modules::lock::layout::{card_height, card_width, center_scale, columns, content_height, dot_x, fetch_colour_count, panel_origin, side_card_height};
use crate::modules::lock::model::{Model, Motion};
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextSize, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons as icon;
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::text_field::text_field_row_slide;
use crate::render::theme::{Theme, palette};
use crate::render::tokens::{Color, WHITE, color, metrics, with_alpha};
use crate::service::battery::BatteryStatus;
use crate::service::media::{MediaPlayback, MediaStatus, media_is_local_art};
use crate::service::notification::Notification;
use crate::service::telemetry::{CpuTempState, GpuTempState, SystemStatsState, gpu_stats_available};

const GPU_COLOR: Color = color(cfg::RES_GAUGE_GPU_COLOR_HEX);

/// What the card shows, borrowed from the services for one paint.
#[derive(Clone, Default)]
pub struct Info<'a> {
    pub user: String,
    pub os: String,
    pub wm: String,
    pub uptime: String,
    pub hour: String,
    pub minute: String,
    pub date: String,
    pub face: String,
    pub battery: Option<&'a BatteryStatus>,
    pub media: Option<&'a MediaStatus>,
    pub stats: Option<&'a SystemStatsState>,
    pub cpu_temp: Option<&'a CpuTempState>,
    pub gpu_temp: Option<&'a GpuTempState>,
    pub notifications: Option<&'a [Notification]>,
}

struct Pane<'p, 'i> {
    canvas: &'p mut dyn Canvas,
    model: &'p mut Model,
    motion: &'p mut Motion,
    info: &'p Info<'i>,
    p: Theme,
    origin_x: f32,
    origin_y: f32,
}

fn text_style(px: i32, bold: bool, max_width: i32) -> TextStyle {
    TextStyle { family: FontFamily::Text, px, bold, max_width, wrap: false }
}

fn icon_style(px: i32) -> TextStyle {
    TextStyle { family: FontFamily::Icon, px, ..TextStyle::default() }
}

fn put(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, x: f32, y: f32, color: Color) -> TextSize {
    let size = canvas.measure(text, style);
    canvas.text(text, style, x.round(), y.round(), color);
    size
}

fn put_centered(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, cx: f32, y: f32, color: Color) -> TextSize {
    let size = canvas.measure(text, style);
    canvas.text(text, style, (cx - size.w * 0.5).round(), y.round(), color);
    size
}

fn rect(x: f32, y: f32, w: f32, h: f32) -> Rect {
    Rect { x, y, w, h }
}

impl Pane<'_, '_> {
    fn absolute(&self, r: Rect) -> Rect {
        rect(r.x + self.origin_x, r.y + self.origin_y, r.w, r.h)
    }
}

struct CardBody {
    x: f32,
    y: f32,
    w: f32,
}

fn draw_card(canvas: &mut dyn Canvas, p: &Theme, col: Rect, title: &str) -> CardBody {
    sticker(canvas, col, metrics::RADIUS_CARD, p.surface, &StickerStyle::default());
    let x = col.x + cfg::SIDE_PANEL_PAD;
    let y = col.y + cfg::SIDE_PANEL_PAD;
    let w = col.w - 2.0 * cfg::SIDE_PANEL_PAD;
    if title.is_empty() {
        return CardBody { x, y, w };
    }
    let style = text_style(cfg::FONT_NORMAL as i32, true, 0);
    let measured = canvas.measure(title, &style);
    let tag = StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() };
    sticker(
        canvas,
        rect(x - cfg::CARD_TAG_PAD, y - cfg::CARD_TAG_PAD / 2.0, measured.w + 2.0 * cfg::CARD_TAG_PAD, measured.h + cfg::CARD_TAG_PAD + metrics::SHADOW_WIDGET),
        metrics::RADIUS_WIDGET,
        p.pop_a,
        &tag,
    );
    let size = put(canvas, title, &style, x, y, p.text);
    CardBody { x, y: y + size.h + cfg::CARD_HEADER_GAP + cfg::CARD_TAG_PAD, w }
}

fn draw_pill(pane: &mut Pane, x: f32, y: f32, w: f32) {
    let p = pane.p;
    let h = cfg::INPUT_HEIGHT;
    let r = h * 0.5;
    let chrome = StickerStyle { outline: p.border_outer, border_width: cfg::BG_BORDER_WIDTH, shadow: 0.0, ..StickerStyle::default() };
    sticker(pane.canvas, rect(x, y, w, h), metrics::RADIUS_CARD, p.pop_b, &chrome);

    let cy = y + h * 0.5;
    let icon_s = icon_style(cfg::PILL_ICON_SIZE as i32);
    let lock_size = pane.canvas.measure(icon::LOCK, &icon_s);
    pane.canvas.text(icon::LOCK, &icon_s, (x + r - lock_size.w * 0.5).round(), (cy - lock_size.h * 0.5).round(), p.text_muted);

    let has_text = !pane.model.password().is_empty();
    let btn = cfg::PILL_BUTTON_SIZE;
    let btn_x = x + w - btn - (h - btn) * 0.5;
    let btn_y = cy - btn * 0.5;
    selectable(pane.canvas, rect(btn_x, btn_y, btn, btn), metrics::RADIUS_WIDGET, has_text, false, p.surface_alt, 1.0);
    let arrow = pane.canvas.measure(icon::ARROW_RIGHT, &icon_s);
    pane.canvas.text(icon::ARROW_RIGHT, &icon_s, (btn_x + (btn - arrow.w) * 0.5).round(), (btn_y + (btn - arrow.h) * 0.5).round(), if has_text { p.text } else { p.text_muted });
    pane.model.hits().pill_button = pane.absolute(rect(btn_x, btn_y, btn, btn));

    let mut mid_x = x + h;
    let mut mid_w = w - 2.0 * h;
    if mid_w < 10.0 {
        mid_x = x + cfg::PILL_PAD;
        mid_w = w - 2.0 * cfg::PILL_PAD;
    }
    let normal = text_style(cfg::FONT_NORMAL as i32, false, 0);
    let motion = &mut *pane.motion;
    if !has_text {
        let model = &*pane.model;
        let message = if model.failed() {
            cfg::FAIL_TEXT
        } else if model.authenticating() {
            cfg::LOADING_TEXT
        } else {
            cfg::PLACEHOLDER_TEXT
        };
        let tone = if model.failed() { p.critical } else { p.text_muted };
        let size = pane.canvas.measure(message, &normal);
        pane.canvas.text(message, &normal, (mid_x + (mid_w - size.w) * 0.5).round(), (cy - size.h * 0.5).round(), tone);
        text_field_row_slide(&mut motion.row, &mut motion.animations, cfg::OWNER_DOT_ROW_X, mid_x + dot_x(0, 0, mid_w));
        return;
    }
    let length = pane.model.password().chars().count();
    let capacity = ((mid_w / cfg::DOT_SIZE) as usize).max(1);
    let visible = length.min(capacity) as i32;
    let row_x = text_field_row_slide(&mut motion.row, &mut motion.animations, cfg::OWNER_DOT_ROW_X, mid_x + dot_x(0, visible, mid_w));
    let echo = pane.canvas.image(cfg::ECHO_ASSET, cfg::DOT_SIZE as i32);
    for i in 0..visible as usize {
        let anim = motion.dots.chars.get(i);
        let size = cfg::DOT_SIZE * anim.map_or(1.0, |a| a.scale.get());
        let dx = row_x + i as f32 * cfg::DOT_SIZE + anim.map_or(0.0, |a| a.slide_x.get());
        let r = rect((dx + (cfg::DOT_SIZE - size) * 0.5).round(), (cy - size * 0.5).round(), size, size);
        if echo != NO_IMAGE {
            pane.canvas.draw_image(echo, r, WHITE);
        } else {
            pane.canvas.rounded(r, size * 0.5, p.text, 0.0, Color::default());
        }
    }
}

fn battery_glyph(status: &BatteryStatus) -> &'static str {
    if status.full {
        icon::PLUGGED_IN
    } else if status.charging {
        icon::BATTERY_CHARGING
    } else if status.percent <= 25 {
        icon::BATTERY1
    } else if status.percent <= 50 {
        icon::BATTERY2
    } else if status.percent <= 75 {
        icon::BATTERY3
    } else {
        icon::BATTERY4
    }
}

fn draw_battery(pane: &mut Pane, col: Rect) -> f32 {
    let Some(status) = pane.info.battery.filter(|s| s.present) else { return 0.0 };
    let p = &pane.p;
    let canvas = &mut *pane.canvas;
    let px = cfg::FONT_NORMAL as i32;
    let title = canvas.measure("Battery", &text_style(px, true, 0));
    let glyph = battery_glyph(status);
    let icon_size = canvas.measure(glyph, &icon_style(px));
    let label = if status.full { "Plugged in".to_string() } else { format!("{}%", status.percent) };
    let line = format!("Battery  {label}");
    let label_size = canvas.measure(&line, &text_style(px, false, 0));
    let row_h = icon_size.h.max(label_size.h);
    let card_h = cfg::SIDE_PANEL_PAD + title.h + cfg::CARD_HEADER_GAP + row_h + cfg::BATTERY_ROW_GAP + cfg::BATTERY_BAR_HEIGHT + cfg::SIDE_PANEL_PAD;

    let body = draw_card(canvas, p, rect(col.x, col.y, col.w, card_h), "Battery");
    let mut rx = body.x;
    canvas.text(glyph, &icon_style(px), rx.round(), (body.y + (row_h - icon_size.h) * 0.5).round(), p.text);
    rx += icon_size.w + cfg::BATTERY_ICON_GAP;
    canvas.text(&line, &text_style(px, false, 0), rx.round(), (body.y + (row_h - label_size.h) * 0.5).round(), p.text);
    let bar_y = body.y + row_h + cfg::BATTERY_ROW_GAP;
    canvas.rounded(rect(body.x.round(), bar_y.round(), body.w, cfg::BATTERY_BAR_HEIGHT), cfg::BATTERY_BAR_RADIUS, p.text_alpha11, 0.0, Color::default());
    let fill = body.w * (status.percent as f32 / 100.0).clamp(0.0, 1.0);
    if fill > 0.0 {
        canvas.rounded(rect(body.x.round(), bar_y.round(), fill, cfg::BATTERY_BAR_HEIGHT), cfg::BATTERY_BAR_RADIUS, p.accent, 0.0, Color::default());
    }
    card_h
}

fn draw_fetch(pane: &mut Pane, col: Rect) {
    let p = &pane.p;
    let info = pane.info;
    let canvas = &mut *pane.canvas;
    let px = cfg::FONT_MONO as i32;
    let inner_w = col.w - 2.0 * cfg::SIDE_PANEL_PAD;
    let fit = text_style(px, false, inner_w as i32);
    let body = draw_card(canvas, p, col, "System");
    let x = body.x;
    let mut y = body.y;

    let bold = text_style(px, true, 0);
    let prompt = canvas.measure(">", &bold);
    let name = canvas.measure(&info.user, &bold);
    let head_h = prompt.h.max(name.h);
    let mut hx = x;
    canvas.text(">", &bold, hx.round(), (y + (head_h - prompt.h) * 0.5).round(), p.lavender);
    hx += prompt.w + cfg::FETCH_CHIP_PAD;
    canvas.text(&info.user, &bold, hx.round(), (y + (head_h - name.h) * 0.5).round(), p.text);
    y += head_h + cfg::FETCH_LINE_GAP;

    for line in [format!("OS  : {}", info.os), format!("WM  : {}", info.wm), info.uptime.clone()] {
        let size = canvas.measure(&line, &fit);
        canvas.text(&line, &fit, x.round(), y.round(), p.text_muted);
        y += size.h.max(1.0) + cfg::FETCH_LINE_GAP;
    }

    let terminal = [p.accent, p.accent_container, p.accent_alt, p.accent_alt_container, p.electro, p.lavender, p.critical, p.text, p.text_muted, p.surface_alt, p.field_bg, p.base];
    let per_row = fetch_colour_count(inner_w, 6);
    let rows = if per_row > 0 { 2 } else { 0 };
    let total = (per_row * rows).min(12);
    let mut drawn = 0;
    for _ in 0..rows {
        let in_row = per_row.min(total - drawn);
        let mut bx = x;
        for _ in 0..in_row {
            canvas.rounded(rect(bx, y, cfg::FETCH_COLOR_BOX, cfg::FETCH_COLOR_BOX), metrics::RADIUS_WIDGET, terminal[drawn as usize], 0.0, Color::default());
            bx += cfg::FETCH_COLOR_BOX + cfg::FETCH_COLOR_GAP;
            drawn += 1;
        }
        y += cfg::FETCH_COLOR_BOX + cfg::FETCH_COLOR_GAP;
    }
}

fn draw_media(pane: &mut Pane, col: Rect) {
    {
        let hits = pane.model.hits();
        hits.media_prev = Rect::default();
        hits.media_play = Rect::default();
        hits.media_next = Rect::default();
    }
    let pad = cfg::SIDE_PANEL_PAD;
    let Some(media) = pane.info.media else { return };
    if col.h < cfg::MEDIA_ART + 2.0 * pad {
        return;
    }
    let p = pane.p;
    let body = draw_card(pane.canvas, &p, col, "Media");

    let cx = col.x + col.w * 0.5;
    let art = cfg::MEDIA_ART;
    let bs = cfg::MEDIA_BTN_SIZE;
    let fit = (col.w - 2.0 * pad) as i32;
    let title = if media.has_player && !media.track.title.is_empty() { media.track.title.as_str() } else { "Nothing playing" };
    let artist = if media.has_player && !media.track.artist.is_empty() { media.track.artist.as_str() } else { "Try playing some music" };
    let title_style = text_style(cfg::FONT_NORMAL as i32, true, fit);
    let artist_style = text_style(cfg::FONT_MONO as i32, false, fit);
    let canvas = &mut *pane.canvas;
    let title_size = canvas.measure(title, &title_style);
    let artist_size = canvas.measure(artist, &artist_style);

    let block_h = art + cfg::MEDIA_TEXT_GAP * 3.0 + title_size.h + cfg::MEDIA_TEXT_GAP + artist_size.h + cfg::MEDIA_BTN_GAP * 1.5 + bs;
    let avail_h = col.y + col.h - pad - body.y;
    let ay = body.y + ((avail_h - block_h) * 0.5).max(0.0);
    let art_box = rect(cx - art * 0.5, ay, art, art);

    canvas.rounded(art_box, metrics::RADIUS_WIDGET, p.field_bg, 0.0, Color::default());
    let mut cover = NO_IMAGE;
    if media.has_player && media_is_local_art(&media.track.art_url) {
        cover = canvas.thumbnail(&media.track.art_url[7..], art as i32);
    }
    if cover != NO_IMAGE {
        canvas.draw_image(cover, art_box, WHITE);
    } else {
        let note_style = icon_style((art * 0.4) as i32);
        let note = canvas.measure(icon::MUSIC_NOTE, &note_style);
        canvas.text(icon::MUSIC_NOTE, &note_style, (cx - note.w * 0.5).round(), (ay + (art - note.h) * 0.5).round(), p.text_dim);
    }

    let mut ty = ay + art + cfg::MEDIA_TEXT_GAP * 3.0;
    put_centered(canvas, title, &title_style, cx, ty, p.lavender);
    ty += title_size.h + cfg::MEDIA_TEXT_GAP;
    put_centered(canvas, artist, &artist_style, cx, ty, p.text_muted);
    ty += artist_size.h + cfg::MEDIA_BTN_GAP * 1.5;

    if ty + bs > col.y + col.h - pad {
        return;
    }
    let row_w = 3.0 * bs + 2.0 * cfg::MEDIA_BTN_GAP;
    let bx = cx - row_w * 0.5;
    let play = if media.playback == MediaPlayback::Playing { icon::PLAYER_PAUSE } else { icon::PLAYER_PLAY };
    let mut out = [Rect::default(); 3];
    for (k, (rx, glyph)) in [(bx, icon::PLAYER_PREV), (bx + bs + cfg::MEDIA_BTN_GAP, play), (bx + 2.0 * (bs + cfg::MEDIA_BTN_GAP), icon::PLAYER_NEXT)].into_iter().enumerate() {
        canvas.rounded(rect(rx, ty, bs, bs), bs * 0.5, p.field_bg, 0.0, Color::default());
        let style = icon_style((bs * 0.5) as i32);
        let size = canvas.measure(glyph, &style);
        canvas.text(glyph, &style, (rx + (bs - size.w) * 0.5).round(), (ty + (bs - size.h) * 0.5).round(), p.text);
        out[k] = rect(rx, ty, bs, bs);
    }
    let [prev, play, next] = out.map(|r| pane.absolute(r));
    let hits = pane.model.hits();
    hits.media_prev = prev;
    hits.media_play = play;
    hits.media_next = next;
}

struct Tile {
    glyph: &'static str,
    label: String,
    frac: f32,
    percent: i32,
    accent: Color,
    label_color: Color,
    row: usize,
}

fn temp_label(base: &str, celsius: f32, p: &Theme) -> (String, Color) {
    if celsius <= 0.0 {
        return (base.to_string(), p.text_dim);
    }
    let tone = if celsius >= cfg::RES_TEMP_WARN_C { p.critical } else { p.text_dim };
    (format!("{base} - {}\u{b0}C", celsius.round() as i32), tone)
}

fn percent_of(frac: f32) -> i32 {
    if frac >= 0.0 { (frac * 100.0).round() as i32 } else { -1 }
}

fn draw_resources(pane: &mut Pane, col: Rect) {
    let gap = cfg::RES_TILE_GAP;
    let p = &pane.p;
    let canvas = &mut *pane.canvas;
    let body = draw_card(canvas, p, col, "Resources");
    let avail_h = col.y + col.h - cfg::SIDE_PANEL_PAD - body.y;
    let (Some(stats), Some(cpu), Some(gpu)) = (pane.info.stats, pane.info.cpu_temp, pane.info.gpu_temp) else { return };

    let mut tiles = Vec::with_capacity(4);
    let (cpu_label, cpu_tone) = temp_label("CPU", cpu.celsius, p);
    tiles.push(Tile { glyph: icon::CPU, label: cpu_label, frac: stats.cpu_usage.max(0.0), percent: percent_of(stats.cpu_usage), accent: p.accent, label_color: cpu_tone, row: 0 });
    if gpu_stats_available(gpu) {
        let (gpu_label, gpu_tone) = temp_label("GPU", gpu.celsius, p);
        tiles.push(Tile { glyph: icon::GPU, label: gpu_label, frac: (gpu.usage_percent / 100.0).clamp(0.0, 1.0), percent: gpu.usage_percent.round() as i32, accent: GPU_COLOR, label_color: gpu_tone, row: 0 });
    }
    tiles.push(Tile { glyph: icon::DEVICE_DESKTOP, label: "RAM".into(), frac: stats.mem_usage.max(0.0), percent: percent_of(stats.mem_usage), accent: p.lavender, label_color: p.text_dim, row: 1 });
    tiles.push(Tile {
        glyph: icon::FOLDER,
        label: "DISK".into(),
        frac: (stats.disk_pct / 100.0).max(0.0),
        percent: if stats.disk_pct >= 0.0 { stats.disk_pct.round() as i32 } else { -1 },
        accent: p.accent_alt,
        label_color: p.text_dim,
        row: 1,
    });

    let label_style = text_style(cfg::FONT_MONO as i32, false, 0);
    let cell_extra = canvas.measure("CPU", &label_style).h + cfg::RES_GAUGE_LABEL_GAP;
    let tile = ((col.w - 3.0 * gap) * 0.5).min((avail_h - gap - 2.0 * cell_extra) * 0.5);
    let stroke = tile * cfg::RES_GAUGE_STROKE_RATIO;
    let col_gap = (col.w - 2.0 * tile) / 3.0;
    let block_h = 2.0 * tile + gap + 2.0 * cell_extra;
    let y0 = body.y + ((avail_h - block_h) * 0.5).max(0.0);
    let row_y = [y0, y0 + tile + cell_extra + gap];

    let mut row_count = [0; 2];
    for t in &tiles {
        row_count[t.row] += 1;
    }
    let mut row_seen = [0; 2];
    let icon_s = icon_style(cfg::RES_ICON_SIZE as i32);
    let value_style = text_style(cfg::RES_VALUE_FONT as i32, true, 0);
    for t in &tiles {
        let within = row_seen[t.row];
        row_seen[t.row] += 1;
        let tx = if row_count[t.row] == 1 {
            col.x + (col.w - tile) * 0.5
        } else if within == 0 {
            col.x + col_gap
        } else {
            col.x + col.w - col_gap - tile
        };
        let ty = row_y[t.row];
        canvas.gauge(rect(tx.round(), ty.round(), tile, tile), stroke, t.frac.clamp(0.0, 1.0), t.accent);

        let value = if t.percent >= 0 { format!("{}%", t.percent) } else { "--".to_string() };
        let icon_size = canvas.measure(t.glyph, &icon_s);
        let value_size = canvas.measure(&value, &value_style);
        let mut stack_y = ty + (tile - icon_size.h - cfg::RES_GAUGE_ICON_VALUE_GAP - value_size.h) * 0.5;
        canvas.text(t.glyph, &icon_s, (tx + (tile - icon_size.w) * 0.5).round(), stack_y.round(), t.accent);
        stack_y += icon_size.h + cfg::RES_GAUGE_ICON_VALUE_GAP;
        canvas.text(&value, &value_style, (tx + (tile - value_size.w) * 0.5).round(), stack_y.round(), p.text);
        put_centered(canvas, &t.label, &label_style, tx + tile * 0.5, ty + tile + cfg::RES_GAUGE_LABEL_GAP, t.label_color);
    }
}

fn draw_notifications(pane: &mut Pane, col: Rect) {
    let Some(list) = pane.info.notifications else { return };
    if col.h < 60.0 {
        return;
    }
    let p = &pane.p;
    let canvas = &mut *pane.canvas;
    let header = match list.len() {
        0 => "Notifications".to_string(),
        1 => "1 notification".to_string(),
        n => format!("{n} notifications"),
    };
    let body = draw_card(canvas, p, col, &header);

    if list.is_empty() {
        let style = text_style(cfg::FONT_NORMAL as i32, false, 0);
        let size = canvas.measure("No Notifications", &style);
        canvas.text("No Notifications", &style, (col.x + (col.w - size.w) * 0.5).round(), (col.y + col.h * 0.5).round(), p.text_dim);
        return;
    }

    let clip_h = col.y + col.h - body.y - cfg::SIDE_PANEL_PAD;
    let cw = col.w - 2.0 * cfg::SIDE_PANEL_PAD;
    let pad = cfg::NOTIF_CARD_PAD;
    let fit = (cw - 2.0 * pad) as i32;
    canvas.begin_group(rect(body.x, body.y, body.w, clip_h), GroupOptions { clip: true, ..GroupOptions::default() });
    let mut cy = 0.0;
    let app_style = text_style(cfg::FONT_MONO as i32, true, fit);
    let summary_style = text_style(cfg::FONT_NORMAL as i32, false, fit);
    let body_style = text_style(cfg::FONT_MONO as i32, false, fit);
    for (shown, record) in list.iter().rev().enumerate() {
        if shown as i32 >= cfg::NOTIF_MAX_CARDS || cy >= clip_h {
            break;
        }
        let app = if record.app.is_empty() { "Notification" } else { record.app.as_str() };
        let app_size = canvas.measure(app, &app_style);
        let summary_size = if record.summary.is_empty() { TextSize::default() } else { canvas.measure(&record.summary, &summary_style) };
        let body_size = if record.body.is_empty() { TextSize::default() } else { canvas.measure(&record.body, &body_style) };
        let ch = 2.0 * pad
            + app_size.h
            + if record.summary.is_empty() { 0.0 } else { cfg::MEDIA_TEXT_GAP + summary_size.h }
            + if record.body.is_empty() { 0.0 } else { cfg::MEDIA_TEXT_GAP + body_size.h };

        canvas.rounded(rect(0.0, cy, cw, ch), metrics::RADIUS_CARD, p.field_bg, 0.0, Color::default());
        let mut iy = cy + pad;
        canvas.text(app, &app_style, pad, iy.round(), p.lavender);
        iy += app_size.h + cfg::MEDIA_TEXT_GAP;
        if !record.summary.is_empty() {
            canvas.text(&record.summary, &summary_style, pad, iy.round(), p.text);
            iy += summary_size.h + cfg::MEDIA_TEXT_GAP;
        }
        if !record.body.is_empty() {
            canvas.text(&record.body, &body_style, pad, iy.round(), p.text_muted);
        }
        cy += ch + cfg::NOTIF_CARD_GAP;
    }
    canvas.end_group();
}

fn draw_center(pane: &mut Pane, col: Rect, output_h: f32) {
    let p = pane.p;
    let info = pane.info;
    {
        let canvas = &mut *pane.canvas;
        draw_card(canvas, &p, col, "");

        let scale = center_scale(output_h);
        let clock_px = ((cfg::FONT_CLOCK * scale) as i32).max(1);
        let clock_style = text_style(clock_px, true, 0);
        let hour = canvas.measure(&info.hour, &clock_style);
        let colon = canvas.measure(":", &clock_style);
        let minute = canvas.measure(&info.minute, &clock_style);
        let date_style = text_style(cfg::FONT_DATE as i32, true, 0);
        let date = canvas.measure(&info.date, &date_style);

        let clock_h = hour.h.max(minute.h);
        let clock_w = hour.w + cfg::CLOCK_GAP + colon.w + cfg::CLOCK_GAP + minute.w;
        let total_h = content_height(clock_h, date.h, 0.0);
        let mut y = col.y + (col.h - total_h) * 0.5;
        let cx = col.x + col.w * 0.5;

        let mut mx = cx - clock_w * 0.5;
        canvas.text(&info.hour, &clock_style, mx.round(), y.round(), p.text);
        mx += hour.w + cfg::CLOCK_GAP;
        canvas.text(":", &clock_style, mx.round(), y.round(), p.text);
        mx += colon.w + cfg::CLOCK_GAP;
        canvas.text(&info.minute, &clock_style, mx.round(), y.round(), p.lavender);
        y += clock_h + cfg::GAP_CLOCK_DATE;

        put_centered(canvas, &info.date, &date_style, cx, y, p.text);
        y += date.h + cfg::GAP_DATE_AVATAR;

        let ps = cfg::PROFILE_SIZE;
        let avatar = rect((cx - ps * 0.5).round(), y.round(), ps, ps);
        canvas.rounded(avatar, ps * 0.5, p.field_bg, 0.0, Color::default());
        let face = if info.face.is_empty() { NO_IMAGE } else { canvas.image(&info.face, ps as i32 * 2) };
        if face != NO_IMAGE {
            canvas.begin_group(avatar, GroupOptions { clip: true, radius: ps * 0.5, ..GroupOptions::default() });
            canvas.draw_image(face, rect(0.0, 0.0, avatar.w, avatar.h), WHITE);
            canvas.end_group();
        } else {
            let glyph_style = icon_style((ps * 0.5) as i32);
            let glyph = canvas.measure(icon::USER, &glyph_style);
            canvas.text(icon::USER, &glyph_style, (avatar.x + (avatar.w - glyph.w) * 0.5).round(), (avatar.y + (avatar.h - glyph.h) * 0.5).round(), p.text);
        }
        let ring = StickerStyle { outline: p.border_outer, border_width: cfg::PROFILE_BORDER_WIDTH, shadow: 0.0, ..StickerStyle::default() };
        sticker(canvas, avatar, ps * 0.5, Color::default(), &ring);
        y += ps + cfg::GAP_AVATAR_INPUT;

        let pill_w = col.w * cfg::INPUT_WIDTH_FRAC;
        draw_pill(pane, cx - pill_w * 0.5, y, pill_w);
    }
}

/// Paints the card centred on a `width` x `height` output and refills `model.hits()`.
pub fn paint(canvas: &mut dyn Canvas, model: &mut Model, motion: &mut Motion, info: &Info, width: f32, height: f32) {
    let p = palette();
    let card_w = card_width(height);
    let card_h = card_height(height);
    let pw = if motion.panel_w > 0.0 { motion.panel_w } else { card_w };
    let ph = if motion.panel_h > 0.0 { motion.panel_h } else { card_h };
    let (px, py) = panel_origin(width, height, pw, ph);
    let (px, py) = (px.round(), py.round());

    *model.hits() = Default::default();
    canvas.begin_group(rect(px, py, pw, ph), GroupOptions { scale: motion.panel_scale, clip: true, radius: metrics::RADIUS_CARD, rotation: motion.panel_rotation });
    sticker(canvas, rect(0.0, 0.0, pw, ph), metrics::RADIUS_CARD, p.paper, &StickerStyle::default());

    if motion.icon_alpha > 0.001 {
        let style = icon_style(cfg::FONT_ICON as i32);
        let size = canvas.measure(icon::LOCK, &style);
        canvas.text(icon::LOCK, &style, ((pw - size.w) * 0.5).round(), ((ph - size.h) * 0.5).round(), with_alpha(p.text, p.text.a * motion.icon_alpha));
    }
    if motion.content_alpha <= 0.001 {
        canvas.end_group();
        return;
    }

    let center_w = cfg::CENTER_WIDTH * center_scale(height);
    let (left, center, right) = columns(card_w, card_h, center_w);

    let cx0 = (pw - card_w) * 0.5;
    let cy0 = (ph - card_h) * 0.5;
    canvas.set_opacity(motion.content_alpha);
    canvas.begin_group(rect(cx0, cy0, card_w, card_h), GroupOptions { scale: motion.content_scale, ..GroupOptions::default() });
    let content_scale = motion.content_scale;
    let mut pane = Pane { canvas, model, motion, info, p, origin_x: px + cx0, origin_y: py + cy0 };

    let bat_h = draw_battery(&mut pane, left);
    let top = left.y + if bat_h > 0.0 { bat_h + cfg::PANEL_GAP } else { 0.0 };
    let rem = left.h - (top - left.y);
    let ch = ((rem - cfg::PANEL_GAP) * 0.5).max(0.0);
    draw_fetch(&mut pane, rect(left.x, top, left.w, ch));
    draw_media(&mut pane, rect(left.x, top + ch + cfg::PANEL_GAP, left.w, ch));

    let side_h = side_card_height(right.h);
    draw_resources(&mut pane, rect(right.x, right.y, right.w, side_h));
    draw_notifications(&mut pane, rect(right.x, right.y + side_h + cfg::PANEL_GAP, right.w, side_h));

    draw_center(&mut pane, center, height);
    let Pane { canvas, model, .. } = pane;
    canvas.end_group();
    canvas.set_opacity(1.0);
    canvas.end_group();
    if content_scale < 0.99 {
        *model.hits() = Default::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::{KeyEvent, KeyKind};
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    fn info<'a>(stats: &'a SystemStatsState, cpu: &'a CpuTempState, gpu: &'a GpuTempState, battery: &'a BatteryStatus) -> Info<'a> {
        Info {
            user: "user".into(),
            hour: "12".into(),
            minute: "34".into(),
            date: "FRI 2026-10-09".into(),
            battery: Some(battery),
            stats: Some(stats),
            cpu_temp: Some(cpu),
            gpu_temp: Some(gpu),
            notifications: Some(&[]),
            ..Info::default()
        }
    }

    fn texts(canvas: &RecordingCanvas) -> Vec<&str> {
        canvas.ops.iter().filter(|o| o.kind == OpKind::Text).map(|o| o.text.as_str()).collect()
    }

    fn index_of(list: &[&str], value: &str) -> usize {
        list.iter().position(|t| *t == value).unwrap_or_else(|| panic!("{value} drawn"))
    }

    #[test]
    fn lock_view_ops() {
        let (stats, cpu, gpu) = (SystemStatsState::default(), CpuTempState::default(), GpuTempState::default());
        let battery = BatteryStatus { present: true, percent: 80, ..Default::default() };
        let info = info(&stats, &cpu, &gpu, &battery);
        for (w, h) in [(2560.0, 1440.0), (1920.0, 1080.0)] {
            let mut model = Model::default();
            let mut motion = Motion::default();
            let mut canvas = RecordingCanvas::default();
            paint(&mut canvas, &mut model, &mut motion, &info, w, h);
            assert_eq!(canvas.ops[0].kind, OpKind::BeginGroup, "the card group opens first");
            assert_eq!(canvas.ops.last().unwrap().kind, OpKind::EndGroup, "the card group closes last");
            let t = texts(&canvas);
            let order = ["Battery", "System", "Resources", "Notifications", "12", "34", "FRI 2026-10-09", cfg::PLACEHOLDER_TEXT];
            for pair in order.windows(2) {
                assert!(index_of(&t, pair[0]) < index_of(&t, pair[1]), "{} before {}", pair[0], pair[1]);
            }
            assert!(model.hits().pill_button.w > 0.0, "the view reports the submit button");
            let hits = *model.hits();
            assert!(hits.media_prev.w == 0.0 && hits.media_play.w == 0.0 && hits.media_next.w == 0.0, "no media card, no media hits");
        }

        let media = MediaStatus::default();
        let with_media = Info { media: Some(&media), ..info };
        let mut model = Model::default();
        let mut motion = Motion::default();
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &mut model, &mut motion, &with_media, 2560.0, 1440.0);
        let t = texts(&canvas);
        assert!(index_of(&t, "System") < index_of(&t, "Media") && index_of(&t, "Media") < index_of(&t, "Resources"), "media sits under system");
        assert!(t.contains(&"Nothing playing") && model.hits().media_play.w > 0.0, "the media card reports its buttons");
    }

    #[test]
    fn lock_view_failed() {
        let (stats, cpu, gpu) = (SystemStatsState::default(), CpuTempState::default(), GpuTempState::default());
        let battery = BatteryStatus::default();
        let info = info(&stats, &cpu, &gpu, &battery);
        let mut model = Model::default();
        let mut motion = Motion::default();
        model.key(&KeyEvent { kind: KeyKind::Text, text: "z".into(), ..Default::default() });
        model.begin_auth();
        model.finish_auth(model.generation(), false);
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &mut model, &mut motion, &info, 2560.0, 1440.0);
        let fail = canvas.ops.iter().find(|o| o.kind == OpKind::Text && o.text == cfg::FAIL_TEXT).expect("the view shows the failure text");
        assert_eq!(fail.color, palette().critical, "in the critical role");
        assert!(!texts(&canvas).contains(&"Battery"), "no battery card without a battery");
    }
}
