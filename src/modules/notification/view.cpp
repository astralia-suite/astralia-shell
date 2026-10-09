#include <algorithm>
#include <cmath>

#include "config/icons.h"
#include "config/notification_config.h"

#include "modules/notification/view.h"

#include "ui/tokens.h"

namespace astralia {

namespace {

namespace cfg = notification_config;

const Color &urgency_color(uint8_t urgency) {
    if (urgency == notification_urgency_critical) {
        return palette::critical;
    }
    if (urgency == 0) {
        return palette::text_muted;
    }
    return palette::accent;
}

void progress_bar(ui::Canvas &canvas, float y, float progress, const Color &color, float opacity) {
    constexpr float width = cfg::card_width;
    float rrect_h = 2.0f * cfg::card_radius;
    progress = std::clamp(progress, 0.0f, 1.0f);
    canvas.begin_group({0, y, width, cfg::progress_height}, {1.0f, true});
    canvas.rounded({0, 0, width, rrect_h}, cfg::card_radius, with_alpha(color, cfg::progress_track_opacity * opacity));
    canvas.end_group();
    float fill_x = width * (1.0f - progress);
    canvas.begin_group({fill_x, y, width * progress, cfg::progress_height}, {1.0f, true});
    canvas.rounded({-fill_x, 0, width, rrect_h}, cfg::card_radius, with_alpha(color, opacity));
    canvas.end_group();
}

} // namespace

void paint_notifications(ui::Canvas &canvas, const NotificationViewState &view, const NotificationLayout &layout) {
    ui::TextStyle app_style{ui::FontFamily::text, cfg::app_px, true, static_cast<int>(cfg::wrap_width)};
    ui::TextStyle summary_style{ui::FontFamily::text, cfg::summary_px, true, static_cast<int>(cfg::wrap_width), true};
    ui::TextStyle body_style{ui::FontFamily::text, cfg::body_px, false, static_cast<int>(cfg::wrap_width), true};
    ui::TextStyle close_style{ui::FontFamily::icon, cfg::close_icon_px};

    for (const NotificationCard &card : layout.cards) {
        const NotificationEntry &entry = *card.entry;
        float opacity = entry.opacity * view.local_opacity(entry.id);
        float y = card.y + entry.slide_offset;
        const Color &accent = urgency_color(entry.urgency);

        canvas.rounded({0, y, cfg::card_width, card.height}, cfg::card_radius, with_alpha(palette::overlay, opacity), cfg::border_width, with_alpha(accent, opacity));
        if (entry.timed) {
            progress_bar(canvas, y, entry.progress, accent, opacity);
        }

        float x = cfg::card_pad;
        float top = y + cfg::card_pad;
        canvas.rounded({x, top + (card.header - cfg::urgency_dot) / 2.0f, cfg::urgency_dot, cfg::urgency_dot}, cfg::urgency_dot / 2.0f, with_alpha(accent, opacity));
        std::string_view app = entry.app.empty() ? std::string_view(cfg::app_fallback) : std::string_view(entry.app);
        ui::TextSize app_size = canvas.measure(app, app_style);
        canvas.text(app, app_style, x + cfg::urgency_dot + cfg::header_spacing, top + (card.header - app_size.h) / 2.0f, with_alpha(palette::text, cfg::app_opacity * opacity));

        float row = top + card.header;
        if (card.summary > 0.0f) {
            row += cfg::content_spacing;
            canvas.text(entry.summary, summary_style, x, row, with_alpha(palette::text, opacity));
            row += card.summary;
        }
        if (card.body > 0.0f) {
            row += cfg::content_spacing;
            canvas.text(entry.body, body_style, x, row, with_alpha(palette::text, cfg::body_opacity * opacity));
        }

        ui::TextSize glyph = canvas.measure(icon::close, close_style);
        float close_x = cfg::card_width - cfg::close_hit;
        float close_opacity = opacity * (entry.id == view.hovered() ? 1.0f : cfg::close_idle_opacity);
        canvas.text(icon::close, close_style, std::round(close_x + (cfg::close_hit - glyph.w) / 2.0f), y + std::round((cfg::close_hit - glyph.h) / 2.0f), with_alpha(palette::text, close_opacity));
    }
}

} // namespace astralia
