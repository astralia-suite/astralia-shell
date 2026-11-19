//! The current player: album art, scrolling title and artist, position, and transport buttons.

use std::rc::Rc;
use std::time::Duration;

use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::panel_set::PanelAnchor;
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::marquee::{MarqueeTextState, draw_marquee_text};
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics};
use crate::service::media::{MediaPlayback, MediaService, media_format_position, media_is_local_art};

const PREVIOUS: i32 = 1;
const PLAY_PAUSE: i32 = 2;
const NEXT: i32 = 3;
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };

fn flat_outline() -> StickerStyle {
    StickerStyle { outline: palette().border_outer, border_width: metrics::BORDER_THIN, shadow: 0.0, ..StickerStyle::default() }
}

fn button(canvas: &mut dyn Canvas, paint: &mut PanelPaint, x: f32, y: f32, size: f32, glyph: &str, action: i32) {
    let p = palette();
    let r = Rect { x, y, w: size, h: size };
    sticker(canvas, r, size / 2.0, p.accent_container, &flat_outline());
    let extent = canvas.measure(glyph, &ICON_STYLE);
    canvas.text(glyph, &ICON_STYLE, x + (size - extent.w) / 2.0, y + (size - extent.h) / 2.0, p.text);
    paint.region(r, action);
}

pub struct MediaPanel {
    base: ContentBase,
    media: Rc<MediaService>,
    title_marquee: MarqueeTextState,
    artist_marquee: MarqueeTextState,
}

impl MediaPanel {
    pub fn new(media: Rc<MediaService>) -> MediaPanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        media.changed.connect(move |_| notify());
        MediaPanel { base, media, title_marquee: MarqueeTextState::default(), artist_marquee: MarqueeTextState::default() }
    }
}

impl PanelContent for MediaPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Media"
    }
    fn tint(&self) -> Color {
        palette().pop_a
    }
    fn anchor(&self) -> PanelAnchor {
        PanelAnchor::Center
    }
    fn opened(&mut self) {
        self.media.poll_position();
    }
    fn refresh_interval(&self) -> Duration {
        Duration::from_millis(cfg::MEDIA_POLL_MS as u64)
    }
    fn refresh(&mut self) {
        self.media.poll_position();
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        cfg::MEDIA_THUMB + cfg::MEDIA_PROGRESS_TOP + cfg::MEDIA_PROGRESS_ROW + cfg::MEDIA_CONTROLS_TOP + cfg::MEDIA_CONTROLS_ROW
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, _scroll: f32, paint: &mut PanelPaint) {
        let p = palette();
        let status = self.media.status().clone();
        let thumb = cfg::MEDIA_THUMB;
        let art = Rect { x: view.x, y: view.y, w: thumb, h: thumb };
        sticker(canvas, art, metrics::RADIUS_WIDGET, p.surface, &flat_outline());
        let local = status.track.art_url.strip_prefix("file://").filter(|_| status.has_player && media_is_local_art(&status.track.art_url));
        let image = local.map_or(NO_IMAGE, |path| canvas.thumbnail(path, thumb as i32));
        if image != NO_IMAGE {
            let size = canvas.image_size(image);
            if size.w > 0.0 && size.h > 0.0 {
                let scale = (thumb / size.w).max(thumb / size.h);
                canvas.begin_group(art, GroupOptions { clip: true, ..GroupOptions::default() });
                canvas.draw_image(image, Rect { x: (thumb - size.w * scale) / 2.0, y: (thumb - size.h * scale) / 2.0, w: size.w * scale, h: size.h * scale }, WHITE);
                canvas.end_group();
            }
        } else {
            widgets::centered_text(canvas, icons::MUSIC_NOTE, &ICON_STYLE, art, p.text_dim);
        }

        let text_x = view.x + thumb + cfg::MEDIA_TITLE_LEFT;
        let text_w = (view.w - thumb - cfg::MEDIA_TITLE_LEFT).max(20.0);
        let title = match (status.has_player, status.track.title.is_empty()) {
            (false, _) => "No player",
            (true, true) => "Unknown",
            (true, false) => status.track.title.as_str(),
        };
        let artist = if status.has_player { status.track.artist.as_str() } else { "" };
        let middle = view.y + thumb / 2.0;
        let title_size = canvas.measure(title, &TEXT_STYLE);
        let am = &mut self.base.animations;
        draw_marquee_text(canvas, am, &mut self.title_marquee, title, TEXT_STYLE, text_x, middle - title_size.h - cfg::MEDIA_TITLE_SPACING / 2.0, text_w, p.text);
        draw_marquee_text(canvas, am, &mut self.artist_marquee, artist, TEXT_STYLE, text_x, middle + cfg::MEDIA_TITLE_SPACING / 2.0, text_w, p.text_dim);

        let progress_y = view.y + thumb + cfg::MEDIA_PROGRESS_TOP;
        if status.has_player {
            let label = format!("{} / {}", media_format_position(status.track.position_us), media_format_position(status.track.length_us));
            widgets::centered_text(canvas, &label, &TEXT_STYLE, Rect { x: view.x, y: progress_y, w: view.w, h: cfg::MEDIA_PROGRESS_ROW }, p.text_dim);
        }

        let controls_y = progress_y + cfg::MEDIA_PROGRESS_ROW + cfg::MEDIA_CONTROLS_TOP;
        let (side, play, spacing) = (cfg::MEDIA_SIDE_BUTTON, cfg::MEDIA_PLAY_BUTTON, cfg::MEDIA_CONTROLS_SPACING);
        let mut x = view.x + (view.w - 2.0 * side - play - 2.0 * spacing) / 2.0;
        button(canvas, paint, x, controls_y + (cfg::MEDIA_CONTROLS_ROW - side) / 2.0, side, icons::PLAYER_PREV, PREVIOUS);
        x += side + spacing;
        let glyph = if status.playback == MediaPlayback::Playing { icons::PLAYER_PAUSE } else { icons::PLAYER_PLAY };
        button(canvas, paint, x, controls_y + (cfg::MEDIA_CONTROLS_ROW - play) / 2.0, play, glyph, PLAY_PAUSE);
        x += play + spacing;
        button(canvas, paint, x, controls_y + (cfg::MEDIA_CONTROLS_ROW - side) / 2.0, side, icons::PLAYER_NEXT, NEXT);
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        match region.id {
            PREVIOUS => self.media.previous(),
            PLAY_PAUSE => self.media.play_pause(),
            NEXT => self.media.next(),
            _ => {}
        }
        true
    }
    fn scrollable(&self) -> bool {
        false
    }
}
