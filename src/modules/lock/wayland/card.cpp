#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <deque>
#include <string>

#include "wayland/app/monitor_output.h"
#include "wayland/app/user_info.h"
#include "wayland/app/wayland_state.h"

#include "modules/lock/wayland/card.h"
#include "modules/lock/wayland/layout.h"

#include "ui/glyphs.h"
#include "ui/tokens.h"
#include "wayland/render/arc_gauge.h"
#include "wayland/render/icon.h"
#include "wayland/render/image.h"
#include "wayland/render/node.h"
#include "wayland/render/text.h"
#include "wayland/render/text_elide.h"

#include "service/media_service.h"
#include "service/telemetry_service.h"

constexpr astralia::Color kLockResGaugeGpuColor = astralia::color(kLockResGaugeGpuColorHex);

namespace {

float px_h(const Texture *t) {
    if (!t)
        return 0.0f;
    return static_cast<float>(t->height) / static_cast<float>(t->scale > 0 ? t->scale : 1);
}
float px_w(const Texture *t) {
    if (!t)
        return 0.0f;
    return static_cast<float>(t->width) / static_cast<float>(t->scale > 0 ? t->scale : 1);
}

std::string strftime_now(const char *fmt, size_t cap) {
    std::time_t now = std::time(nullptr);
    std::string buf(cap, '\0');
    size_t n = std::strftime(buf.data(), cap, fmt, std::localtime(&now));
    buf.resize(n);
    return buf;
}
std::string hour_string() { return strftime_now("%H", 8); }
std::string minute_string() { return strftime_now("%M", 8); }
std::string date_string() {
    std::string s = strftime_now("%a %Y-%m-%d", 64);
    for (char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

const Texture *tc_text(LockState &st, const std::string &s, int px, bool bold, int32_t scale) {
    if (s.empty())
        return nullptr;
    std::string key = "pt:" + std::to_string(px) + (bold ? "b:" : "n:") + s;
    return st.tcache.get(key, [&] { return rasterize_text_px(s, px, bold, scale); });
}

const Texture *tc_icon(LockState &st, const char *glyph, int px, int32_t scale) {
    std::string key = std::string("pi:") + std::to_string(px) + ":" + glyph;
    return st.tcache.get(key, [&] { return rasterize_icon(glyph, scale, px); });
}

std::string wm_name(const WaylandState *app) {
    return app ? app->desktop->name() : "Wayland";
}

void draw_center_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca);
void draw_left_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca);
void draw_right_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca);

