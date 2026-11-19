//! Points and rectangles in logical pixels.

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

impl Rect {
    /// Half-open: the left and top edges are inside, the right and bottom edges are not.
    pub fn contains(&self, px: f64, py: f64) -> bool {
        let (x, y, w, h) = (self.x as f64, self.y as f64, self.w as f64, self.h as f64);
        px >= x && px < x + w && py >= y && py < y + h
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rect_contains_is_half_open() {
        let r = Rect { x: 10.0, y: 10.0, w: 5.0, h: 5.0 };
        assert!(r.contains(10.0, 10.0) && r.contains(14.9, 14.9));
        assert!(!r.contains(15.0, 12.0) && !r.contains(12.0, 15.0) && !r.contains(9.9, 12.0));
    }
}
