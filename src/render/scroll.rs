//! The scroll panel: paper between two rollers, unrolled from the centre (or the top) as it opens.

use super::canvas::Canvas;
use super::geometry::Rect;
use super::sticker::{StickerStyle, sticker};
use super::theme::palette;
use super::tokens::{Color, WHITE, lerp_color, metrics};

const CAP_FLARE: f32 = 2.0;
const HIGHLIGHT_MIX: f32 = 0.35;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum ScrollAxis {
    #[default]
    Horizontal,
    Vertical,
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[allow(clippy::enum_variant_names)]
pub enum ScrollEntry {
    FromLeft,
    FromRight,
    FromTop,
    FromBottom,
}

#[derive(Clone, Copy, Debug)]
pub struct ScrollMetrics {
    pub roller_w: f32,
    pub overhang: f32,
    pub cap_h: f32,
    pub rounding: f32,
    pub band: f32,
    pub axis: ScrollAxis,
    pub gap: f32,
}

impl Default for ScrollMetrics {
    fn default() -> Self {
        ScrollMetrics {
            roller_w: 18.0,
            overhang: 10.0,
            cap_h: 8.0,
            rounding: metrics::RADIUS_CARD,
            band: 4.0,
            axis: ScrollAxis::Horizontal,
            gap: 6.0,
        }
    }
}

impl ScrollMetrics {
    /// How far a roller sits out from the paper edge when open.
    pub fn reach(&self) -> f32 {
        self.roller_w + self.gap
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ScrollGeometry {
    pub paper: Rect,
    pub first_roller: Rect,
    pub second_roller: Rect,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ScrollOffset {
    pub dx: f32,
    pub dy: f32,
}

fn outlined(border_width: f32) -> StickerStyle {
    StickerStyle { outline: palette().border_outer, border_width, shadow: 0.0, ..StickerStyle::default() }
}

fn paint_roller(canvas: &mut dyn Canvas, roller: Rect, m: &ScrollMetrics) {
    let p = palette();
    let body_w = roller.w;
    let body_h = (roller.h - metrics::SHADOW_OFFSET).max(0.0);
    sticker(canvas, roller, m.roller_w * 0.35, p.accent, &StickerStyle::default());
    let highlight = lerp_color(p.accent, WHITE, HIGHLIGHT_MIX);
    let cap = outlined(metrics::BORDER_THIN);
    if m.axis == ScrollAxis::Horizontal {
        let cap_h = m.cap_h.min(body_h / 2.0);
        let cap_w = body_w + CAP_FLARE * 2.0;
        let cap_x = roller.x - CAP_FLARE;
        sticker(canvas, Rect { x: cap_x, y: roller.y, w: cap_w, h: cap_h }, cap_h / 2.0, p.accent_alt, &cap);
        let bottom = Rect { x: cap_x, y: roller.y + body_h - cap_h, w: cap_w, h: cap_h };
        sticker(canvas, bottom, cap_h / 2.0, p.accent_alt, &cap);
        let strip_h = body_h - cap_h * 2.0 - 4.0;
        if strip_h > 0.0 {
            let strip = Rect { x: roller.x + body_w * 0.22, y: roller.y + cap_h + 2.0, w: body_w * 0.18, h: strip_h };
            canvas.rounded(strip, body_w * 0.09, highlight, 0.0, Color::default());
        }
        return;
    }
    let cap_w = m.cap_h.min(body_w / 2.0);
    let cap_h = body_h + CAP_FLARE * 2.0;
    let cap_y = roller.y - CAP_FLARE;
    sticker(canvas, Rect { x: roller.x, y: cap_y, w: cap_w, h: cap_h }, cap_w / 2.0, p.accent_alt, &cap);
    let right = Rect { x: roller.x + body_w - cap_w, y: cap_y, w: cap_w, h: cap_h };
    sticker(canvas, right, cap_w / 2.0, p.accent_alt, &cap);
    let strip_w = body_w - cap_w * 2.0 - 4.0;
    if strip_w > 0.0 {
        let strip = Rect { x: roller.x + cap_w + 2.0, y: roller.y + body_h * 0.22, w: strip_w, h: body_h * 0.18 };
        canvas.rounded(strip, body_h * 0.09, highlight, 0.0, Color::default());
    }
}

/// Layout at `open` (clamped to `0..=1`): horizontal paper widens from the centre, vertical paper
/// grows down from the top roller; rollers move a further `gap` out as it opens.
pub fn scroll_geometry(panel: Rect, open: f32, m: &ScrollMetrics) -> ScrollGeometry {
    let t = open.clamp(0.0, 1.0);
    let gap = m.gap * t;
    if m.axis == ScrollAxis::Vertical {
        // Top roller stays put; only the paper and the bottom roller travel.
        let paper_h = panel.h * t;
        let left = panel.x - m.overhang;
        let width = panel.w + m.overhang * 2.0;
        return ScrollGeometry {
            paper: Rect { h: paper_h, ..panel },
            first_roller: Rect { x: left, y: panel.y - m.roller_w - m.gap, w: width, h: m.roller_w },
            second_roller: Rect { x: left, y: panel.y - m.gap + (panel.h + m.gap * 2.0) * t, w: width, h: m.roller_w },
        };
    }
    let cx = panel.x + panel.w / 2.0;
    let paper_w = panel.w * t;
    let top = panel.y - m.overhang;
    let height = panel.h + m.overhang * 2.0;
    let paper = Rect { x: cx - paper_w / 2.0, y: panel.y, w: paper_w, h: panel.h };
    ScrollGeometry {
        paper,
        first_roller: Rect { x: paper.x - m.roller_w - gap, y: top, w: m.roller_w, h: height },
        second_roller: Rect { x: paper.x + paper.w + gap, y: top, w: m.roller_w, h: height },
    }
}

/// The rectangle spanning both rollers.
pub fn scroll_bounds(g: &ScrollGeometry) -> Rect {
    let (a, b) = (g.first_roller, g.second_roller);
    let left = a.x.min(b.x);
    let top = a.y.min(b.y);
    let right = (a.x + a.w).max(b.x + b.w);
    let bottom = (a.y + a.h).max(b.y + b.h);
    Rect { x: left, y: top, w: right - left, h: bottom - top }
}

/// Offset that moves `closed` fully off the surface through `entry`'s edge at `slide` 1.
pub fn scroll_slide_offset(entry: ScrollEntry, slide: f32, closed: Rect, surface_w: f32, surface_h: f32) -> ScrollOffset {
    match entry {
        ScrollEntry::FromLeft => ScrollOffset { dx: -(closed.x + closed.w) * slide, dy: 0.0 },
        ScrollEntry::FromRight => ScrollOffset { dx: (surface_w - closed.x) * slide, dy: 0.0 },
        ScrollEntry::FromTop => ScrollOffset { dx: 0.0, dy: -(closed.y + closed.h) * slide },
        ScrollEntry::FromBottom => ScrollOffset { dx: 0.0, dy: (surface_h - closed.y) * slide },
    }
}

pub fn paint_scroll(canvas: &mut dyn Canvas, g: &ScrollGeometry, m: &ScrollMetrics) {
    let p = g.paper;
    if p.w.min(p.h) >= metrics::BORDER_THICK * 2.0 {
        let colours = palette();
        sticker(canvas, p, m.rounding, colours.accent_container, &StickerStyle::default());
        let inner = Rect {
            x: p.x + m.band,
            y: p.y + m.band,
            w: p.w - m.band * 2.0,
            h: p.h - metrics::SHADOW_OFFSET - m.band * 2.0,
        };
        if inner.w > 0.0 && inner.h > 0.0 {
            let style = StickerStyle { ring: Some(0.0), ..outlined(metrics::BORDER_THIN) };
            sticker(canvas, inner, (m.rounding - 2.0).max(0.0), colours.paper, &style);
        }
    }
    paint_roller(canvas, g.first_roller, m);
    paint_roller(canvas, g.second_roller, m);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    fn near(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-3
    }

    const PANEL: Rect = Rect { x: 100.0, y: 50.0, w: 400.0, h: 200.0 };

    fn rounded_count(g: &ScrollGeometry, m: &ScrollMetrics) -> usize {
        let mut c = RecordingCanvas::default();
        paint_scroll(&mut c, g, m);
        c.count(OpKind::Rounded)
    }

    #[test]
    fn scroll_horizontal() {
        let m = ScrollMetrics::default();
        let panel = PANEL;

        let closed = scroll_geometry(panel, 0.0, &m);
        assert!(closed.paper.w == 0.0 && near(closed.first_roller.x + m.roller_w, closed.second_roller.x));
        assert!(near(closed.second_roller.x, panel.x + panel.w / 2.0));
        assert!(near(closed.first_roller.h, panel.h + m.overhang * 2.0) && near(closed.first_roller.y, panel.y - m.overhang));

        let open = scroll_geometry(panel, 1.0, &m);
        assert_eq!(open.paper, panel);
        assert!(near(open.first_roller.x + m.roller_w + m.gap, panel.x) && near(open.second_roller.x, panel.x + panel.w + m.gap));

        let mut last = -1.0;
        for i in 0..=10 {
            let t = i as f32 / 10.0;
            let g = scroll_geometry(panel, t, &m);
            assert!(g.paper.w >= last, "paper widens");
            last = g.paper.w;
            assert!(near(g.paper.x + g.paper.w / 2.0, panel.x + panel.w / 2.0));
            assert!(near(g.first_roller.x + m.roller_w + g.paper.w + 2.0 * m.gap * t, g.second_roller.x));
        }

        assert!(scroll_geometry(panel, -1.0, &m).paper.w == 0.0 && scroll_geometry(panel, 2.0, &m).paper == panel);
        assert!(rounded_count(&closed, &m) < rounded_count(&open, &m), "paper drawn only once it has width");
    }

    #[test]
    fn scroll_vertical() {
        let m = ScrollMetrics { axis: ScrollAxis::Vertical, roller_w: 14.0, overhang: 6.0, ..ScrollMetrics::default() };
        let panel = PANEL;

        let closed = scroll_geometry(panel, 0.0, &m);
        assert!(closed.paper.h == 0.0 && near(closed.first_roller.y + closed.first_roller.h, closed.second_roller.y));
        assert!(near(closed.first_roller.y, panel.y - m.roller_w - m.gap) && near(closed.first_roller.w, panel.w + m.overhang * 2.0));

        let open = scroll_geometry(panel, 1.0, &m);
        assert!(open.paper == panel && near(open.second_roller.y, panel.y + panel.h + m.gap));

        let mut last = -1.0;
        for i in 0..=10 {
            let t = i as f32 / 10.0;
            let g = scroll_geometry(panel, t, &m);
            assert!(g.paper.h >= last, "paper grows downward");
            last = g.paper.h;
            assert!(near(g.first_roller.y, panel.y - m.roller_w - m.gap) && near(g.paper.y, panel.y), "top roller stays put");
        }
        assert!(rounded_count(&closed, &m) < rounded_count(&open, &m), "paper drawn only once it has height");
    }

    #[test]
    fn scroll_offset() {
        let panel = Rect { x: 300.0, y: 100.0, w: 400.0, h: 200.0 };
        let (sw, sh) = (1000.0, 800.0);
        let entries = [ScrollEntry::FromLeft, ScrollEntry::FromRight, ScrollEntry::FromTop, ScrollEntry::FromBottom];
        for axis in [ScrollAxis::Horizontal, ScrollAxis::Vertical] {
            let m = ScrollMetrics { axis, ..ScrollMetrics::default() };
            let closed = scroll_bounds(&scroll_geometry(panel, 0.0, &m));
            for entry in entries {
                let none = scroll_slide_offset(entry, 0.0, closed, sw, sh);
                assert!(none.dx == 0.0 && none.dy == 0.0);
                let out = scroll_slide_offset(entry, 1.0, closed, sw, sh);
                let moved = Rect { x: closed.x + out.dx, y: closed.y + out.dy, ..closed };
                let gone = moved.x + moved.w <= 0.0 || moved.x >= sw || moved.y + moved.h <= 0.0 || moved.y >= sh;
                assert!(gone, "{entry:?} slides off the surface");
                let half = scroll_slide_offset(entry, 0.5, closed, sw, sh);
                assert!(near(half.dx * 2.0, out.dx) && near(half.dy * 2.0, out.dy));
            }
        }
        let closed = scroll_bounds(&scroll_geometry(panel, 0.0, &ScrollMetrics::default()));
        assert!(
            scroll_slide_offset(ScrollEntry::FromLeft, 1.0, closed, sw, sh).dx < 0.0
                && scroll_slide_offset(ScrollEntry::FromRight, 1.0, closed, sw, sh).dx > 0.0
        );
        assert!(
            scroll_slide_offset(ScrollEntry::FromTop, 1.0, closed, sw, sh).dy < 0.0
                && scroll_slide_offset(ScrollEntry::FromBottom, 1.0, closed, sw, sh).dy > 0.0
        );
    }
}