size_t utf8_len(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

std::deque<astralia::Color> &color_pool() {
    static thread_local std::deque<astralia::Color> pool;
    return pool;
}

const float *cmod(const astralia::Color &base, float a) {
    auto &pool = color_pool();
    pool.push_back(astralia::with_alpha(base, base.a * a));
    return astralia::rgba(pool.back());
}

float rnd(float v) { return std::round(v); }

void draw_pill(LockState &st, LockOutputSurface &los, Node *content, float x, float y, float w, int32_t scale, float ca) {
    float h = kLockInputHeight;
    float r = h * 0.5f;
    node_add_rrect(content, x, y, w, h, r, kLockBgBorderWidth, cmod(astralia::palette::field_bg, ca), cmod(astralia::palette::accent, ca));

    float cy = y + h * 0.5f;

    const Texture *lock_t =
        tc_icon(st, astralia::icon::lock, static_cast<int>(kLockPillIconSize), scale);
    if (lock_t)
        node_add_texture_rect(content, rnd(x + r - px_w(lock_t) * 0.5f), rnd(cy - px_h(lock_t) * 0.5f), px_w(lock_t), px_h(lock_t), *lock_t, cmod(astralia::palette::text_muted, ca));

    bool has_text = !st.password.text.empty();
    float btn = kLockPillButtonSize;
    float btn_x = x + w - btn - (h - btn) * 0.5f;
    float btn_y = cy - btn * 0.5f;
    node_add_rrect(content, btn_x, btn_y, btn, btn, btn * 0.5f, 0.0f, cmod(has_text ? astralia::palette::accent : astralia::palette::surface_alt, ca), kNodeTransparent);
    const Texture *arrow_t = tc_icon(st, astralia::icon::arrow_right, static_cast<int>(kLockPillIconSize), scale);
    if (arrow_t)
        node_add_texture_rect(content, rnd(btn_x + (btn - px_w(arrow_t)) * 0.5f), rnd(btn_y + (btn - px_h(arrow_t)) * 0.5f), px_w(arrow_t), px_h(arrow_t), *arrow_t, cmod(has_text ? astralia::palette::base : astralia::palette::text_muted, ca));
    los.pill_button = {btn_x, btn_y, btn, btn};

    float mid_x0 = x + h;
    float mid_w = w - 2.0f * h;
    if (mid_w < 10.0f) {
        mid_x0 = x + kLockPillPad;
        mid_w = w - 2.0f * kLockPillPad;
    }

    if (!has_text) {
        if (st.failed) {
            const Texture *ft =
                tc_text(st, kLockFailText, static_cast<int>(kLockFontNormal), false, scale);
            if (ft)
                node_add_texture_rect(content, rnd(mid_x0 + (mid_w - px_w(ft)) * 0.5f), rnd(cy - px_h(ft) * 0.5f), px_w(ft), px_h(ft), *ft, cmod(astralia::palette::critical, ca));
            return;
        }
        const char *ph =
            st.authenticating ? kLockLoadingText : kLockPlaceholderText;
        const Texture *pt =
            tc_text(st, ph, static_cast<int>(kLockFontNormal), false, scale);
        if (pt)
            node_add_texture_rect(content, rnd(mid_x0 + (mid_w - px_w(pt)) * 0.5f), rnd(cy - px_h(pt) * 0.5f), px_w(pt), px_h(pt), *pt, cmod(astralia::palette::text_muted, ca));
        text_field_row_slide(st.pw_row_slide, los.animations, kLockOwnerDotRowX, mid_x0 + lock_dot_x(0, 0, mid_w));
        return;
    }

    int n = static_cast<int>(utf8_len(st.password.text));
    float row_x = text_field_row_slide(st.pw_row_slide, los.animations, kLockOwnerDotRowX, mid_x0 + lock_dot_x(0, n, mid_w));
    for (int i = 0; i < n; ++i) {
        const TextFieldCharAnim *anim =
            i < static_cast<int>(st.pw_anim.chars.size())
                ? &st.pw_anim.chars[static_cast<size_t>(i)]
                : nullptr;
        float dsc = anim ? anim->scale : 1.0f;
        float slide = anim ? anim->slide_x : 0.0f;
        float dx = row_x + static_cast<float>(i) * kLockDotSize + slide;
        float dsz = kLockDotSize * dsc;
        float gx = rnd(dx + (kLockDotSize - dsz) * 0.5f);
        float gy = rnd(cy - dsz * 0.5f);
        if (st.echo_glyph.id)
            node_add_texture_rect(content, gx, gy, dsz, dsz, st.echo_glyph, cmod(astralia::palette::text, ca));
        else
            node_add_rrect(content, gx, gy, dsz, dsz, dsz * 0.5f, 0.0f, cmod(astralia::palette::text, ca), kNodeTransparent);
    }
}

struct LockCard {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

LockCard draw_card(LockState &st, Node *content, const LockRect &col, const std::string &title, int32_t scale, float ca) {
    node_add_rrect(content, col.x, col.y, col.w, col.h, kLockSidePanelRadius, kLockCardBorderWidth, cmod(astralia::palette::overlay, ca), cmod(astralia::palette::accent, ca));

    float x = col.x + kLockSidePanelPad;
    float y = col.y + kLockSidePanelPad;
    float w = col.w - 2.0f * kLockSidePanelPad;
    const Texture *title_t =
        tc_text(st, title, static_cast<int>(kLockFontNormal), true, scale);
    if (!title_t)
        return {x, y, w};

    node_add_texture_rect(content, rnd(x), rnd(y), px_w(title_t), px_h(title_t), *title_t, cmod(astralia::palette::text, ca));
    return {x, y + px_h(title_t) + kLockCardHeaderGap, w};
}

void draw_fetch(LockState &st, Node *content, const LockRect &col, int32_t scale, float ca) {
    float pad = kLockSidePanelPad;
    int fpx = static_cast<int>(kLockFontMono);
    float inner_w = col.w - 2.0f * pad;
    size_t maxc = static_cast<size_t>(std::max(1.0f, inner_w / (static_cast<float>(fpx) * 0.62f)));

    std::vector<std::string> lines;
    lines.push_back("OS  : " + user_info::os_pretty_name());
    lines.push_back("WM  : " + wm_name(st.app));
    lines.push_back(user_info::uptime_string());

    const Texture *label_t = tc_text(st, st.user, fpx, true, scale);
    const Texture *prompt_t = tc_text(st, ">", fpx, true, scale);
    float head_h = std::max(px_h(prompt_t), px_h(label_t));

    std::vector<const Texture *> line_tex;
    float line_h = 0.0f;
    for (const std::string &l : lines) {
        const Texture *t = tc_text(st, elide(l, maxc), fpx, false, scale);
        line_tex.push_back(t);
        line_h = std::max(line_h, px_h(t));
    }
    static const astralia::Color *term[12] = {
        &astralia::palette::accent, &astralia::palette::accent_container,
        &astralia::palette::accent_alt, &astralia::palette::accent_alt_container,
        &astralia::palette::electro, &astralia::palette::lavender,
        &astralia::palette::critical, &astralia::palette::text,
        &astralia::palette::text_muted, &astralia::palette::surface_alt,
        &astralia::palette::field_bg, &astralia::palette::base};
    int per_row = lock_fetch_colour_count(inner_w, 6);
    int rows = per_row > 0 ? 2 : 0;
    int total_boxes = std::min(12, per_row * rows);

    LockCard cc = draw_card(st, content, col, "System", scale, ca);
    float x = cc.x;
    float y = cc.y;

    float hx = x;
    if (prompt_t) {
        node_add_texture_rect(content, rnd(hx), rnd(y + (head_h - px_h(prompt_t)) * 0.5f), px_w(prompt_t), px_h(prompt_t), *prompt_t, cmod(astralia::palette::accent, ca));
        hx += px_w(prompt_t) + kLockFetchChipPad;
    }
    if (label_t)
        node_add_texture_rect(content, rnd(hx), rnd(y + (head_h - px_h(label_t)) * 0.5f), px_w(label_t), px_h(label_t), *label_t, cmod(astralia::palette::text, ca));
    y += head_h + kLockFetchLineGap;

    for (const Texture *t : line_tex) {
        if (t)
            node_add_texture_rect(content, rnd(x), rnd(y), px_w(t), px_h(t), *t, cmod(astralia::palette::text_muted, ca));
        y += line_h + kLockFetchLineGap;
    }

    int drawn = 0;
    for (int r = 0; r < rows; ++r) {
        int in_row = std::min(per_row, total_boxes - drawn);
        float bx = x;
        for (int i = 0; i < in_row; ++i) {
            node_add_rrect(content, bx, y, kLockFetchColorBox, kLockFetchColorBox, kLockResTileRadius * 0.4f, 0.0f, cmod(*term[drawn], ca), kNodeTransparent);
            bx += kLockFetchColorBox + kLockFetchColorGap;
            ++drawn;
        }
        y += kLockFetchColorBox + kLockFetchColorGap;
    }
}

void draw_media(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca) {
    los.media_prev = los.media_play = los.media_next = {};
    float pad = kLockSidePanelPad;
    if (col.h < kLockMediaArt + 2.0f * pad)
        return;

    LockCard cc = draw_card(st, content, col, "Media", scale, ca);

    const astralia::MediaStatus &m = st.app->media->status();
    float cx = col.x + col.w * 0.5f;
    float art = kLockMediaArt;
    float bs = kLockMediaBtnSize;
    int tpx = static_cast<int>(kLockFontNormal);
    size_t maxc = static_cast<size_t>(std::max(1.0f, (col.w - 2.0f * pad) / (static_cast<float>(tpx) * 0.62f)));
    std::string title = m.has_player && !m.track.title.empty()
                            ? m.track.title
                            : "Nothing playing";
    std::string artist = m.has_player && !m.track.artist.empty() ? m.track.artist : "Try playing some music";
    const Texture *tt = tc_text(st, elide(title, maxc), tpx, true, scale);
    const Texture *at =
        tc_text(st, elide(artist, maxc), static_cast<int>(kLockFontMono), false, scale);

    float block_h = art + kLockMediaTextGap * 3.0f + px_h(tt) +
                    kLockMediaTextGap + px_h(at) +
                    kLockMediaBtnGap * 1.5f + bs;
    float avail_h = col.y + col.h - pad - cc.y;
    float ay = cc.y + std::max(0.0f, (avail_h - block_h) * 0.5f);

    node_add_rrect(content, cx - art * 0.5f, ay, art, art, kLockResTileRadius, 0.0f, cmod(astralia::palette::field_bg, ca), kNodeTransparent);

    const Texture *art_tex = nullptr;
    if (m.has_player && astralia::media_is_local_art_url(m.track.art_url)) {
        std::string path = m.track.art_url.substr(7);
        auto it = st.art_cache.find(path);
        if (it == st.art_cache.end())
            it = st.art_cache.emplace(path, load_image_texture(path)).first;
        if (it->second.id)
            art_tex = &it->second;
    }
    if (art_tex)
        node_add_texture_rect(content, cx - art * 0.5f, ay, art, art, *art_tex, cmod(astralia::palette::text, ca));
    else {
        const Texture *note =
            tc_icon(st, astralia::icon::music_note, static_cast<int>(art * 0.4f), scale);
        if (note)
            node_add_texture_rect(content, rnd(cx - px_w(note) * 0.5f), rnd(ay + (art - px_h(note)) * 0.5f), px_w(note), px_h(note), *note, cmod(astralia::palette::text_dim, ca));
    }

    float ty = ay + art + kLockMediaTextGap * 3.0f;
    if (tt)
        node_add_texture_rect(content, rnd(cx - px_w(tt) * 0.5f), rnd(ty), px_w(tt), px_h(tt), *tt, cmod(astralia::palette::accent, ca));
    ty += px_h(tt) + kLockMediaTextGap;
    if (at)
        node_add_texture_rect(content, rnd(cx - px_w(at) * 0.5f), rnd(ty), px_w(at), px_h(at), *at, cmod(astralia::palette::text_muted, ca));
    ty += px_h(at) + kLockMediaBtnGap * 1.5f;

    if (ty + bs > col.y + col.h - pad)
        return;
    float row_w = 3.0f * bs + 2.0f * kLockMediaBtnGap;
    float bx = cx - row_w * 0.5f;

    auto button = [&](float rx, const char *glyph, astralia::ui::Box &out) {
        node_add_rrect(content, rx, ty, bs, bs, bs * 0.5f, 0.0f, cmod(astralia::palette::field_bg, ca), kNodeTransparent);
        const Texture *g =
            tc_icon(st, glyph, static_cast<int>(bs * 0.5f), scale);
        if (g)
            node_add_texture_rect(content, rnd(rx + (bs - px_w(g)) * 0.5f), rnd(ty + (bs - px_h(g)) * 0.5f), px_w(g), px_h(g), *g, cmod(astralia::palette::text, ca));
        out = {rx, ty, bs, bs};
    };
    button(bx, astralia::icon::player_prev, los.media_prev);
    button(bx + bs + kLockMediaBtnGap, m.playback == astralia::MediaPlayback::playing ? astralia::icon::player_pause : astralia::icon::player_play, los.media_play);
    button(bx + 2.0f * (bs + kLockMediaBtnGap), astralia::icon::player_next, los.media_next);
}

void draw_resources(LockState &st, Node *content, const LockRect &col, int32_t scale, float ca) {
    float pad = kLockSidePanelPad;
    float gap = kLockResTileGap;

    LockCard cc = draw_card(st, content, col, "Resources", scale, ca);
    float avail_h = col.y + col.h - pad - cc.y;

    const astralia::SystemStatsState &s = st.app->system_stats;
    const astralia::CpuTempState &ct = st.app->cpu_temp;
    const astralia::GpuTempState &gt = st.app->gpu_temp;
    bool show_gpu = astralia::gpu_stats_available(gt);

    struct Tile {
        const char *glyph;
        std::string label;
        float frac;
        int pct;
        const astralia::Color *accent;
        const astralia::Color *label_color;
        int row;
    };
    auto temp_label = [](const char *base, float celsius, const astralia::Color *&label_color) {
        if (celsius <= 0.0f)
            return std::string(base);
        int c = static_cast<int>(std::lround(celsius));
        label_color = celsius >= kLockResTempWarnC ? &astralia::palette::critical : &astralia::palette::text_dim;
        return std::string(base) + " - " + std::to_string(c) + "\xC2\xB0"
                                                               "C";
    };
    std::vector<Tile> tiles;
    const astralia::Color *cpu_label_color = &astralia::palette::text_dim;
    std::string cpu_label = temp_label("CPU", ct.celsius, cpu_label_color);
    tiles.push_back({astralia::icon::cpu, cpu_label, std::max(0.0f, s.cpu_usage), s.cpu_usage >= 0.0f ? static_cast<int>(std::lround(s.cpu_usage * 100.0f)) : -1, &astralia::palette::accent, cpu_label_color, 0});
    if (show_gpu) {
        const astralia::Color *gpu_label_color = &astralia::palette::text_dim;
        std::string gpu_label = temp_label("GPU", gt.celsius, gpu_label_color);
        tiles.push_back({astralia::icon::gpu, gpu_label, std::clamp(gt.usage_percent / 100.0f, 0.0f, 1.0f), static_cast<int>(std::lround(gt.usage_percent)), &kLockResGaugeGpuColor, gpu_label_color, 0});
    }
    tiles.push_back({astralia::icon::device_desktop, "RAM", std::max(0.0f, s.mem_usage), s.mem_usage >= 0.0f ? static_cast<int>(std::lround(s.mem_usage * 100.0f)) : -1, &astralia::palette::lavender, &astralia::palette::text_dim, 1});
    tiles.push_back({astralia::icon::folder, "DISK", std::max(0.0f, s.disk_pct / 100.0f), s.disk_pct >= 0.0f ? static_cast<int>(std::lround(s.disk_pct)) : -1, &astralia::palette::accent_alt, &astralia::palette::text_dim, 1});

    const Texture *label_probe =
        tc_text(st, "CPU", static_cast<int>(kLockFontMono), false, scale);
    float cell_extra = px_h(label_probe) + kLockResGaugeLabelGap;

    float tile_sz = std::min((col.w - 3.0f * gap) * 0.5f, (avail_h - gap - 2.0f * cell_extra) * 0.5f);
    float stroke = tile_sz * kLockResGaugeStrokeRatio;

    float col_gap = (col.w - 2.0f * tile_sz) / 3.0f;
    float block_h = 2.0f * tile_sz + gap + 2.0f * cell_extra;
    float y0 = cc.y + std::max(0.0f, (avail_h - block_h) * 0.5f);
    float row_y[2] = {y0, y0 + tile_sz + cell_extra + gap};

    int row_count[2] = {0, 0};
    for (const Tile &t : tiles)
        ++row_count[t.row];
    int row_seen[2] = {0, 0};

    for (const Tile &t : tiles) {
        int within = row_seen[t.row]++;
        float tx = row_count[t.row] == 1 ? col.x + (col.w - tile_sz) * 0.5f : (within == 0 ? col.x + col_gap : col.x + col.w - col_gap - tile_sz);
        float ty = row_y[t.row];

        float frac = std::clamp(t.frac, 0.0f, 1.0f);
        astralia::Color arc_color = astralia::with_alpha(*t.accent, t.accent->a * ca);
        const Texture *ic =
            tc_icon(st, t.glyph, static_cast<int>(kLockResIconSize), scale);
        const Texture *vt =
            tc_text(st, t.pct >= 0 ? std::to_string(t.pct) + "%" : "--", static_cast<int>(kLockResValueFont), true, scale);
        const Texture *lt = tc_text(st, t.label, static_cast<int>(kLockFontMono), false, scale);
        draw_arc_gauge(content, st.tcache, scale, tx, ty, tile_sz, stroke, frac, arc_color, ic, cmod(*t.accent, ca), vt, cmod(astralia::palette::text, ca), lt, cmod(*t.label_color, ca), kLockResGaugeIconValueGap, kLockResGaugeLabelGap);
    }
}

void draw_notifs(LockState &st, Node *content, const LockRect &col, int32_t scale, float ca) {
    if (col.h < 60.0f)
        return;

    float pad = kLockSidePanelPad;
    const std::vector<astralia::Notification> recs(st.app->notifications->list().rbegin(), st.app->notifications->list().rend());

    std::string hdr = recs.empty() ? "Notifications" : std::to_string(recs.size()) + (recs.size() == 1 ? " notification" : " notifications");
    LockCard cc = draw_card(st, content, col, hdr, scale, ca);
    float x = cc.x;
    float y = cc.y;

    if (recs.empty()) {
        const Texture *nt =
            tc_text(st, "No Notifications", static_cast<int>(kLockFontNormal), false, scale);
        if (nt)
            node_add_texture_rect(content, rnd(col.x + (col.w - px_w(nt)) * 0.5f), rnd(col.y + col.h * 0.5f), px_w(nt), px_h(nt), *nt, cmod(astralia::palette::text_dim, ca));
        return;
    }

    float clip_h = col.y + col.h - y - pad;
    Node *clip = node_add_group(content, x, y, cc.w, clip_h, true);
    float cw = col.w - 2.0f * pad;
    float cardpad = kLockNotifCardPad;
    size_t maxc = static_cast<size_t>(std::max(1.0f, (cw - 2.0f * cardpad) / (kLockFontMono * 0.62f)));

    float cy = 0.0f;
    int shown = 0;
    for (const astralia::Notification &rec : recs) {
        if (shown >= kLockNotifMaxCards || cy >= clip_h)
            break;
        const Texture *appt = tc_text(st, elide(rec.app.empty() ? "Notification" : rec.app, maxc), static_cast<int>(kLockFontMono), true, scale);
        const Texture *sumt =
            rec.summary.empty()
                ? nullptr
                : tc_text(st, elide(rec.summary, maxc), static_cast<int>(kLockFontNormal), false, scale);
        const Texture *bodyt =
            rec.body.empty()
                ? nullptr
                : tc_text(st, elide(rec.body, maxc), static_cast<int>(kLockFontMono), false, scale);
        float ch = 2.0f * cardpad + px_h(appt) + (sumt ? kLockMediaTextGap + px_h(sumt) : 0.0f) + (bodyt ? kLockMediaTextGap + px_h(bodyt) : 0.0f);

        node_add_rrect(clip, 0.0f, cy, cw, ch, kLockNotifCardRadius, 0.0f, cmod(astralia::palette::field_bg, ca), kNodeTransparent);
        float ix = cardpad;
        float iy = cy + cardpad;
        if (appt) {
            node_add_texture_rect(clip, rnd(ix), rnd(iy), px_w(appt), px_h(appt), *appt, cmod(astralia::palette::accent, ca));
            iy += px_h(appt) + kLockMediaTextGap;
        }
        if (sumt) {
            node_add_texture_rect(clip, rnd(ix), rnd(iy), px_w(sumt), px_h(sumt), *sumt, cmod(astralia::palette::text, ca));
            iy += px_h(sumt) + kLockMediaTextGap;
        }
        if (bodyt)
            node_add_texture_rect(clip, rnd(ix), rnd(iy), px_w(bodyt), px_h(bodyt), *bodyt, cmod(astralia::palette::text_muted, ca));

        cy += ch + kLockNotifCardGap;
        ++shown;
    }
}

const char *battery_glyph(const astralia::BatteryStatus &u) {
    if (u.full)
        return astralia::icon::plugged_in;
    if (u.charging)
        return astralia::icon::battery_charging;
    if (u.percent <= 25)
        return astralia::icon::battery1;
    if (u.percent <= 50)
        return astralia::icon::battery2;
    if (u.percent <= 75)
        return astralia::icon::battery3;
    return astralia::icon::battery4;
}

float draw_battery(LockState &st, Node *content, const LockRect &col, int32_t scale, float ca) {
    if (!st.app || !st.app->battery->status().present)
        return 0.0f;

    const astralia::BatteryStatus &u = st.app->battery->status();
    int fpx = static_cast<int>(kLockFontNormal);

    const Texture *title_t = tc_text(st, "Battery", fpx, true, scale);
    const Texture *icon_t = tc_icon(st, battery_glyph(u), fpx, scale);
    std::string label =
        u.full ? "Plugged in" : (std::to_string(u.percent) + "%");
    const Texture *label_t =
        tc_text(st, "Battery  " + label, fpx, false, scale);

    float row_h = std::max(px_h(icon_t), px_h(label_t));
    float card_h = kLockSidePanelPad + px_h(title_t) + kLockCardHeaderGap + row_h + kLockBatteryRowGap + kLockBatteryBarHeight + kLockSidePanelPad;

    LockCard cc = draw_card(st, content, {col.x, col.y, col.w, card_h}, "Battery", scale, ca);
    float x = cc.x;
    float y = cc.y;
    float content_w = cc.w;

    float rx = x;
    if (icon_t) {
        node_add_texture_rect(content, rnd(rx), rnd(y + (row_h - px_h(icon_t)) * 0.5f), px_w(icon_t), px_h(icon_t), *icon_t, cmod(astralia::palette::text, ca));
        rx += px_w(icon_t) + kLockBatteryIconGap;
    }
    if (label_t)
        node_add_texture_rect(content, rnd(rx), rnd(y + (row_h - px_h(label_t)) * 0.5f), px_w(label_t), px_h(label_t), *label_t, cmod(astralia::palette::text, ca));
    y += row_h + kLockBatteryRowGap;

    node_add_rrect(content, rnd(x), rnd(y), content_w, kLockBatteryBarHeight, kLockBatteryBarRadius, 0.0f, cmod(astralia::palette::text_alpha11, ca), kNodeTransparent);
    float fill_w = content_w * std::clamp(u.percent / 100.0f, 0.0f, 1.0f);
    if (fill_w > 0.0f)
        node_add_rrect(content, rnd(x), rnd(y), fill_w, kLockBatteryBarHeight, kLockBatteryBarRadius, 0.0f, cmod(astralia::palette::accent, ca), kNodeTransparent);

    return card_h;
}

void draw_left_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca) {
    float bat_h = draw_battery(st, content, col, scale, ca);
    float top = col.y + (bat_h > 0.0f ? bat_h + kLockPanelGap : 0.0f);
    float rem = col.h - (top - col.y);
    float ch = std::max(0.0f, (rem - kLockPanelGap) * 0.5f);
    LockRect fetch{col.x, top, col.w, ch};
    LockRect media{col.x, top + ch + kLockPanelGap, col.w, ch};
    draw_fetch(st, content, fetch, scale, ca);
    draw_media(st, los, content, media, scale, ca);
}

void draw_right_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca) {
    (void)los;
    float ch = lock_side_card_height(col.h);
    LockRect res{col.x, col.y, col.w, ch};
    LockRect notif{col.x, col.y + ch + kLockPanelGap, col.w, ch};
    draw_resources(st, content, res, scale, ca);
    draw_notifs(st, content, notif, scale, ca);
}

