//! The colour theme derived from wallpaper seeds, and the live palette every painter reads.

use std::cell::Cell;

use super::oklab::{Oklch, color_from_oklch, contrast_ratio};
use super::palette_extract::Extraction;
use super::tokens::{Color, with_alpha};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Theme {
    pub ink: Color,
    pub paper: Color,
    pub surface: Color,
    pub surface_alt: Color,
    pub shadow: Color,
    pub border_outer: Color,
    pub border_inner: Color,

    pub accent: Color,
    pub accent_alpha12: Color,
    pub accent_alpha19: Color,
    pub accent_alpha25: Color,
    pub accent_container: Color,

    pub accent_alt: Color,
    pub accent_alt_alpha50: Color,
    pub accent_alt_container: Color,

    pub pop_a: Color,
    pub pop_b: Color,
    pub pop_c: Color,

    pub base: Color,
    pub overlay: Color,

    pub lavender: Color,

    pub field_bg: Color,

    pub text: Color,
    pub text_alpha04: Color,
    pub text_alpha06: Color,
    pub text_alpha08: Color,
    pub text_alpha11: Color,
    pub text_alpha15: Color,
    pub text_alpha20: Color,
    pub text_alpha65: Color,
    pub text_muted: Color,
    pub text_dim: Color,

    pub electro: Color,

    pub critical: Color,
    pub critical_alpha15: Color,
    pub critical_container: Color,

    pub warn: Color,

    pub window_backdrop: Color,
}

const FALLBACK_HUE: f32 = 250.0;
const ALT_HUE_OFFSET: f32 = 40.0;
const POP_A_OFFSET: f32 = 120.0;
const POP_B_OFFSET: f32 = 240.0;
const POP_C_OFFSET: f32 = 180.0;
const CRITICAL_HUE: f32 = 27.0;
const WARN_HUE: f32 = 75.0;

const DUSTY_CHROMA: f32 = 0.07;
const CANDY_CHROMA: f32 = 0.16;

const TONE_BRIGHT: f32 = 0.75;
const TONE_DARK: f32 = 0.35;
const TEXT_CONTRAST: f32 = 4.5;
const FILL_CONTRAST: f32 = 5.5;
const OUTLINE_GAP: f32 = 0.3;
const SHADOW_GAP: f32 = 0.06;
const SETTLE_STEPS: usize = 16;

const WHITE: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };
const BLACK: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 };

fn byte_alpha(value: u8) -> f32 {
    value as f32 / 255.0
}

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

fn wrap_hue(hue: f32) -> f32 {
    let h = hue % 360.0;
    if h < 0.0 { h + 360.0 } else { h }
}

