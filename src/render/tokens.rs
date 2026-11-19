//! Colour values and fixed layout metrics.

/// Straight (not premultiplied) sRGB colour, channels in `0..=1`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

impl Color {
    pub const fn rgba(self) -> [f32; 4] {
        [self.r, self.g, self.b, self.a]
    }
}

const fn digit(c: u8) -> u8 {
    if c <= b'9' { c - b'0' } else { (c | 0x20) - b'a' + 10 }
}

/// Parses `#RRGGBB` or `#RRGGBBAA`; the `#` is optional and case does not matter.
pub const fn color(hex: &str) -> Color {
    let h = hex.as_bytes();
    let i = if h[0] == b'#' { 1 } else { 0 };
    const fn byte(h: &[u8], at: usize) -> f32 {
        (digit(h[at]) as u32 * 16 + digit(h[at + 1]) as u32) as f32 / 255.0
    }
    let a = if h.len() - i > 6 { byte(h, i + 6) } else { 1.0 };
    Color { r: byte(h, i), g: byte(h, i + 2), b: byte(h, i + 4), a }
}

pub const fn with_alpha(c: Color, a: f32) -> Color {
    Color { a, ..c }
}

pub fn lerp_color(a: Color, b: Color, t: f32) -> Color {
    Color {
        r: a.r + (b.r - a.r) * t,
        g: a.g + (b.g - a.g) * t,
        b: a.b + (b.b - a.b) * t,
        a: a.a + (b.a - a.a) * t,
    }
}

pub const WHITE: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };

pub mod metrics {
    pub const RADIUS_CARD: f32 = 10.0;
    pub const RADIUS_WIDGET: f32 = 5.0;
    pub const BORDER_THIN: f32 = 2.0;
    pub const BORDER_THICK: f32 = 3.0;
    pub const SHADOW_OFFSET: f32 = 4.0;
    pub const RING_WIDTH: f32 = 3.0;
    pub const RING_THIN: f32 = 1.0;
    pub const RING_MIN_INNER: f32 = 4.0;
    pub const SHADOW_WIDGET: f32 = 2.0;
}

#[cfg(test)]
mod tests {
    use super::*;

    fn near(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-6
    }

    #[test]
    fn tokens_parse() {
        const OPAQUE: Color = color("#9B57F4");
        assert!(near(OPAQUE.r, 155.0 / 255.0) && near(OPAQUE.g, 87.0 / 255.0) && near(OPAQUE.b, 244.0 / 255.0));
        assert!(near(OPAQUE.a, 1.0));

        const TRANSLUCENT: Color = color("F0ECF914");
        assert!(near(TRANSLUCENT.a, 20.0 / 255.0));
        assert!(near(color("#aBcDeF").r, 171.0 / 255.0));

        let c = color("#9B57F4");
        let channels = c.rgba();
        assert!(near(channels[0], c.r) && near(channels[3], c.a));
    }

    const _: () = assert!(
        metrics::RADIUS_CARD > metrics::RADIUS_WIDGET
            && metrics::BORDER_THICK > metrics::BORDER_THIN
            && metrics::SHADOW_OFFSET > metrics::SHADOW_WIDGET
    );
}