void draw_center_column(LockState &st, LockOutputSurface &los, Node *content, const LockRect &col, int32_t scale, float ca) {
    draw_card(st, content, col, "", scale, ca);

    float oh = static_cast<float>(los.height);
    float cscale = lock_center_scale(oh);
    int clock_px = std::max(1, static_cast<int>(kLockFontClock * cscale));

    const Texture *ht = tc_text(st, hour_string(), clock_px, true, scale);
    const Texture *colon_t = tc_text(st, ":", clock_px, true, scale);
    const Texture *mt = tc_text(st, minute_string(), clock_px, true, scale);
    const Texture *dt = tc_text(st, date_string(), static_cast<int>(kLockFontDate), true, scale);

    float clock_h = std::max(px_h(ht), px_h(mt));
    float clock_w = px_w(ht) + kLockClockGap + px_w(colon_t) +
                    kLockClockGap + px_w(mt);
    float date_h = px_h(dt);

    float total_h = lock_content_height(clock_h, date_h, 0.0f);
    float y = col.y + (col.h - total_h) * 0.5f;
    float cx = col.x + col.w * 0.5f;

    float mx = cx - clock_w * 0.5f;
    if (ht) {
        node_add_texture_rect(content, rnd(mx), rnd(y + clock_h - px_h(ht)), px_w(ht), px_h(ht), *ht, cmod(astralia::palette::accent, ca));
        mx += px_w(ht) + kLockClockGap;
    }
    if (colon_t) {
        node_add_texture_rect(content, rnd(mx), rnd(y + clock_h - px_h(colon_t)), px_w(colon_t), px_h(colon_t), *colon_t, cmod(astralia::palette::text, ca));
        mx += px_w(colon_t) + kLockClockGap;
    }
    if (mt)
        node_add_texture_rect(content, rnd(mx), rnd(y + clock_h - px_h(mt)), px_w(mt), px_h(mt), *mt, cmod(astralia::palette::lavender, ca));
    y += clock_h + kLockGapClockDate;

    if (dt)
        node_add_texture_rect(content, rnd(cx - px_w(dt) * 0.5f), rnd(y), px_w(dt), px_h(dt), *dt, cmod(astralia::palette::text, ca));
    y += date_h + kLockGapDateAvatar;

    float ax = cx - kLockProfileSize * 0.5f;
    st.avatar.style.ring_fill = astralia::rgba(astralia::palette::field_bg);
    st.avatar.style.border_color = astralia::rgba(astralia::palette::accent);
    animated_image_draw(st.avatar, content, rnd(ax), rnd(y), kLockProfileSize, kLockProfileSize, ca);
    y += kLockProfileSize + kLockGapAvatarInput;

    float pill_w = col.w * kLockInputWidthFrac;
    draw_pill(st, los, content, cx - pill_w * 0.5f, y, pill_w, scale, ca);
}

} // namespace

