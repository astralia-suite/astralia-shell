//! The raised card look: a drop shadow, an outline, a light rim and the fill.

use super::canvas::Canvas;
use super::geometry::Rect;
use super::theme::palette;
use super::tokens::{Color, metrics, with_alpha};

#[derive(Clone, Copy, Debug)]
pub struct StickerStyle {
    pub outline: Color,
    pub border_width: f32,
    /// Drop shadow depth; the face shrinks by this much at the bottom.
    pub shadow: f32,
    pub shadow_color: Option<Color>,
    pub shadow_dx: Option<f32>,
    /// Rim width between outline and fill; by default thick borders get a wider rim.
    pub ring: Option<f32>,
}

impl Default for StickerStyle {
    fn default() -> Self {
        StickerStyle {
            outline: palette().border_outer,
            border_width: metrics::BORDER_THICK,
            shadow: metrics::SHADOW_OFFSET,
            shadow_color: None,
            shadow_dx: None,
            ring: None,
        }
    }
}

pub fn sticker(canvas: &mut dyn Canvas, r: Rect, radius: f32, fill: Color, s: &StickerStyle) {
    let p = palette();
    let dx = s.shadow_dx.unwrap_or(0.0);
    let w = (r.w - dx).max(0.0);
    let h = (r.h - s.shadow).max(0.0);
    let radius = radius.min(w.min(h) / 2.0);
    if s.shadow > 0.0 {
        let shade = s.shadow_color.unwrap_or(with_alpha(p.shadow, p.shadow.a * s.outline.a));
        canvas.rounded(Rect { x: r.x + dx, y: r.y + s.shadow, w, h }, radius, shade, 0.0, Color::default());
    }
    let mut band = s.ring.unwrap_or(if s.border_width >= metrics::BORDER_THICK {
        metrics::RING_WIDTH
    } else {
        metrics::RING_THIN
    });
    if s.ring.is_none() && w.min(h) - 2.0 * (s.border_width + band) < metrics::RING_MIN_INNER {
        band = 0.0;
    }
    let rim = with_alpha(p.border_inner, p.border_inner.a * s.outline.a);
    let face = Rect { x: r.x, y: r.y, w, h };
    if band <= 0.0 || fill == rim {
        canvas.rounded(face, radius, fill, s.border_width, s.outline);
        return;
    }
    canvas.rounded(face, radius, rim, s.border_width, s.outline);
    let inset = s.border_width + band;
    let inner = Rect { x: r.x + inset, y: r.y + inset, w: (w - 2.0 * inset).max(0.0), h: (h - 2.0 * inset).max(0.0) };
    canvas.rounded(inner, (radius - inset).max(0.0), fill, 0.0, Color::default());
}

/// A pressable chip: raised in `idle` (or the hover colour), pressed flat into the accent when selected.
#[allow(clippy::too_many_arguments)]
pub fn selectable(canvas: &mut dyn Canvas, r: Rect, radius: f32, selected: bool, hovered: bool, idle: Color, alpha: f32) {
    let p = palette();
    let outline = with_alpha(p.border_outer, p.border_outer.a * alpha);
    if selected {
        let s = metrics::SHADOW_WIDGET;
        let pressed = Rect { x: r.x + s, y: r.y + s, w: (r.w - s).max(0.0), h: (r.h - s).max(0.0) };
        let style = StickerStyle { outline, border_width: metrics::BORDER_THIN, shadow: 0.0, ..StickerStyle::default() };
        sticker(canvas, pressed, radius, with_alpha(p.accent, p.accent.a * alpha), &style);
        return;
    }
    let face = if hovered { p.pop_a } else { idle };
    let style = StickerStyle { outline, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() };
    sticker(canvas, r, radius, with_alpha(face, face.a * alpha), &style);
}
