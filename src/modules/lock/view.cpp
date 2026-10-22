#include <algorithm>
#include <cmath>
#include <string>

#include "config/lock_config.h"

#include "render/icons.h"
#include "render/sticker.h"
#include "render/tokens.h"

#include "modules/lock/layout.h"
#include "modules/lock/view.h"

namespace astralia::lock {

namespace {

constexpr Color gpu_color = color(lock_config::kResGaugeGpuColorHex);

struct Pane {
    ui::Canvas &canvas;
    Model &model;
    Motion &motion;
    const Info &info;
    float origin_x;
    float origin_y;
};

ui::TextStyle text_style(int px, bool bold = false, int max_width = 0) {
    return {ui::FontFamily::text, px, bold, max_width, false};
}

ui::TextStyle icon_style(int px) { return {ui::FontFamily::icon, px}; }

ui::TextSize put(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style, float x,
                 float y, const Color &color) {
    ui::TextSize size = canvas.measure(text, style);
    canvas.text(text, style, std::round(x), std::round(y), color);
    return size;
}

ui::TextSize put_centered(ui::Canvas &canvas, std::string_view text, const ui::TextStyle &style,
                          float cx, float y, const Color &color) {
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

CardBody draw_card(ui::Canvas &canvas, const Rect &col, const std::string &title) {
    sticker(canvas, {col.x, col.y, col.w, col.h}, metrics::radius_card, palette::surface);
    float x = col.x + lock_config::kSidePanelPad;
    float y = col.y + lock_config::kSidePanelPad;
    float w = col.w - 2.0f * lock_config::kSidePanelPad;
    if (title.empty()) {
        return {x, y, w};
    }
    ui::TextStyle style = text_style(static_cast<int>(lock_config::kFontNormal), true);
    ui::TextSize measured = canvas.measure(title, style);
    sticker(canvas,
            {x - lock_config::kCardTagPad, y - lock_config::kCardTagPad / 2.0f,
             measured.w + 2.0f * lock_config::kCardTagPad + metrics::shadow_widget,
             measured.h + lock_config::kCardTagPad + metrics::shadow_widget},
            metrics::radius_widget, palette::pop_a, palette::ink, metrics::border_thin,
            metrics::shadow_widget);
    ui::TextSize size = put(canvas, title, style, x, y, palette::text);
    return {x, y + size.h + lock_config::kCardHeaderGap + lock_config::kCardTagPad, w};
}

void draw_pill(Pane &pane, float x, float y, float w) {
    ui::Canvas &canvas = pane.canvas;
    const Model &model = pane.model;
    float h = lock_config::kInputHeight;
    float r = h * 0.5f;
    canvas.rounded({x, y, w, h}, metrics::radius_card, palette::pop_b, lock_config::kBgBorderWidth,
                   palette::ink);

    float cy = y + h * 0.5f;
    ui::TextStyle icon = icon_style(static_cast<int>(lock_config::kPillIconSize));
    ui::TextSize lock_size = canvas.measure(icon::lock, icon);
    canvas.text(icon::lock, icon, std::round(x + r - lock_size.w * 0.5f),
                std::round(cy - lock_size.h * 0.5f), palette::text_muted);

    bool has_text = !model.password().empty();
    float btn = lock_config::kPillButtonSize;
    float btn_x = x + w - btn - (h - btn) * 0.5f;
    float btn_y = cy - btn * 0.5f;
    selectable(canvas, {btn_x, btn_y, btn, btn}, metrics::radius_widget, has_text, false,
               palette::surface_alt);
    ui::TextSize arrow = canvas.measure(icon::arrow_right, icon);
    canvas.text(icon::arrow_right, icon, std::round(btn_x + (btn - arrow.w) * 0.5f),
                std::round(btn_y + (btn - arrow.h) * 0.5f),
                has_text ? palette::text : palette::text_muted);
    pane.model.hits().pill_button = absolute(pane, {btn_x, btn_y, btn, btn});

    float mid_x = x + h;
    float mid_w = w - 2.0f * h;
    if (mid_w < 10.0f) {
        mid_x = x + lock_config::kPillPad;
        mid_w = w - 2.0f * lock_config::kPillPad;
    }
    ui::TextStyle normal = text_style(static_cast<int>(lock_config::kFontNormal));
    Motion &motion = pane.motion;
    if (!has_text) {
        const char *message = model.failed()
                                  ? lock_config::kFailText
                                  : (model.authenticating() ? lock_config::kLoadingText
                                                            : lock_config::kPlaceholderText);
        const Color &tone = model.failed() ? palette::critical : palette::text_muted;
        ui::TextSize size = canvas.measure(message, normal);
        canvas.text(message, normal, std::round(mid_x + (mid_w - size.w) * 0.5f),
                    std::round(cy - size.h * 0.5f), tone);
        text_field_row_slide(motion.row, motion.animations, lock_config::kOwnerDotRowX,
                             mid_x + dot_x(0, 0, mid_w));
        return;
    }
    size_t length = utf8_len(model.password());
    size_t capacity = std::max<size_t>(1, static_cast<size_t>(mid_w / lock_config::kDotSize));
    int visible = static_cast<int>(std::min(length, capacity));
    float row_x = text_field_row_slide(motion.row, motion.animations, lock_config::kOwnerDotRowX,
                                       mid_x + dot_x(0, visible, mid_w));
    for (int i = 0; i < visible; ++i) {
        const TextFieldCharAnim *anim = static_cast<size_t>(i) < motion.dots.chars.size()
                                            ? &motion.dots.chars[static_cast<size_t>(i)]
                                            : nullptr;
        float size = lock_config::kDotSize * (anim ? anim->scale : 1.0f);
        float dx =
            row_x + static_cast<float>(i) * lock_config::kDotSize + (anim ? anim->slide_x : 0.0f);
        ui::Box box{std::round(dx + (lock_config::kDotSize - size) * 0.5f),
                    std::round(cy - size * 0.5f), size, size};
        canvas.rounded(box, size * 0.5f, palette::accent);
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

float draw_battery(Pane &pane, const Rect &col) {
    const BatteryStatus *status = pane.info.battery;
    if (status == nullptr || !status->present) {
        return 0.0f;
    }
    ui::Canvas &canvas = pane.canvas;
    int px = static_cast<int>(lock_config::kFontNormal);
    ui::TextSize title = canvas.measure("Battery", text_style(px, true));
    ui::TextSize icon = canvas.measure(battery_glyph(*status), icon_style(px));
    std::string label = status->full ? "Plugged in" : std::to_string(status->percent) + "%";
    std::string line = "Battery  " + label;
    ui::TextSize label_size = canvas.measure(line, text_style(px));
    float row_h = std::max(icon.h, label_size.h);
    float card_h = lock_config::kSidePanelPad + title.h + lock_config::kCardHeaderGap + row_h +
                   lock_config::kBatteryRowGap + lock_config::kBatteryBarHeight +
                   lock_config::kSidePanelPad;

    CardBody body = draw_card(canvas, {col.x, col.y, col.w, card_h}, "Battery");
    float rx = body.x;
    canvas.text(battery_glyph(*status), icon_style(px), std::round(rx),
                std::round(body.y + (row_h - icon.h) * 0.5f), palette::text);
    rx += icon.w + lock_config::kBatteryIconGap;
    canvas.text(line, text_style(px), std::round(rx),
                std::round(body.y + (row_h - label_size.h) * 0.5f), palette::text);
    float bar_y = body.y + row_h + lock_config::kBatteryRowGap;
    canvas.rounded({std::round(body.x), std::round(bar_y), body.w, lock_config::kBatteryBarHeight},
                   lock_config::kBatteryBarRadius, palette::text_alpha11);
    float fill = body.w * std::clamp(status->percent / 100.0f, 0.0f, 1.0f);
    if (fill > 0.0f) {
        canvas.rounded(
            {std::round(body.x), std::round(bar_y), fill, lock_config::kBatteryBarHeight},
            lock_config::kBatteryBarRadius, palette::accent);
    }
    return card_h;
}

void draw_fetch(Pane &pane, const Rect &col) {
    ui::Canvas &canvas = pane.canvas;
    int px = static_cast<int>(lock_config::kFontMono);
    float inner_w = col.w - 2.0f * lock_config::kSidePanelPad;
    ui::TextStyle fit = text_style(px, false, static_cast<int>(inner_w));
    CardBody body = draw_card(canvas, col, "System");
    float x = body.x;
    float y = body.y;

    ui::TextSize prompt = canvas.measure(">", text_style(px, true));
    ui::TextSize name = canvas.measure(pane.info.user, text_style(px, true));
    float head_h = std::max(prompt.h, name.h);
    float hx = x;
    canvas.text(">", text_style(px, true), std::round(hx),
                std::round(y + (head_h - prompt.h) * 0.5f), palette::lavender);
    hx += prompt.w + lock_config::kFetchChipPad;
    canvas.text(pane.info.user, text_style(px, true), std::round(hx),
                std::round(y + (head_h - name.h) * 0.5f), palette::text);
    y += head_h + lock_config::kFetchLineGap;

    for (const std::string &line :
         {"OS  : " + pane.info.os, "WM  : " + pane.info.wm, pane.info.uptime}) {
        ui::TextSize size = canvas.measure(line, fit);
        canvas.text(line, fit, std::round(x), std::round(y), palette::text_muted);
        y += std::max(size.h, 1.0f) + lock_config::kFetchLineGap;
    }

    const Color *terminal[12] = {&palette::accent,     &palette::accent_container,
                                 &palette::accent_alt, &palette::accent_alt_container,
                                 &palette::electro,    &palette::lavender,
                                 &palette::critical,   &palette::text,
                                 &palette::text_muted, &palette::surface_alt,
                                 &palette::field_bg,   &palette::base};
    int per_row = fetch_colour_count(inner_w, 6);
    int rows = per_row > 0 ? 2 : 0;
    int total = std::min(12, per_row * rows);
    int drawn = 0;
    for (int r = 0; r < rows; ++r) {
        int in_row = std::min(per_row, total - drawn);
        float bx = x;
        for (int i = 0; i < in_row; ++i) {
            canvas.rounded({bx, y, lock_config::kFetchColorBox, lock_config::kFetchColorBox},
                           metrics::radius_widget, *terminal[drawn]);
            bx += lock_config::kFetchColorBox + lock_config::kFetchColorGap;
            ++drawn;
        }
        y += lock_config::kFetchColorBox + lock_config::kFetchColorGap;
    }
}

void draw_media(Pane &pane, const Rect &col) {
    ui::Canvas &canvas = pane.canvas;
    Hits &hits = pane.model.hits();
    hits.media_prev = hits.media_play = hits.media_next = {};
    float pad = lock_config::kSidePanelPad;
    if (col.h < lock_config::kMediaArt + 2.0f * pad || pane.info.media == nullptr) {
        return;
    }
    CardBody body = draw_card(canvas, col, "Media");

    const MediaStatus &media = *pane.info.media;
    float cx = col.x + col.w * 0.5f;
    float art = lock_config::kMediaArt;
    float bs = lock_config::kMediaBtnSize;
    int title_px = static_cast<int>(lock_config::kFontNormal);
    int fit = static_cast<int>(col.w - 2.0f * pad);
    std::string title =
        media.has_player && !media.track.title.empty() ? media.track.title : "Nothing playing";
    std::string artist = media.has_player && !media.track.artist.empty() ? media.track.artist
                                                                         : "Try playing some music";
    ui::TextStyle title_style = text_style(title_px, true, fit);
    ui::TextStyle artist_style = text_style(static_cast<int>(lock_config::kFontMono), false, fit);
    ui::TextSize title_size = canvas.measure(title, title_style);
    ui::TextSize artist_size = canvas.measure(artist, artist_style);

    float block_h = art + lock_config::kMediaTextGap * 3.0f + title_size.h +
                    lock_config::kMediaTextGap + artist_size.h + lock_config::kMediaBtnGap * 1.5f +
                    bs;
    float avail_h = col.y + col.h - pad - body.y;
    float ay = body.y + std::max(0.0f, (avail_h - block_h) * 0.5f);
    ui::Box art_box{cx - art * 0.5f, ay, art, art};

    canvas.rounded(art_box, metrics::radius_widget, palette::field_bg);
    ui::ImageId cover = ui::no_image;
    if (media.has_player && media_is_local_art(media.track.art_url)) {
        cover = canvas.thumbnail(media.track.art_url.substr(7), static_cast<int>(art));
    }
    if (cover != ui::no_image) {
        canvas.draw_image(cover, art_box, palette::white);
    } else {
        ui::TextStyle note_style = icon_style(static_cast<int>(art * 0.4f));
        ui::TextSize note = canvas.measure(icon::music_note, note_style);
        canvas.text(icon::music_note, note_style, std::round(cx - note.w * 0.5f),
                    std::round(ay + (art - note.h) * 0.5f), palette::text_dim);
    }

    float ty = ay + art + lock_config::kMediaTextGap * 3.0f;
    put_centered(canvas, title, title_style, cx, ty, palette::lavender);
    ty += title_size.h + lock_config::kMediaTextGap;
    put_centered(canvas, artist, artist_style, cx, ty, palette::text_muted);
    ty += artist_size.h + lock_config::kMediaBtnGap * 1.5f;

    if (ty + bs > col.y + col.h - pad) {
        return;
    }
    float row_w = 3.0f * bs + 2.0f * lock_config::kMediaBtnGap;
    float bx = cx - row_w * 0.5f;
    auto button = [&](float rx, const char *glyph, ui::Box &out) {
        canvas.rounded({rx, ty, bs, bs}, bs * 0.5f, palette::field_bg);
        ui::TextStyle style = icon_style(static_cast<int>(bs * 0.5f));
        ui::TextSize size = canvas.measure(glyph, style);
        canvas.text(glyph, style, std::round(rx + (bs - size.w) * 0.5f),
                    std::round(ty + (bs - size.h) * 0.5f), palette::text);
        out = absolute(pane, {rx, ty, bs, bs});
    };
    button(bx, icon::player_prev, hits.media_prev);
    button(bx + bs + lock_config::kMediaBtnGap,
           media.playback == MediaPlayback::playing ? icon::player_pause : icon::player_play,
           hits.media_play);
    button(bx + 2.0f * (bs + lock_config::kMediaBtnGap), icon::player_next, hits.media_next);
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
    label_color = celsius >= lock_config::kResTempWarnC ? &palette::critical : &palette::text_dim;
    return std::string(base) + " - " + std::to_string(static_cast<int>(std::lround(celsius))) +
           "\xC2\xB0"
           "C";
}

void draw_resources(Pane &pane, const Rect &col) {
    ui::Canvas &canvas = pane.canvas;
    float gap = lock_config::kResTileGap;
    CardBody body = draw_card(canvas, col, "Resources");
    float avail_h = col.y + col.h - lock_config::kSidePanelPad - body.y;
    if (pane.info.stats == nullptr || pane.info.cpu_temp == nullptr ||
        pane.info.gpu_temp == nullptr) {
        return;
    }
    const SystemStatsState &stats = *pane.info.stats;
    const CpuTempState &cpu = *pane.info.cpu_temp;
    const GpuTempState &gpu = *pane.info.gpu_temp;

    std::vector<Tile> tiles;
    const Color *cpu_tone = &palette::text_dim;
    std::string cpu_label = temp_label("CPU", cpu.celsius, cpu_tone);
    tiles.push_back(
        {icon::cpu, cpu_label, std::max(0.0f, stats.cpu_usage),
         stats.cpu_usage >= 0.0f ? static_cast<int>(std::lround(stats.cpu_usage * 100.0f)) : -1,
         &palette::accent, cpu_tone, 0});
    if (gpu_stats_available(gpu)) {
        const Color *gpu_tone = &palette::text_dim;
        std::string gpu_label = temp_label("GPU", gpu.celsius, gpu_tone);
        tiles.push_back({icon::gpu, gpu_label, std::clamp(gpu.usage_percent / 100.0f, 0.0f, 1.0f),
                         static_cast<int>(std::lround(gpu.usage_percent)), &gpu_color, gpu_tone,
                         0});
    }
    tiles.push_back(
        {icon::device_desktop, "RAM", std::max(0.0f, stats.mem_usage),
         stats.mem_usage >= 0.0f ? static_cast<int>(std::lround(stats.mem_usage * 100.0f)) : -1,
         &palette::lavender, &palette::text_dim, 1});
    tiles.push_back({icon::folder, "DISK", std::max(0.0f, stats.disk_pct / 100.0f),
                     stats.disk_pct >= 0.0f ? static_cast<int>(std::lround(stats.disk_pct)) : -1,
                     &palette::accent_alt, &palette::text_dim, 1});

    ui::TextStyle label_style = text_style(static_cast<int>(lock_config::kFontMono));
    float cell_extra = canvas.measure("CPU", label_style).h + lock_config::kResGaugeLabelGap;
    float tile = std::min((col.w - 3.0f * gap) * 0.5f, (avail_h - gap - 2.0f * cell_extra) * 0.5f);
    float stroke = tile * lock_config::kResGaugeStrokeRatio;
    float col_gap = (col.w - 2.0f * tile) / 3.0f;
    float block_h = 2.0f * tile + gap + 2.0f * cell_extra;
    float y0 = body.y + std::max(0.0f, (avail_h - block_h) * 0.5f);
    float row_y[2] = {y0, y0 + tile + cell_extra + gap};

    int row_count[2] = {0, 0};
    for (const Tile &t : tiles) {
        ++row_count[t.row];
    }
    int row_seen[2] = {0, 0};
    ui::TextStyle icon = icon_style(static_cast<int>(lock_config::kResIconSize));
    ui::TextStyle value_style = text_style(static_cast<int>(lock_config::kResValueFont), true);
    for (const Tile &t : tiles) {
        int within = row_seen[t.row]++;
        float tx = row_count[t.row] == 1
                       ? col.x + (col.w - tile) * 0.5f
                       : (within == 0 ? col.x + col_gap : col.x + col.w - col_gap - tile);
        float ty = row_y[t.row];
        canvas.gauge({std::round(tx), std::round(ty), tile, tile}, stroke,
                     std::clamp(t.frac, 0.0f, 1.0f), *t.accent);

        std::string value = t.percent >= 0 ? std::to_string(t.percent) + "%" : "--";
        ui::TextSize icon_size = canvas.measure(t.glyph, icon);
        ui::TextSize value_size = canvas.measure(value, value_style);
        float stack_y =
            ty + (tile - icon_size.h - lock_config::kResGaugeIconValueGap - value_size.h) * 0.5f;
        canvas.text(t.glyph, icon, std::round(tx + (tile - icon_size.w) * 0.5f),
                    std::round(stack_y), *t.accent);
        stack_y += icon_size.h + lock_config::kResGaugeIconValueGap;
        canvas.text(value, value_style, std::round(tx + (tile - value_size.w) * 0.5f),
                    std::round(stack_y), palette::text);
        put_centered(canvas, t.label, label_style, tx + tile * 0.5f,
                     ty + tile + lock_config::kResGaugeLabelGap, *t.label_color);
    }
}

void draw_notifications(Pane &pane, const Rect &col) {
    ui::Canvas &canvas = pane.canvas;
    if (col.h < 60.0f || pane.info.notifications == nullptr) {
        return;
    }
    const std::vector<Notification> &list = *pane.info.notifications;
    std::string header = list.empty() ? "Notifications"
                                      : std::to_string(list.size()) +
                                            (list.size() == 1 ? " notification" : " notifications");
    CardBody body = draw_card(canvas, col, header);

    if (list.empty()) {
        ui::TextStyle style = text_style(static_cast<int>(lock_config::kFontNormal));
        ui::TextSize size = canvas.measure("No Notifications", style);
        canvas.text("No Notifications", style, std::round(col.x + (col.w - size.w) * 0.5f),
                    std::round(col.y + col.h * 0.5f), palette::text_dim);
        return;
    }

    float clip_h = col.y + col.h - body.y - lock_config::kSidePanelPad;
    float cw = col.w - 2.0f * lock_config::kSidePanelPad;
    float pad = lock_config::kNotifCardPad;
    int fit = static_cast<int>(cw - 2.0f * pad);
    canvas.begin_group({body.x, body.y, body.w, clip_h}, {1.0f, true});
    float cy = 0.0f;
    int shown = 0;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (shown >= lock_config::kNotifMaxCards || cy >= clip_h) {
            break;
        }
        const Notification &record = *it;
        ui::TextStyle app_style = text_style(static_cast<int>(lock_config::kFontMono), true, fit);
        ui::TextStyle summary_style =
            text_style(static_cast<int>(lock_config::kFontNormal), false, fit);
        ui::TextStyle body_style = text_style(static_cast<int>(lock_config::kFontMono), false, fit);
        std::string app = record.app.empty() ? "Notification" : record.app;
        ui::TextSize app_size = canvas.measure(app, app_style);
        ui::TextSize summary_size =
            record.summary.empty() ? ui::TextSize{} : canvas.measure(record.summary, summary_style);
        ui::TextSize body_size =
            record.body.empty() ? ui::TextSize{} : canvas.measure(record.body, body_style);
        float ch = 2.0f * pad + app_size.h +
                   (record.summary.empty() ? 0.0f : lock_config::kMediaTextGap + summary_size.h) +
                   (record.body.empty() ? 0.0f : lock_config::kMediaTextGap + body_size.h);

        canvas.rounded({0.0f, cy, cw, ch}, metrics::radius_card, palette::field_bg);
        float iy = cy + pad;
        canvas.text(app, app_style, pad, std::round(iy), palette::lavender);
        iy += app_size.h + lock_config::kMediaTextGap;
        if (!record.summary.empty()) {
            canvas.text(record.summary, summary_style, pad, std::round(iy), palette::text);
            iy += summary_size.h + lock_config::kMediaTextGap;
        }
        if (!record.body.empty()) {
            canvas.text(record.body, body_style, pad, std::round(iy), palette::text_muted);
        }
        cy += ch + lock_config::kNotifCardGap;
        ++shown;
    }
    canvas.end_group();
}

void draw_center(Pane &pane, const Rect &col, float output_h) {
    ui::Canvas &canvas = pane.canvas;
    draw_card(canvas, col, "");

    float scale = center_scale(output_h);
    int clock_px = std::max(1, static_cast<int>(lock_config::kFontClock * scale));
    ui::TextStyle clock_style = text_style(clock_px, true);
    ui::TextSize hour = canvas.measure(pane.info.hour, clock_style);
    ui::TextSize colon = canvas.measure(":", clock_style);
    ui::TextSize minute = canvas.measure(pane.info.minute, clock_style);
    ui::TextStyle date_style = text_style(static_cast<int>(lock_config::kFontDate), true);
    ui::TextSize date = canvas.measure(pane.info.date, date_style);

    float clock_h = std::max(hour.h, minute.h);
    float clock_w = hour.w + lock_config::kClockGap + colon.w + lock_config::kClockGap + minute.w;
    float total_h = content_height(clock_h, date.h, 0.0f);
    float y = col.y + (col.h - total_h) * 0.5f;
    float cx = col.x + col.w * 0.5f;

    float mx = cx - clock_w * 0.5f;
    canvas.text(pane.info.hour, clock_style, std::round(mx), std::round(y), palette::text);
    mx += hour.w + lock_config::kClockGap;
    canvas.text(":", clock_style, std::round(mx), std::round(y), palette::text);
    mx += colon.w + lock_config::kClockGap;
    canvas.text(pane.info.minute, clock_style, std::round(mx), std::round(y), palette::lavender);
    y += clock_h + lock_config::kGapClockDate;

    put_centered(canvas, pane.info.date, date_style, cx, y, palette::text);
    y += date.h + lock_config::kGapDateAvatar;

    ui::Box avatar{std::round(cx - lock_config::kProfileSize * 0.5f), std::round(y),
                   lock_config::kProfileSize, lock_config::kProfileSize};
    canvas.rounded(avatar, lock_config::kProfileSize * 0.5f, palette::field_bg);
    ui::ImageId face =
        pane.info.face.empty()
            ? ui::no_image
            : canvas.image(pane.info.face, static_cast<int>(lock_config::kProfileSize) * 2);
    if (face != ui::no_image) {
        canvas.begin_group(avatar, {1.0f, true, lock_config::kProfileSize * 0.5f});
        canvas.draw_image(face, {0.0f, 0.0f, avatar.w, avatar.h}, palette::white);
        canvas.end_group();
    } else {
        ui::TextStyle glyph_style = icon_style(static_cast<int>(lock_config::kProfileSize * 0.5f));
        ui::TextSize glyph = canvas.measure(icon::user, glyph_style);
        canvas.text(icon::user, glyph_style, std::round(avatar.x + (avatar.w - glyph.w) * 0.5f),
                    std::round(avatar.y + (avatar.h - glyph.h) * 0.5f), palette::text);
    }
    canvas.rounded(avatar, lock_config::kProfileSize * 0.5f, {0.0f, 0.0f, 0.0f, 0.0f},
                   lock_config::kProfileBorderWidth, palette::ink);
    y += lock_config::kProfileSize + lock_config::kGapAvatarInput;

    float pill_w = col.w * lock_config::kInputWidthFrac;
    draw_pill(pane, cx - pill_w * 0.5f, y, pill_w);
}

} // namespace

void paint(ui::Canvas &canvas, Model &model, Motion &motion, const Info &info, float width,
           float height) {
    float card_w = card_width(height);
    float card_h = card_height(height);
    float pw = motion.panel_w > 0.0f ? motion.panel_w : card_w;
    float ph = motion.panel_h > 0.0f ? motion.panel_h : card_h;
    float px = 0.0f;
    float py = 0.0f;
    panel_origin(width, height, pw, ph, px, py);
    px = std::round(px);
    py = std::round(py);

    model.hits() = {};
    canvas.begin_group({px, py, pw, ph},
                       {motion.panel_scale, true, metrics::radius_card, motion.panel_rotation});
    sticker(canvas, {0.0f, 0.0f, pw, ph}, metrics::radius_card, palette::paper);

    if (motion.icon_alpha > 0.001f) {
        const char *glyph = icon::lock;
        ui::TextStyle style = icon_style(static_cast<int>(lock_config::kFontIcon));
        ui::TextSize size = canvas.measure(glyph, style);
        canvas.text(glyph, style, std::round((pw - size.w) * 0.5f),
                    std::round((ph - size.h) * 0.5f),
                    with_alpha(palette::text, palette::text.a * motion.icon_alpha));
    }
    if (motion.content_alpha <= 0.001f) {
        canvas.end_group();
        return;
    }

    float center_w = lock_config::kCenterWidth * center_scale(height);
    Rect left, center, right;
    columns(card_w, card_h, center_w, left, center, right);

    float cx0 = (pw - card_w) * 0.5f;
    float cy0 = (ph - card_h) * 0.5f;
    canvas.set_opacity(motion.content_alpha);
    canvas.begin_group({cx0, cy0, card_w, card_h}, {motion.content_scale});
    Pane pane{canvas, model, motion, info, px + cx0, py + cy0};

    float bat_h = draw_battery(pane, left);
    float top = left.y + (bat_h > 0.0f ? bat_h + lock_config::kPanelGap : 0.0f);
    float rem = left.h - (top - left.y);
    float ch = std::max(0.0f, (rem - lock_config::kPanelGap) * 0.5f);
    draw_fetch(pane, {left.x, top, left.w, ch});
    draw_media(pane, {left.x, top + ch + lock_config::kPanelGap, left.w, ch});

    float side_h = side_card_height(right.h);
    draw_resources(pane, {right.x, right.y, right.w, side_h});
    draw_notifications(pane, {right.x, right.y + side_h + lock_config::kPanelGap, right.w, side_h});

    draw_center(pane, center, height);
    canvas.end_group();
    canvas.set_opacity(1.0f);
    canvas.end_group();
    if (motion.content_scale < 0.99f) {
        model.hits() = {};
    }
}

} // namespace astralia::lock
