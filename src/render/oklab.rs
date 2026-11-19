//! OKLab and OKLCH conversions, gamut mapping and WCAG contrast.

use super::tokens::Color;

#[derive(Clone, Copy, Debug)]
pub struct Oklab {
    pub l: f32,
    pub a: f32,
    pub b: f32,
}

#[derive(Clone, Copy, Debug)]
pub struct Oklch {
    pub l: f32,
    pub c: f32,
    pub h: f32,
}

#[derive(Clone, Copy, Debug)]
pub struct LinearRgb {
    pub r: f32,
    pub g: f32,
    pub b: f32,
}

use std::f32::consts::PI;

pub fn srgb_to_linear(c: f32) -> f32 {
    if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) }
}

pub fn linear_to_srgb(c: f32) -> f32 {
    if c <= 0.003_130_8 { c * 12.92 } else { 1.055 * c.powf(1.0 / 2.4) - 0.055 }
}

#[allow(clippy::excessive_precision)]
pub fn oklab_from_linear(r: f32, g: f32, b: f32) -> Oklab {
    let l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b).cbrt();
    let m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b).cbrt();
    let s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b).cbrt();
    Oklab {
        l: 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        a: 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        b: 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    }
}

pub fn oklab_from_color(c: Color) -> Oklab {
    oklab_from_linear(srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b))
}

#[allow(clippy::excessive_precision)]
pub fn linear_from_oklab(lab: Oklab) -> LinearRgb {
    let l = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    let m = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    let s = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
    let (l, m, s) = (l * l * l, m * m * m, s * s * s);
    LinearRgb {
        r: 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        g: -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        b: -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    }
}

pub fn in_gamut(c: LinearRgb) -> bool {
    const EPS: f32 = 1e-4;
    [c.r, c.g, c.b].iter().all(|v| (-EPS..=1.0 + EPS).contains(v))
}

pub fn color_from_linear(c: LinearRgb, alpha: f32) -> Color {
    let encode = |v: f32| linear_to_srgb(v.clamp(0.0, 1.0));
    Color { r: encode(c.r), g: encode(c.g), b: encode(c.b), a: alpha }
}

pub fn oklab_from_oklch(lch: Oklch) -> Oklab {
    let rad = lch.h * PI / 180.0;
    Oklab { l: lch.l, a: lch.c * rad.cos(), b: lch.c * rad.sin() }
}

pub fn oklch_from_oklab(lab: Oklab) -> Oklch {
    let h = lab.b.atan2(lab.a) * 180.0 / PI;
    Oklch { l: lab.l, c: lab.a.hypot(lab.b), h: if h < 0.0 { h + 360.0 } else { h } }
}

pub fn oklch_from_color(c: Color) -> Oklch {
    oklch_from_oklab(oklab_from_color(c))
}

/// Converts to sRGB, lowering chroma by bisection until the colour fits the gamut.
pub fn color_from_oklch(lch: Oklch, alpha: f32) -> Color {
    let linear = |c: f32| linear_from_oklab(oklab_from_oklch(Oklch { c, ..lch }));
    if in_gamut(linear(lch.c)) {
        return color_from_linear(linear(lch.c), alpha);
    }
    let (mut low, mut high) = (0.0, lch.c);
    for _ in 0..20 {
        let mid = 0.5 * (low + high);
        if in_gamut(linear(mid)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    color_from_linear(linear(low), alpha)
}

pub fn relative_luminance(c: Color) -> f32 {
    0.2126 * srgb_to_linear(c.r) + 0.7152 * srgb_to_linear(c.g) + 0.0722 * srgb_to_linear(c.b)
}

pub fn contrast_ratio(a: Color, b: Color) -> f32 {
    let (la, lb) = (relative_luminance(a), relative_luminance(b));
    (la.max(lb) + 0.05) / (la.min(lb) + 0.05)
}

pub fn hue_distance(a: f32, b: f32) -> f32 {
    let d = (a - b).abs() % 360.0;
    if d > 180.0 { 360.0 - d } else { d }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::tokens::color;

    fn near(a: f32, b: f32, eps: f32) -> bool {
        (a - b).abs() < eps
    }

    #[test]
    fn oklab_reference_values() {
        let white = oklab_from_color(color("#FFFFFF"));
        assert!(near(white.l, 1.0, 1e-3) && near(white.a, 0.0, 1e-3) && near(white.b, 0.0, 1e-3));
        assert!(near(oklab_from_color(color("#000000")).l, 0.0, 1e-3));

        let red = oklch_from_color(color("#FF0000"));
        assert!(near(red.l, 0.628, 5e-3) && near(red.c, 0.2577, 5e-3) && near(red.h, 29.2, 0.5));

        for hex in ["#9B57F4", "#DBAA24", "#0A0614", "#40E0D0", "#808080"] {
            let c = color(hex);
            let back = color_from_oklch(oklch_from_color(c), 1.0);
            assert!(near(back.r, c.r, 2e-3) && near(back.g, c.g, 2e-3) && near(back.b, c.b, 2e-3), "{hex}");
        }

        let clipped = color_from_oklch(Oklch { l: 0.9, c: 0.4, h: 140.0 }, 1.0);
        for ch in [clipped.r, clipped.g, clipped.b] {
            assert!((0.0..=1.0).contains(&ch));
        }
        assert!(near(oklch_from_color(clipped).h, 140.0, 3.0));

        assert!(near(contrast_ratio(color("#FFFFFF"), color("#000000")), 21.0, 0.05));
        assert!(near(contrast_ratio(color("#777777"), color("#777777")), 1.0, 1e-4));
        assert!(near(
            contrast_ratio(color("#FFFFFF"), color("#000000")),
            contrast_ratio(color("#000000"), color("#FFFFFF")),
            1e-4
        ));

        assert!(near(hue_distance(350.0, 10.0), 20.0, 1e-4));
        assert!(near(hue_distance(0.0, 180.0), 180.0, 1e-4));
    }
}