void lock_card_build(LockState &st, LockOutputSurface &los, Node *root) {
    int32_t scale = los.output_scale.scale;
    float ow = static_cast<float>(los.width);
    float oh = static_cast<float>(los.height);

    color_pool().clear();

    const char *glyph = st.unlocking ? astralia::icon::lock_open : astralia::icon::lock;
    const Texture *icon_tex =
        tc_icon(st, glyph, static_cast<int>(kLockFontIcon), scale);

    float card_w = lock_card_width(oh);
    float card_h = lock_card_height(oh);

    float pw = los.panel_w;
    float ph = los.panel_h;
    float px, py;
    lock_panel_origin(ow, oh, pw, ph, px, py);

    Node *panel = node_add_rrect(root, px, py, pw, ph, kLockCardRadius, kLockBgBorderWidth, astralia::rgba(astralia::palette::overlay), astralia::rgba(astralia::palette::accent));
    panel->rotation = los.panel_rotation;
    panel->scale = los.panel_scale;
    panel->clip_children = true;

    if (icon_tex && los.icon_alpha > 0.001f) {
        float iw = px_w(icon_tex), ih = px_h(icon_tex);
        node_add_texture_rect(panel, rnd((pw - iw) * 0.5f), rnd((ph - ih) * 0.5f), iw, ih, *icon_tex, cmod(astralia::palette::accent, los.icon_alpha));
    }

    if (los.content_alpha <= 0.001f) {
        los.media_prev = los.media_play = los.media_next = los.pill_button = {};
        return;
    }

    float ca = los.content_alpha;
    float center_w = kLockCenterWidth * lock_center_scale(oh);
    LockRect left, center, right;
    lock_columns(card_w, card_h, center_w, left, center, right);

    Node *content = node_add_group(panel, (pw - card_w) * 0.5f, (ph - card_h) * 0.5f, card_w, card_h);
    content->scale = los.content_scale;

    draw_left_column(st, los, content, left, scale, ca);
    draw_center_column(st, los, content, center, scale, ca);
    draw_right_column(st, los, content, right, scale, ca);

    if (los.content_scale < 0.99f) {
        los.media_prev = los.media_play = los.media_next = los.pill_button = {};
    } else {
        astralia::ui::Box *rects[] = {&los.media_prev, &los.media_play, &los.media_next, &los.pill_button};
        for (astralia::ui::Box *rp : rects)
            if (rp->w > 0.0f) {
                rp->x += px;
                rp->y += py;
            }
    }
}
