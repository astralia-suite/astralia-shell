#include <algorithm>
#include <cmath>
#include <string>

#include "config/lock_config.h"
#include "render/icons.h"

#include "modules/lock/layout.h"
#include "modules/lock/view.h"

#include "render/tokens.h"

namespace astralia {

namespace {

constexpr Color gpu_color = color(kLockResGaugeGpuColorHex);

struct Pane {
    ui::Canvas &canvas;
    LockModel &model;
    const LockInfo &info;
    float origin_x;
    float origin_y;
};

ui::TextStyle text_style(int px, bool bold = false, int max_width = 0) {
    return {ui::FontFamily::text, px, bold, max_width, false};
}

ui::TextStyle icon_style(int px) {
    return {ui::FontFamily::icon, px};
}

ui::TextSize put(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x, float y, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, std::round(x), std::round(y), color);
    return size;
}

ui::TextSize put_centered(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float cx, float y, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, std::round(cx - size.w * 0.5f), std::round(y), color);
    return size;
}

ui::Box absolute(const Pane &pane, const ui::Box &box) {
    return {box.x + pane.origin_x, box.y + pane.origin_y, box.w, box.h};
}

size_t utf8_len(const std::string &text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

struct CardBody {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

CardBody draw_card(ui::Canvas &canvas, const LockRect &col, const std::string &title) {
    canvas.rounded({col.x, col.y, col.w, col.h}, kLockSidePanelRadius, palette::overlay, kLockCardBorderWidth, palette::accent);
    float x = col.x + kLockSidePanelPad;
    float y = col.y + kLockSidePanelPad;
    float w = col.w - 2.0f * kLockSidePanelPad;
    if (title.empty()) {
        return {x, y, w};
    }
    ui::TextSize size = put(canvas, title, text_style(static_cast<int>(kLockFontNormal), true), x, y, palette::text);
    return {x, y + size.h + kLockCardHeaderGap, w};
}

void draw_pill(Pane &pane, float x, float y, float w) {
    ui::Canvas &canvas = pane.canvas;
    const LockModel &model = pane.model;
    float h = kLockInputHeight;
    float r = h * 0.5f;
    canvas.rounded({x, y, w, h}, r, palette::field_bg, kLockBgBorderWidth, palette::accent);

    float cy = y + h * 0.5f;
    ui::TextStyle icon = icon_style(static_cast<int>(kLockPillIconSize));
    ui::TextSize lock_size = canvas.measure(icon::lock, icon);
    canvas.text(icon::lock, icon, std::round(x + r - lock_size.w * 0.5f), std::round(cy - lock_size.h * 0.5f), palette::text_muted);

    bool has_text = !model.password().empty();
    float btn = kLockPillButtonSize;
    float btn_x = x + w - btn - (h - btn) * 0.5f;
    float btn_y = cy - btn * 0.5f;
    canvas.rounded({btn_x, btn_y, btn, btn}, btn * 0.5f, has_text ? palette::accent : palette::surface_alt);
    ui::TextSize arrow = canvas.measure(icon::arrow_right, icon);
    canvas.text(icon::arrow_right, icon, std::round(btn_x + (btn - arrow.w) * 0.5f), std::round(btn_y + (btn - arrow.h) * 0.5f), has_text ? palette::base : palette::text_muted);
    pane.model.hits().pill_button = absolute(pane, {btn_x, btn_y, btn, btn});

    float mid_x = x + h;
    float mid_w = w - 2.0f * h;
    if (mid_w < 10.0f) {
        mid_x = x + kLockPillPad;
        mid_w = w - 2.0f * kLockPillPad;
    }
    ui::TextStyle normal = text_style(static_cast<int>(kLockFontNormal));
    if (!has_text) {
        const char *message = model.failed() ? kLockFailText : (model.authenticating() ? kLockLoadingText : kLockPlaceholderText);
        const Color &tone = model.failed() ? palette::critical : palette::text_muted;
        ui::TextSize size = canvas.measure(message, normal);
        canvas.text(message, normal, std::round(mid_x + (mid_w - size.w) * 0.5f), std::round(cy - size.h * 0.5f), tone);
        return;
    }
    size_t length = utf8_len(model.password());
    size_t capacity = std::max<size_t>(1, static_cast<size_t>(mid_w / kLockDotSize));
    int visible = static_cast<int>(std::min(length, capacity));
    float row_x = mid_x + lock_dot_x(0, visible, mid_w);
    ui::ImageId echo = canvas.image(kLockEchoAsset, static_cast<int>(kLockDotSize));
    for (int i = 0; i < visible; ++i) {
        ui::Box box{std::round(row_x + static_cast<float>(i) * kLockDotSize), std::round(cy - kLockDotSize * 0.5f), kLockDotSize, kLockDotSize};
        if (echo != ui::no_image) {
            canvas.draw_image(echo, box, palette::text);
        } else {
            canvas.rounded(box, kLockDotSize * 0.5f, palette::text);
        }
    }
}

const char *battery_glyph(const BatteryStatus &status) {
    if (status.full) {
        return icon::plugged_in;
    }
    if (status.charging) {
        return icon::battery_charging;
    }
    if (status.percent <= 25) {
        return icon::battery1;
    }
    if (status.percent <= 50) {
        return icon::battery2;
    }
    if (status.percent <= 75) {
        return icon::battery3;
    }
    return icon::battery4;
}

float draw_battery(Pane &pane, const LockRect &col) {
    const BatteryStatus *status = pane.info.battery;
    if (status == nullptr || !status->present) {
        return 0.0f;
    }
    ui::Canvas &canvas = pane.canvas;
    int px = static_cast<int>(kLockFontNormal);
    ui::TextSize title = canvas.measure("Battery", text_style(px, true));
    ui::TextSize icon = canvas.measure(battery_glyph(*status), icon_style(px));
    std::string label = status->full ? "Plugged in" : std::to_string(status->percent) + "%";
    std::string line = "Battery  " + label;
    ui::TextSize label_size = canvas.measure(line, text_style(px));
    float row_h = std::max(icon.h, label_size.h);
    float card_h = kLockSidePanelPad + title.h + kLockCardHeaderGap + row_h + kLockBatteryRowGap + kLockBatteryBarHeight + kLockSidePanelPad;

    CardBody body = draw_card(canvas, {col.x, col.y, col.w, card_h}, "Battery");
    float rx = body.x;
    canvas.text(battery_glyph(*status), icon_style(px), std::round(rx), std::round(body.y + (row_h - icon.h) * 0.5f), palette::text);
    rx += icon.w + kLockBatteryIconGap;
    canvas.text(line, text_style(px), std::round(rx), std::round(body.y + (row_h - label_size.h) * 0.5f), palette::text);
    float bar_y = body.y + row_h + kLockBatteryRowGap;
    canvas.rounded({std::round(body.x), std::round(bar_y), body.w, kLockBatteryBarHeight}, kLockBatteryBarRadius, palette::text_alpha11);
    float fill = body.w * std::clamp(status->percent / 100.0f, 0.0f, 1.0f);
    if (fill > 0.0f) {
        canvas.rounded({std::round(body.x), std::round(bar_y), fill, kLockBatteryBarHeight}, kLockBatteryBarRadius, palette::accent);
    }
    return card_h;
}

void draw_fetch(Pane &pane, const LockRect &col) {
    ui::Canvas &canvas = pane.canvas;
    int px = static_cast<int>(kLockFontMono);
    float inner_w = col.w - 2.0f * kLockSidePanelPad;
    ui::TextStyle fit = text_style(px, false, static_cast<int>(inner_w));
    CardBody body = draw_card(canvas, col, "System");
    float x = body.x;
    float y = body.y;

    ui::TextSize prompt = canvas.measure(">", text_style(px, true));
    ui::TextSize name = canvas.measure(pane.info.user, text_style(px, true));
    float head_h = std::max(prompt.h, name.h);
    float hx = x;
    canvas.text(">", text_style(px, true), std::round(hx), std::round(y + (head_h - prompt.h) * 0.5f), palette::accent);
    hx += prompt.w + kLockFetchChipPad;
    canvas.text(pane.info.user, text_style(px, true), std::round(hx), std::round(y + (head_h - name.h) * 0.5f), palette::text);
    y += head_h + kLockFetchLineGap;

    for (const std::string &line : {"OS  : " + pane.info.os, "WM  : " + pane.info.wm, pane.info.uptime}) {
        ui::TextSize size = canvas.measure(line, fit);
        canvas.text(line, fit, std::round(x), std::round(y), palette::text_muted);
        y += std::max(size.h, 1.0f) + kLockFetchLineGap;
    }

    const Color *terminal[12] = {
        &palette::accent, &palette::accent_container,
        &palette::accent_alt, &palette::accent_alt_container,
        &palette::electro, &palette::lavender,
        &palette::critical, &palette::text,
        &palette::text_muted, &palette::surface_alt,
        &palette::field_bg, &palette::base};
    int per_row = lock_fetch_colour_count(inner_w, 6);
    int rows = per_row > 0 ? 2 : 0;
    int total = std::min(12, per_row * rows);
    int drawn = 0;
    for (int r = 0; r < rows; ++r) {
        int in_row = std::min(per_row, total - drawn);
        float bx = x;
        for (int i = 0; i < in_row; ++i) {
            canvas.rounded({bx, y, kLockFetchColorBox, kLockFetchColorBox}, kLockResTileRadius * 0.4f, *terminal[drawn]);
            bx += kLockFetchColorBox + kLockFetchColorGap;
            ++drawn;
        }
        y += kLockFetchColorBox + kLockFetchColorGap;
    }
}

void draw_media(Pane &pane, const LockRect &col) {
    ui::Canvas &canvas = pane.canvas;
    LockHits &hits = pane.model.hits();
    hits.media_prev = hits.media_play = hits.media_next = {};
    float pad = kLockSidePanelPad;
    if (col.h < kLockMediaArt + 2.0f * pad || pane.info.media == nullptr) {
        return;
    }
    CardBody body = draw_card(canvas, col, "Media");

    const MediaStatus &media = *pane.info.media;
    float cx = col.x + col.w * 0.5f;
    float art = kLockMediaArt;
    float bs = kLockMediaBtnSize;
    int title_px = static_cast<int>(kLockFontNormal);
    int fit = static_cast<int>(col.w - 2.0f * pad);
    std::string title = media.has_player && !media.track.title.empty() ? media.track.title : "Nothing playing";
    std::string artist = media.has_player && !media.track.artist.empty() ? media.track.artist : "Try playing some music";
    ui::TextStyle title_style = text_style(title_px, true, fit);
    ui::TextStyle artist_style = text_style(static_cast<int>(kLockFontMono), false, fit);
    ui::TextSize title_size = canvas.measure(title, title_style);
    ui::TextSize artist_size = canvas.measure(artist, artist_style);

    float block_h = art + kLockMediaTextGap * 3.0f + title_size.h + kLockMediaTextGap + artist_size.h + kLockMediaBtnGap * 1.5f + bs;
    float avail_h = col.y + col.h - pad - body.y;
    float ay = body.y + std::max(0.0f, (avail_h - block_h) * 0.5f);
    ui::Box art_box{cx - art * 0.5f, ay, art, art};

    canvas.rounded(art_box, kLockResTileRadius, palette::field_bg);
    ui::ImageId cover = ui::no_image;
    if (media.has_player && media_is_local_art_url(media.track.art_url)) {
        cover = canvas.thumbnail(media.track.art_url.substr(7), static_cast<int>(art));
    }
    if (cover != ui::no_image) {
        canvas.draw_image(cover, art_box, palette::text);
    } else {
        ui::TextStyle note_style = icon_style(static_cast<int>(art * 0.4f));
        ui::TextSize note = canvas.measure(icon::music_note, note_style);
        canvas.text(icon::music_note, note_style, std::round(cx - note.w * 0.5f), std::round(ay + (art - note.h) * 0.5f), palette::text_dim);
    }

    float ty = ay + art + kLockMediaTextGap * 3.0f;
    put_centered(canvas, title, title_style, cx, ty, palette::accent);
    ty += title_size.h + kLockMediaTextGap;
    put_centered(canvas, artist, artist_style, cx, ty, palette::text_muted);
    ty += artist_size.h + kLockMediaBtnGap * 1.5f;

    if (ty + bs > col.y + col.h - pad) {
        return;
    }
    float row_w = 3.0f * bs + 2.0f * kLockMediaBtnGap;
    float bx = cx - row_w * 0.5f;
    auto button = [&](float rx, const char *glyph, ui::Box &out) {
        canvas.rounded({rx, ty, bs, bs}, bs * 0.5f, palette::field_bg);
        ui::TextStyle style = icon_style(static_cast<int>(bs * 0.5f));
        ui::TextSize size = canvas.measure(glyph, style);
        canvas.text(glyph, style, std::round(rx + (bs - size.w) * 0.5f), std::round(ty + (bs - size.h) * 0.5f), palette::text);
        out = absolute(pane, {rx, ty, bs, bs});
    };
    button(bx, icon::player_prev, hits.media_prev);
    button(bx + bs + kLockMediaBtnGap, media.playback == MediaPlayback::playing ? icon::player_pause : icon::player_play, hits.media_play);
    button(bx + 2.0f * (bs + kLockMediaBtnGap), icon::player_next, hits.media_next);
}

struct Tile {
    const char *glyph;
    std::string label;
    float frac;
    int percent;
    const Color *accent;
    const Color *label_color;
    int row;
};

std::string temp_label(const char *base, float celsius, const Color *&label_color) {
    if (celsius <= 0.0f) {
        return base;
    }
    label_color = celsius >= kLockResTempWarnC ? &palette::critical : &palette::text_dim;
    return std::string(base) + " - " + std::to_string(static_cast<int>(std::lround(celsius))) + "\xC2\xB0"
                                                                                                "C";
}

void draw_resources(Pane &pane, const LockRect &col) {
    ui::Canvas &canvas = pane.canvas;
    float gap = kLockResTileGap;
    CardBody body = draw_card(canvas, col, "Resources");
    float avail_h = col.y + col.h - kLockSidePanelPad - body.y;
    if (pane.info.stats == nullptr || pane.info.cpu_temp == nullptr || pane.info.gpu_temp == nullptr) {
        return;
    }
    const SystemStatsState &stats = *pane.info.stats;
    const CpuTempState &cpu = *pane.info.cpu_temp;
    const GpuTempState &gpu = *pane.info.gpu_temp;

    std::vector<Tile> tiles;
    const Color *cpu_tone = &palette::text_dim;
    std::string cpu_label = temp_label("CPU", cpu.celsius, cpu_tone);
    tiles.push_back({icon::cpu, cpu_label, std::max(0.0f, stats.cpu_usage), stats.cpu_usage >= 0.0f ? static_cast<int>(std::lround(stats.cpu_usage * 100.0f)) : -1, &palette::accent, cpu_tone, 0});
    if (gpu_stats_available(gpu)) {
        const Color *gpu_tone = &palette::text_dim;
        std::string gpu_label = temp_label("GPU", gpu.celsius, gpu_tone);
        tiles.push_back({icon::gpu, gpu_label, std::clamp(gpu.usage_percent / 100.0f, 0.0f, 1.0f), static_cast<int>(std::lround(gpu.usage_percent)), &gpu_color, gpu_tone, 0});
    }
    tiles.push_back({icon::device_desktop, "RAM", std::max(0.0f, stats.mem_usage), stats.mem_usage >= 0.0f ? static_cast<int>(std::lround(stats.mem_usage * 100.0f)) : -1, &palette::lavender, &palette::text_dim, 1});
    tiles.push_back({icon::folder, "DISK", std::max(0.0f, stats.disk_pct / 100.0f), stats.disk_pct >= 0.0f ? static_cast<int>(std::lround(stats.disk_pct)) : -1, &palette::accent_alt, &palette::text_dim, 1});

    ui::TextStyle label_style = text_style(static_cast<int>(kLockFontMono));
    float cell_extra = canvas.measure("CPU", label_style).h + kLockResGaugeLabelGap;
    float tile = std::min((col.w - 3.0f * gap) * 0.5f, (avail_h - gap - 2.0f * cell_extra) * 0.5f);
    float stroke = tile * kLockResGaugeStrokeRatio;
    float col_gap = (col.w - 2.0f * tile) / 3.0f;
    float block_h = 2.0f * tile + gap + 2.0f * cell_extra;
    float y0 = body.y + std::max(0.0f, (avail_h - block_h) * 0.5f);
    float row_y[2] = {y0, y0 + tile + cell_extra + gap};

    int row_count[2] = {0, 0};
    for (const Tile &t : tiles) {
        ++row_count[t.row];
    }
    int row_seen[2] = {0, 0};
    ui::TextStyle icon = icon_style(static_cast<int>(kLockResIconSize));
    ui::TextStyle value_style = text_style(static_cast<int>(kLockResValueFont), true);
    for (const Tile &t : tiles) {
        int within = row_seen[t.row]++;
        float tx = row_count[t.row] == 1 ? col.x + (col.w - tile) * 0.5f : (within == 0 ? col.x + col_gap : col.x + col.w - col_gap - tile);
        float ty = row_y[t.row];
        canvas.gauge({std::round(tx), std::round(ty), tile, tile}, stroke, std::clamp(t.frac, 0.0f, 1.0f), *t.accent);

        std::string value = t.percent >= 0 ? std::to_string(t.percent) + "%" : "--";
        ui::TextSize icon_size = canvas.measure(t.glyph, icon);
        ui::TextSize value_size = canvas.measure(value, value_style);
        float stack_y = ty + (tile - icon_size.h - kLockResGaugeIconValueGap - value_size.h) * 0.5f;
        canvas.text(t.glyph, icon, std::round(tx + (tile - icon_size.w) * 0.5f), std::round(stack_y), *t.accent);
        stack_y += icon_size.h + kLockResGaugeIconValueGap;
        canvas.text(value, value_style, std::round(tx + (tile - value_size.w) * 0.5f), std::round(stack_y), palette::text);
        put_centered(canvas, t.label, label_style, tx + tile * 0.5f, ty + tile + kLockResGaugeLabelGap, *t.label_color);
    }
}

void draw_notifications(Pane &pane, const LockRect &col) {
    ui::Canvas &canvas = pane.canvas;
    if (col.h < 60.0f || pane.info.notifications == nullptr) {
        return;
    }
    const std::vector<Notification> &list = *pane.info.notifications;
    std::string header = list.empty() ? "Notifications" : std::to_string(list.size()) + (list.size() == 1 ? " notification" : " notifications");
    CardBody body = draw_card(canvas, col, header);

    if (list.empty()) {
        ui::TextStyle style = text_style(static_cast<int>(kLockFontNormal));
        ui::TextSize size = canvas.measure("No Notifications", style);
        canvas.text("No Notifications", style, std::round(col.x + (col.w - size.w) * 0.5f), std::round(col.y + col.h * 0.5f), palette::text_dim);
        return;
    }

    float clip_h = col.y + col.h - body.y - kLockSidePanelPad;
    float cw = col.w - 2.0f * kLockSidePanelPad;
    float pad = kLockNotifCardPad;
    int fit = static_cast<int>(cw - 2.0f * pad);
    canvas.begin_group({body.x, body.y, body.w, clip_h}, {1.0f, true});
    float cy = 0.0f;
    int shown = 0;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (shown >= kLockNotifMaxCards || cy >= clip_h) {
            break;
        }
        const Notification &record = *it;
        ui::TextStyle app_style = text_style(static_cast<int>(kLockFontMono), true, fit);
        ui::TextStyle summary_style = text_style(static_cast<int>(kLockFontNormal), false, fit);
        ui::TextStyle body_style = text_style(static_cast<int>(kLockFontMono), false, fit);
        std::string app = record.app.empty() ? "Notification" : record.app;
        ui::TextSize app_size = canvas.measure(app, app_style);
        ui::TextSize summary_size = record.summary.empty() ? ui::TextSize{} : canvas.measure(record.summary, summary_style);
        ui::TextSize body_size = record.body.empty() ? ui::TextSize{} : canvas.measure(record.body, body_style);
        float ch = 2.0f * pad + app_size.h + (record.summary.empty() ? 0.0f : kLockMediaTextGap + summary_size.h) + (record.body.empty() ? 0.0f : kLockMediaTextGap + body_size.h);

        canvas.rounded({0.0f, cy, cw, ch}, kLockNotifCardRadius, palette::field_bg);
        float iy = cy + pad;
        canvas.text(app, app_style, pad, std::round(iy), palette::accent);
        iy += app_size.h + kLockMediaTextGap;
        if (!record.summary.empty()) {
            canvas.text(record.summary, summary_style, pad, std::round(iy), palette::text);
            iy += summary_size.h + kLockMediaTextGap;
        }
        if (!record.body.empty()) {
            canvas.text(record.body, body_style, pad, std::round(iy), palette::text_muted);
        }
        cy += ch + kLockNotifCardGap;
        ++shown;
    }
    canvas.end_group();
}

void draw_center(Pane &pane, const LockRect &col, float output_h) {
    ui::Canvas &canvas = pane.canvas;
    draw_card(canvas, col, "");

    float scale = lock_center_scale(output_h);
    int clock_px = std::max(1, static_cast<int>(kLockFontClock * scale));
    ui::TextStyle clock_style = text_style(clock_px, true);
    ui::TextSize hour = canvas.measure(pane.info.hour, clock_style);
    ui::TextSize colon = canvas.measure(":", clock_style);
    ui::TextSize minute = canvas.measure(pane.info.minute, clock_style);
    ui::TextStyle date_style = text_style(static_cast<int>(kLockFontDate), true);
    ui::TextSize date = canvas.measure(pane.info.date, date_style);

    float clock_h = std::max(hour.h, minute.h);
    float clock_w = hour.w + kLockClockGap + colon.w + kLockClockGap + minute.w;
    float total_h = lock_content_height(clock_h, date.h, 0.0f);
    float y = col.y + (col.h - total_h) * 0.5f;
    float cx = col.x + col.w * 0.5f;

    float mx = cx - clock_w * 0.5f;
    canvas.text(pane.info.hour, clock_style, std::round(mx), std::round(y), palette::accent);
    mx += hour.w + kLockClockGap;
    canvas.text(":", clock_style, std::round(mx), std::round(y), palette::text);
    mx += colon.w + kLockClockGap;
    canvas.text(pane.info.minute, clock_style, std::round(mx), std::round(y), palette::lavender);
    y += clock_h + kLockGapClockDate;

    put_centered(canvas, pane.info.date, date_style, cx, y, palette::text);
    y += date.h + kLockGapDateAvatar;

    ui::Box avatar{std::round(cx - kLockProfileSize * 0.5f), std::round(y), kLockProfileSize, kLockProfileSize};
    canvas.rounded(avatar, kLockProfileSize * 0.5f, palette::field_bg);
    ui::ImageId face = canvas.image(kLockProfileAsset, static_cast<int>(kLockProfileSize) * 2);
    if (face != ui::no_image) {
        canvas.begin_group(avatar, {1.0f, true, kLockProfileSize * 0.5f});
        canvas.draw_image(face, {0.0f, 0.0f, avatar.w, avatar.h}, palette::text);
        canvas.end_group();
    }
    canvas.rounded(avatar, kLockProfileSize * 0.5f, {0.0f, 0.0f, 0.0f, 0.0f}, kLockProfileBorderWidth, palette::accent);
    y += kLockProfileSize + kLockGapAvatarInput;

    float pill_w = col.w * kLockInputWidthFrac;
    draw_pill(pane, cx - pill_w * 0.5f, y, pill_w);
}

} // namespace

