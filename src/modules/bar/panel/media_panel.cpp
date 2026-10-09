#include <algorithm>

#include "config/icons.h"

#include "modules/bar/panel/media_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { previous = 1,
              play_pause,
              next };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

void button(ui::Canvas &canvas, PanelPaint &paint, float x, float y, float size, const char *glyph, int action) {
    ui::Box box{x, y, size, size};
    canvas.rounded(box, size / 2.0f, palette::overlay);
    ui::TextSize extent = canvas.measure(glyph, icon_style);
    canvas.text(glyph, icon_style, x + (size - extent.w) / 2.0f, y + (size - extent.h) / 2.0f, palette::text);
    paint.region(box, action);
}

} // namespace

MediaPanel::MediaPanel(MediaService &media) : media_(media) {
    media_.changed.connect(notifier());
}

float MediaPanel::content_height(ui::Canvas &) {
    return cfg::media_thumb + cfg::media_progress_top + cfg::media_progress_row + cfg::media_controls_top + cfg::media_controls_row;
}

void MediaPanel::paint(ui::Canvas &canvas, const ui::Box &view, float, PanelPaint &paint) {
    const MediaStatus &status = media_.status();
    float thumb = cfg::media_thumb;
    ui::Box art{view.x, view.y, thumb, thumb};
    canvas.rounded(art, cfg::media_thumb_radius, palette::overlay);
    ui::ImageId image = status.has_player && media_is_local_art_url(status.track.art_url) ? canvas.image(status.track.art_url.substr(7), static_cast<int>(thumb)) : ui::no_image;
    if (image != ui::no_image) {
        ui::TextSize size = canvas.image_size(image);
        if (size.w > 0 && size.h > 0) {
            float scale = std::max(thumb / size.w, thumb / size.h);
            canvas.begin_group(art, {1.0f, true});
            canvas.draw_image(image, {(thumb - size.w * scale) / 2.0f, (thumb - size.h * scale) / 2.0f, size.w * scale, size.h * scale}, palette::text);
            canvas.end_group();
        }
    } else {
        panel_widgets::centered_text(canvas, icon::music_note, icon_style, art, palette::text_dim);
    }

    float text_x = view.x + thumb + cfg::media_title_left;
    float text_w = std::max(20.0f, view.w - thumb - cfg::media_title_left);
    std::string title = status.has_player ? status.track.title : "No player";
    if (title.empty()) {
        title = "Unknown";
    }
    std::string artist = status.has_player ? status.track.artist : "";
    float middle = view.y + thumb / 2.0f;
    ui::TextSize title_size = canvas.measure(title, text_style);
    draw_marquee_text(canvas, animations(), title_marquee_, title, text_style, text_x, middle - title_size.h - cfg::media_title_spacing / 2.0f, text_w, palette::text);
    draw_marquee_text(canvas, animations(), artist_marquee_, artist, text_style, text_x, middle + cfg::media_title_spacing / 2.0f, text_w, palette::text_dim);

    float progress_y = view.y + thumb + cfg::media_progress_top;
    if (status.has_player) {
        std::string label = media_format_position(status.track.position_us) + " / " + media_format_position(status.track.length_us);
        panel_widgets::centered_text(canvas, label, text_style, {view.x, progress_y, view.w, cfg::media_progress_row}, palette::text_dim);
    }

    float controls_y = progress_y + cfg::media_progress_row + cfg::media_controls_top;
    float side = cfg::media_side_button;
    float play = cfg::media_play_button;
    float spacing = cfg::media_controls_spacing;
    float x = view.x + (view.w - 2.0f * side - play - 2.0f * spacing) / 2.0f;
    button(canvas, paint, x, controls_y + (cfg::media_controls_row - side) / 2.0f, side, icon::player_prev, previous);
    x += side + spacing;
    button(canvas, paint, x, controls_y + (cfg::media_controls_row - play) / 2.0f, play, status.playback == MediaPlayback::playing ? icon::player_pause : icon::player_play, play_pause);
    x += play + spacing;
    button(canvas, paint, x, controls_y + (cfg::media_controls_row - side) / 2.0f, side, icon::player_next, next);
}

bool MediaPanel::activate(const PanelRegion &region, double, double) {
    switch (region.id) {
    case previous:
        media_.previous();
        break;
    case play_pause:
        media_.play_pause();
        break;
    case next:
        media_.next();
        break;
    default:
        break;
    }
    return true;
}

} // namespace astralia
