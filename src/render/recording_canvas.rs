//! A `Canvas` that records every call, for tests.

pub use super::canvas::{Canvas, FontFamily, GroupOptions, ImageId, TextSize, TextStyle};
pub use super::geometry::{Point, Rect};
pub use super::tokens::Color;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OpKind {
    Rect,
    Rounded,
    Fillet,
    Erase,
    Text,
    Image,
    BeginGroup,
    EndGroup,
    Opacity,
    Polygon,
    CutPolygon,
    Snapshot,
    SnapshotPolygon,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Op {
    pub kind: OpKind,
    pub rect: Rect,
    pub radius: f32,
    pub border_width: f32,
    pub text: String,
    pub color: Color,
    pub value: f32,
}

impl Op {
    fn new(kind: OpKind) -> Op {
        Op {
            kind,
            rect: Rect::default(),
            radius: 0.0,
            border_width: 0.0,
            text: String::new(),
            color: Color::default(),
            value: 0.0,
        }
    }
}

pub struct RecordingCanvas {
    pub ops: Vec<Op>,
    pub glyph_w: f32,
    pub glyph_h: f32,
    pub image_id: ImageId,
    pub thumbnails: Vec<(String, i32)>,
}

impl Default for RecordingCanvas {
    fn default() -> Self {
        RecordingCanvas { ops: Vec::new(), glyph_w: 9.0, glyph_h: 20.0, image_id: 0, thumbnails: Vec::new() }
    }
}

fn bounds(points: &[Point]) -> Rect {
    let Some(first) = points.first() else { return Rect::default() };
    let (mut x0, mut y0, mut x1, mut y1) = (first.x, first.y, first.x, first.y);
    for p in points {
        x0 = x0.min(p.x);
        y0 = y0.min(p.y);
        x1 = x1.max(p.x);
        y1 = y1.max(p.y);
    }
    Rect { x: x0, y: y0, w: x1 - x0, h: y1 - y0 }
}

impl RecordingCanvas {
    pub fn count(&self, kind: OpKind) -> usize {
        self.ops.iter().filter(|o| o.kind == kind).count()
    }

    fn push(&mut self, op: Op) {
        self.ops.push(op);
    }
}

impl Canvas for RecordingCanvas {
    fn rect(&mut self, r: Rect, fill: Color) {
        self.push(Op { rect: r, color: fill, ..Op::new(OpKind::Rect) });
    }
    fn rounded(&mut self, r: Rect, radius: f32, fill: Color, border_width: f32, _border: Color) {
        self.push(Op { rect: r, radius, border_width, color: fill, ..Op::new(OpKind::Rounded) });
    }
    fn fillet(&mut self, r: Rect, circle_on_right: bool, color: Color) {
        let value = if circle_on_right { 1.0 } else { 0.0 };
        self.push(Op { rect: r, color, value, ..Op::new(OpKind::Fillet) });
    }
    fn set_erase(&mut self, on: bool) {
        self.push(Op { value: if on { 1.0 } else { 0.0 }, ..Op::new(OpKind::Erase) });
    }
    fn measure(&mut self, text: &str, style: &TextStyle) -> TextSize {
        if text.is_empty() {
            return TextSize::default();
        }
        if style.family == FontFamily::Icon {
            return TextSize { w: style.px as f32, h: style.px as f32 };
        }
        TextSize { w: self.glyph_w * text.len() as f32, h: self.glyph_h }
    }
    fn advance(&mut self, _style: &TextStyle) -> f32 {
        self.glyph_w
    }
    fn text(&mut self, text: &str, _style: &TextStyle, x: f32, y: f32, color: Color) {
        let rect = Rect { x, y, w: 0.0, h: 0.0 };
        self.push(Op { rect, text: text.to_owned(), color, ..Op::new(OpKind::Text) });
    }
    fn image(&mut self, _asset: &str, _target_px: i32) -> ImageId {
        self.image_id
    }
    fn thumbnail(&mut self, path: &str, px: i32) -> ImageId {
        self.thumbnails.push((path.to_owned(), px));
        self.image_id
    }
    fn image_size(&mut self, _id: ImageId) -> TextSize {
        TextSize { w: 64.0, h: 64.0 }
    }
    fn draw_image(&mut self, _id: ImageId, r: Rect, tint: Color) {
        self.push(Op { rect: r, color: tint, ..Op::new(OpKind::Image) });
    }
    fn begin_group(&mut self, r: Rect, options: GroupOptions) {
        self.push(Op { rect: r, value: options.scale, ..Op::new(OpKind::BeginGroup) });
    }
    fn end_group(&mut self) {
        self.push(Op::new(OpKind::EndGroup));
    }
    fn set_opacity(&mut self, opacity: f32) {
        self.push(Op { value: opacity, ..Op::new(OpKind::Opacity) });
    }
    fn polygon(&mut self, points: &[Point], color: Color) {
        let (rect, value) = (bounds(points), points.len() as f32);
        self.push(Op { rect, color, value, ..Op::new(OpKind::Polygon) });
    }
    fn cut_polygon(&mut self, points: &[Point]) {
        let (rect, value) = (bounds(points), points.len() as f32);
        self.push(Op { rect, value, ..Op::new(OpKind::CutPolygon) });
    }
    fn snapshot(&mut self) {
        self.push(Op::new(OpKind::Snapshot));
    }
    fn snapshot_polygon(&mut self, _from: &[Point], to: &[Point], alpha: f32) {
        self.push(Op { rect: bounds(to), value: alpha, ..Op::new(OpKind::SnapshotPolygon) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recording_measures_like_a_grid() {
        let mut c = RecordingCanvas::default();
        let text = TextStyle::default();
        let icon = TextStyle { family: FontFamily::Icon, px: 18, ..TextStyle::default() };
        assert_eq!((c.measure("abc", &text).w, c.measure("abc", &text).h), (27.0, 20.0));
        assert_eq!(c.measure("", &text).w, 0.0);
        assert_eq!(c.measure("x", &icon).w, 18.0);
        c.rect(Rect::default(), Color::default());
        c.rounded(Rect::default(), 4.0, Color::default(), 0.0, Color::default());
        assert_eq!((c.count(OpKind::Rect), c.count(OpKind::Rounded)), (1, 1));
    }
}