void paint_lock(ui::Canvas &canvas, LockModel &model, const LockInfo &info, float width, float height) {
    float card_w = lock_card_width(height);
    float card_h = lock_card_height(height);
    float px = 0.0f;
    float py = 0.0f;
    lock_panel_origin(width, height, card_w, card_h, px, py);
    px = std::round(px);
    py = std::round(py);
    canvas.rounded({px, py, card_w, card_h}, kLockCardRadius, palette::overlay, kLockBgBorderWidth, palette::accent);

    float center_w = kLockCenterWidth * lock_center_scale(height);
    LockRect left, center, right;
    lock_columns(card_w, card_h, center_w, left, center, right);

    model.hits() = {};
    canvas.begin_group({px, py, card_w, card_h}, {1.0f, true, kLockCardRadius});
    Pane pane{canvas, model, info, px, py};

    float bat_h = draw_battery(pane, left);
    float top = left.y + (bat_h > 0.0f ? bat_h + kLockPanelGap : 0.0f);
    float rem = left.h - (top - left.y);
    float ch = std::max(0.0f, (rem - kLockPanelGap) * 0.5f);
    draw_fetch(pane, {left.x, top, left.w, ch});
    draw_media(pane, {left.x, top + ch + kLockPanelGap, left.w, ch});

    float side_h = lock_side_card_height(right.h);
    draw_resources(pane, {right.x, right.y, right.w, side_h});
    draw_notifications(pane, {right.x, right.y + side_h + kLockPanelGap, right.w, side_h});

    draw_center(pane, center, height);
    canvas.end_group();
}

} // namespace astralia