/// 0 for a bright wallpaper, 1 for a dark one, smoothstepped between the two tones.
fn depth_from_tone(tone: f32) -> f32 {
    let t = ((TONE_BRIGHT - tone) / (TONE_BRIGHT - TONE_DARK)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// `start` when it passes `ok`, otherwise the value nearest `start` on the way to `pole` that passes.
fn settle(start: f32, pole: f32, ok: impl Fn(f32) -> bool) -> f32 {
    if ok(start) {
        return start;
    }
    let (mut low, mut high) = (start, pole);
    for _ in 0..SETTLE_STEPS {
        let mid = 0.5 * (low + high);
        if ok(mid) {
            high = mid;
        } else {
            low = mid;
        }
    }
    high
}

fn lch(l: f32, c: f32, h: f32) -> Oklch {
    Oklch { l, c, h }
}

fn rgb(o: Oklch) -> Color {
    color_from_oklch(o, 1.0)
}

pub fn theme_from_seed(e: &Extraction) -> Theme {
    let seeds = &e.seeds;
    let hue = seeds.first().map_or(FALLBACK_HUE, |s| s.hue);
    let chroma = seeds.first().map_or(DUSTY_CHROMA, |s| s.chroma);
    let seed_hue = |index: usize, offset: f32| seeds.get(index).map_or(wrap_hue(hue + offset), |s| s.hue);
    let alt_hue = seed_hue(1, ALT_HUE_OFFSET);

    let vivid = ((chroma - DUSTY_CHROMA) / (CANDY_CHROMA - DUSTY_CHROMA)).clamp(0.0, 1.0);
    let c = |dusty: f32, candy: f32| lerp(dusty, candy, vivid);

    let depth = depth_from_tone(e.tone);
    let l = |light: f32, night: f32| lerp(light, night, depth);
    let tint = lerp(1.0, 1.4, depth);

    // paper, surface, surface_alt, accent, accent_container, accent_alt, accent_alt_container,
    // pop_a, pop_b, pop_c, critical_container
    let mut grounds = [
        lch(l(0.975, 0.27), c(0.010, 0.020) * tint, hue),
        lch(l(0.945, 0.32), c(0.020, 0.040) * tint, hue),
        lch(l(0.890, 0.38), c(0.040, 0.070) * tint, hue),
        lch(l(0.74, 0.50), c(0.090, 0.190), hue),
        lch(l(0.90, 0.36), c(0.060, 0.080), hue),
        lch(l(0.80, 0.48), c(0.080, 0.170), alt_hue),
        lch(l(0.92, 0.35), c(0.050, 0.070), alt_hue),
        lch(l(0.76, 0.48), c(0.110, 0.200), seed_hue(1, POP_A_OFFSET)),
        lch(l(0.86, 0.46), c(0.100, 0.180), seed_hue(2, POP_B_OFFSET)),
        lch(l(0.80, 0.47), c(0.100, 0.170), seed_hue(3, POP_C_OFFSET)),
        lch(l(0.86, 0.36), 0.070, CRITICAL_HUE),
    ];

    let mut worst_white = f32::MAX;
    let mut worst_black = f32::MAX;
    for g in &grounds {
        let color = rgb(*g);
        worst_white = worst_white.min(contrast_ratio(color, WHITE));
        worst_black = worst_black.min(contrast_ratio(color, BLACK));
    }
    let light_text = worst_white >= worst_black;
    let text_pole = if light_text { 1.0 } else { 0.0 };
    let text_ref = if light_text { WHITE } else { BLACK };
    for g in &mut grounds {
        let (gc, gh) = (g.c, g.h);
        g.l = settle(g.l, 1.0 - text_pole, |lightness| {
            contrast_ratio(rgb(lch(lightness, gc, gh)), text_ref) >= FILL_CONTRAST
        });
    }
    let paper_l = grounds[0].l;
    let [paper, surface, surface_alt, accent, accent_container, accent_alt, accent_alt_container, pop_a, pop_b, pop_c, critical_container] =
        grounds.map(rgb);

    let all_grounds = [
        paper,
        surface,
        surface_alt,
        accent,
        accent_container,
        accent_alt,
        accent_alt_container,
        pop_a,
        pop_b,
        pop_c,
        critical_container,
    ];
    let plain_grounds = [paper, surface, surface_alt];
    let paper_ground = [paper];
    let readable = |start: f32, chroma: f32, backdrops: &[Color]| {
        settle(start, text_pole, |lightness| {
            let color = rgb(lch(lightness, chroma, hue));
            backdrops.iter().all(|b| contrast_ratio(color, *b) >= TEXT_CONTRAST)
        })
    };

    let shadow_lightness = l(0.14, 0.07);
    let mut ink_lightness = l(0.20, 0.15);
    if !light_text {
        ink_lightness = readable(ink_lightness, c(0.060, 0.100), &plain_grounds);
    }
    let mut outline_lightness =
        settle(l(0.20, 0.66), text_pole, |lightness| (lightness - paper_l).abs() >= OUTLINE_GAP);
    if !light_text {
        outline_lightness = outline_lightness.max(shadow_lightness + SHADOW_GAP);
    }

    let accent_alpha = |a: u8| with_alpha(accent, byte_alpha(a));
    let text = rgb(lch(readable(l(0.20, 0.93), c(0.060, 0.100), &all_grounds), c(0.060, 0.100), hue));
    let text_alpha = |a: u8| with_alpha(text, byte_alpha(a));
    let text_muted = rgb(lch(readable(l(0.42, 0.78), c(0.060, 0.100), &plain_grounds), c(0.060, 0.100), hue));
    let critical = rgb(lch(l(0.58, 0.66), 0.190, CRITICAL_HUE));

    Theme {
        ink: rgb(lch(ink_lightness, c(0.060, 0.100), hue)),
        paper,
        surface,
        surface_alt,
        shadow: rgb(lch(shadow_lightness, 0.050, hue)),
        border_outer: rgb(lch(outline_lightness, c(0.060, 0.100), hue)),
        border_inner: paper,

        accent,
        accent_alpha12: accent_alpha(0x1F),
        accent_alpha19: accent_alpha(0x30),
        accent_alpha25: accent_alpha(0x40),
        accent_container,

        accent_alt,
        accent_alt_alpha50: with_alpha(accent_alt, byte_alpha(0x80)),
        accent_alt_container,

        pop_a,
        pop_b,
        pop_c,

        base: paper,
        overlay: with_alpha(paper, byte_alpha(0xEB)),

        lavender: rgb(lch(readable(l(0.48, 0.72), c(0.090, 0.150), &paper_ground), c(0.090, 0.150), hue)),

        field_bg: surface,

        text,
        text_alpha04: text_alpha(0x0A),
        text_alpha06: text_alpha(0x0F),
        text_alpha08: text_alpha(0x14),
        text_alpha11: text_alpha(0x1C),
        text_alpha15: text_alpha(0x26),
        text_alpha20: text_alpha(0x33),
        text_alpha65: text_alpha(0xA6),
        text_muted,
        text_dim: with_alpha(text_muted, byte_alpha(0xD0)),

        electro: rgb(lch(l(0.70, 0.74), 0.150, hue)),

        critical,
        critical_alpha15: with_alpha(critical, byte_alpha(0x26)),
        critical_container,

        warn: rgb(lch(l(0.68, 0.78), 0.140, WARN_HUE)),

        window_backdrop: with_alpha(rgb(lch(l(0.20, 0.16), 0.030, hue)), byte_alpha(0xB3)),
    }
}

thread_local! {
    static PALETTE: Cell<Theme> = Cell::new(theme_from_seed(&Extraction::default()));
}

/// The theme every painter on this thread reads; the no-wallpaper theme until one is applied.
pub fn palette() -> Theme {
    PALETTE.with(Cell::get)
}

pub fn theme_apply(t: &Theme) {
    PALETTE.with(|p| p.set(*t));
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::oklab::{contrast_ratio, hue_distance, oklch_from_color};
    use crate::render::palette_extract::Seed;
    use crate::render::tokens::lerp_color;

    fn over(fg: Color, bg: Color) -> Color {
        lerp_color(bg, fg, fg.a)
    }

    fn near(a: Color, b: Color) -> bool {
        let e = 1.5 / 255.0;
        (a.r - b.r).abs() < e && (a.g - b.g).abs() < e && (a.b - b.b).abs() < e && (a.a - b.a).abs() < e
    }

    fn seed(hue: f32, chroma: f32, lightness: f32) -> Seed {
        Seed { hue, chroma, lightness }
    }

    fn lit(seeds: Vec<Seed>, tone: f32) -> Extraction {
        Extraction { seeds, found: true, tone }
    }

    fn base_extraction() -> Extraction {
        lit(vec![seed(195.0, 0.09, 0.75)], 0.9)
    }

    fn apply_base_theme() {
        theme_apply(&theme_from_seed(&base_extraction()));
    }

    fn l_of(c: Color) -> f32 {
        oklch_from_color(c).l
    }

    #[test]
    fn theme_rules() {
        apply_base_theme();
        let base = theme_from_seed(&base_extraction());
        let p = palette();
        assert!(
            near(base.accent, p.accent)
                && near(base.ink, p.ink)
                && near(base.paper, p.paper)
                && near(base.shadow, p.shadow)
                && near(base.text_dim, p.text_dim)
                && near(base.window_backdrop, p.window_backdrop),
            "applying a theme sets every role"
        );

        for hue in (0..360).step_by(15) {
            let t = theme_from_seed(&lit(vec![seed(hue as f32, 0.12, 0.7)], 0.9));
            for ground in [t.paper, t.surface, t.surface_alt, t.accent, t.accent_alt, t.accent_container] {
                assert!(contrast_ratio(t.text, ground) >= 4.5, "hue {hue}");
            }
            assert!(contrast_ratio(over(t.text_dim, t.paper), t.paper) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(over(t.text_dim, t.surface), t.surface) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(over(t.text_alpha65, t.paper), t.paper) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(t.text_muted, t.pop_b) >= 4.5, "hue {hue}");
            for ground in [t.pop_a, t.pop_b, t.pop_c] {
                assert!(contrast_ratio(t.text, ground) >= 4.5, "hue {hue}");
            }
            assert!(contrast_ratio(t.lavender, t.paper) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(t.paper, t.ink) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(t.text, t.critical_container) >= 4.5, "hue {hue}");
            assert!(contrast_ratio(t.text_muted, t.paper) >= 4.5, "hue {hue}");
        }

        let muted = theme_from_seed(&lit(vec![seed(350.0, 0.05, 0.7)], 0.9));
        let candy = theme_from_seed(&lit(vec![seed(350.0, 0.16, 0.7)], 0.9));
        assert!(oklch_from_color(candy.accent).c > oklch_from_color(muted.accent).c + 0.04, "vivid is more saturated");
        assert!(oklch_from_color(muted.accent).c > 0.08 && oklch_from_color(muted.pop_a).c > 0.08, "muted still tinted");
        for e in [&muted, &candy] {
            assert!(
                contrast_ratio(e.text, e.accent) >= 4.5
                    && contrast_ratio(e.text, e.surface_alt) >= 4.5
                    && contrast_ratio(e.text_muted, e.paper) >= 4.5,
                "contrast at the muted and vivid ends"
            );
        }

        let paired = theme_from_seed(&lit(vec![seed(200.0, 0.1, 0.7), seed(320.0, 0.1, 0.7)], 0.9));
        let rotated = theme_from_seed(&lit(vec![seed(200.0, 0.1, 0.7)], 0.9));
        assert!(hue_distance(oklch_from_color(paired.accent_alt).h, 320.0) < 6.0, "second seed drives accent_alt");
        assert!(hue_distance(oklch_from_color(rotated.accent_alt).h, 240.0) < 6.0, "accent_alt rotates by 40");

        let full = theme_from_seed(&lit(
            vec![seed(200.0, 0.1, 0.7), seed(320.0, 0.1, 0.7), seed(80.0, 0.1, 0.7), seed(20.0, 0.1, 0.7)],
            0.9,
        ));
        let hue_near = |c: Color, h: f32| hue_distance(oklch_from_color(c).h, h) < 8.0;
        assert!(hue_near(full.pop_a, 320.0) && hue_near(full.pop_b, 80.0) && hue_near(full.pop_c, 20.0), "pops follow seeds");
        assert!(
            hue_near(rotated.pop_a, 320.0) && hue_near(rotated.pop_b, 80.0) && hue_near(rotated.pop_c, 20.0),
            "pops rotate the seed hue"
        );
        assert!(oklch_from_color(full.accent).c > 0.12 && oklch_from_color(full.pop_a).c > 0.12, "bold accents");
        assert!(l_of(full.paper) > 0.96, "paper stays near white");

        let night = theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], 0.15));
        assert!(l_of(night.paper) < 0.3 && l_of(night.text) > 0.85, "dark wallpaper, dark paper, light text");
        assert!(l_of(night.ink) < l_of(night.paper), "dark wallpaper keeps ink dark");
        assert!(
            l_of(night.border_outer) > l_of(night.shadow) + 0.4 && near(base.border_outer, base.ink),
            "dark outline apart from the shadow, bright outline is the ink"
        );
        assert!(
            hue_distance(oklch_from_color(night.accent).h, oklch_from_color(base.accent).h) < 1.0,
            "tone leaves the hue alone"
        );
        assert!(l_of(night.text_muted) > l_of(night.paper) + 0.3, "muted text readable on dark paper");

        for tone in [0.15, 0.35, 0.5, 0.65, 0.9] {
            for hue in (0..360).step_by(15) {
                let t = theme_from_seed(&lit(vec![seed(hue as f32, 0.12, 0.7)], tone));
                for ground in [
                    t.paper,
                    t.surface,
                    t.surface_alt,
                    t.accent,
                    t.accent_alt,
                    t.pop_a,
                    t.pop_b,
                    t.pop_c,
                    t.accent_container,
                    t.critical_container,
                ] {
                    assert!(contrast_ratio(t.text, ground) >= 4.5, "tone {tone} hue {hue}");
                }
                for ground in [t.paper, t.surface, t.surface_alt] {
                    assert!(contrast_ratio(t.text_muted, ground) >= 4.5, "tone {tone} hue {hue}");
                }
                let (inner, outer, drop) = (l_of(t.border_inner), l_of(t.border_outer), l_of(t.shadow));
                assert!(
                    (inner - outer).abs() > 0.3 && (outer - drop).abs() > 0.04 && (inner - drop).abs() > 0.04,
                    "three distinct outline colours, tone {tone} hue {hue}"
                );
                let paper = l_of(t.paper);
                assert!(
                    l_of(t.ink) < paper && (l_of(t.text) > paper || contrast_ratio(t.ink, t.paper) >= 4.5),
                    "ink deep and readable, tone {tone} hue {hue}"
                );
            }
        }

        let mut previous = 2.0;
        for step in (2..=18).rev() {
            let paper = l_of(theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], step as f32 * 0.05)).paper);
            assert!(paper <= previous + 1e-4, "paper darkens as the tone falls");
            previous = paper;
        }
        assert!(
            l_of(theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], 0.9)).paper) > 0.96
                && l_of(theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], 0.15)).paper) < 0.3,
            "extreme tones give pale and deep paper"
        );
        assert!(
            near(theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], 0.75)).paper, base.paper)
                && near(theme_from_seed(&lit(vec![seed(195.0, 0.09, 0.75)], 0.35)).paper, night.paper),
            "ramp ends settle on the bright and dark tables"
        );

        let other = theme_from_seed(&lit(vec![seed(20.0, 0.12, 0.7)], 0.9));
        assert!(!near(other.accent, palette().accent), "different seed, different accent");
        theme_apply(&other);
        let p = palette();
        assert!(
            near(other.accent, p.accent)
                && near(other.text, p.text)
                && near(other.text_alpha20, p.text_alpha20)
                && near(other.overlay, p.overlay),
            "theme_apply sets the palette"
        );
        apply_base_theme();
        assert!(near(base.accent, palette().accent), "theme_apply restores the base theme");
    }

    #[test]
    fn fallback_theme_readable() {
        let t = palette(); // a fresh test thread has never applied a theme
        assert_eq!(t, theme_from_seed(&Extraction::default()));
        for ground in [
            t.paper,
            t.surface,
            t.surface_alt,
            t.accent,
            t.accent_alt,
            t.pop_a,
            t.pop_b,
            t.pop_c,
            t.accent_container,
            t.critical_container,
        ] {
            assert!(contrast_ratio(t.text, ground) >= 4.5);
        }
    }
}
