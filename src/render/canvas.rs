//! The drawing surface every painter targets.

use super::geometry::{Point, Rect};
use super::tokens::Color;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum FontFamily {
    #[default]
    Text,
    Icon,
    Glyph,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TextStyle {
    pub family: FontFamily,
    pub px: i32,
    pub bold: bool,
    /// Wrap or ellipsis width in pixels; 0 means unbounded.
    pub max_width: i32,
    pub wrap: bool,
}

impl Default for TextStyle {
    fn default() -> Self {
        TextStyle { family: FontFamily::Text, px: 13, bold: false, max_width: 0, wrap: false }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct TextSize {
    pub w: f32,
    pub h: f32,
}

pub type ImageId = i32;

pub const NO_IMAGE: ImageId = -1;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GroupOptions {
    pub scale: f32,
    pub clip: bool,
    pub radius: f32,
    pub rotation: f32,
}

impl Default for GroupOptions {
    fn default() -> Self {
        GroupOptions { scale: 1.0, clip: false, radius: 0.0, rotation: 0.0 }
    }
}

pub trait Canvas {
    fn rect(&mut self, r: Rect, fill: Color);
    fn rounded(&mut self, r: Rect, radius: f32, fill: Color, border_width: f32, border: Color);
    /// A concave corner piece: `r` minus a quarter circle on its left or right side.
    fn fillet(&mut self, r: Rect, circle_on_right: bool, color: Color);
    /// While on, shapes clear what is under them instead of painting.
    fn set_erase(&mut self, on: bool);

    fn measure(&mut self, text: &str, style: &TextStyle) -> TextSize;
    fn advance(&mut self, style: &TextStyle) -> f32;
    fn text(&mut self, text: &str, style: &TextStyle, x: f32, y: f32, color: Color);

    fn image(&mut self, asset: &str, target_px: i32) -> ImageId;
    fn thumbnail(&mut self, path: &str, px: i32) -> ImageId;
    fn image_size(&mut self, id: ImageId) -> TextSize;
    fn draw_image(&mut self, id: ImageId, r: Rect, tint: Color);

    /// Starts drawing into a layer over `r`, composited (scaled, clipped, rotated) on `end_group`.
    fn begin_group(&mut self, r: Rect, options: GroupOptions);
    fn end_group(&mut self);

    fn set_opacity(&mut self, opacity: f32);

    /// A ring of segments lit up to `value` (0..1), `r.w` across with a `stroke` wide line.
    fn gauge(&mut self, _r: Rect, _stroke: f32, _value: f32, _color: Color) {}

    fn polygon(&mut self, _points: &[Point], _color: Color) {}
    fn cut_polygon(&mut self, _points: &[Point]) {}
    fn snapshot(&mut self) {}
    fn snapshot_polygon(&mut self, _from: &[Point], _to: &[Point], _alpha: f32) {}
}
