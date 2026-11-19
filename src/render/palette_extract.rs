//! Seed colours picked from a wallpaper: k-means in OKLab, ranked by area and chroma.

use super::oklab::{
    Oklab, Oklch, color_from_oklch, hue_distance, oklab_from_linear, oklch_from_oklab, srgb_to_linear,
};
use super::tokens::Color;

pub const MAX_SEEDS: usize = 4;

const MAX_SIDE: i32 = 64;
const CLUSTER_COUNT: usize = 8;
const ITERATION_CAP: usize = 12;
const ALPHA_FLOOR: f32 = 0.5;
const GREY_CHROMA: f32 = 0.04;
const VIVID_CHROMA: f32 = 0.12;
const SECONDARY_HUE_GAP: f32 = 40.0;
const SECONDARY_HUE_NEAR: f32 = 90.0;
const MERGE_DISTANCE: f32 = 1e-4;
const EMBER_MIN_LIGHTNESS: f32 = 0.72;
const EMBER_PLAIN_LIGHTNESS: f32 = 0.92;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Seed {
    pub hue: f32,
    pub chroma: f32,
    pub lightness: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Extraction {
    pub seeds: Vec<Seed>,
    pub found: bool,
    /// Mean OKLab lightness of the image, `0.5` when it has no usable pixel.
    pub tone: f32,
}

impl Default for Extraction {
    fn default() -> Self {
        Extraction { seeds: Vec::new(), found: false, tone: 0.5 }
    }
}

fn chroma_weight(chroma: f32) -> f32 {
    ((chroma - GREY_CHROMA) / (VIVID_CHROMA - GREY_CHROMA)).clamp(0.0, 1.0)
}

fn distance_sq(a: Oklab, b: Oklab) -> f32 {
    let (dl, da, db) = (a.l - b.l, a.a - b.a, a.b - b.b);
    dl * dl + da * da + db * db
}

/// Box-averages the image down to at most `MAX_SIDE` cells a side, dropping mostly transparent cells.
fn downsample(rgba: &[u8], width: i32, height: i32, premultiplied: bool) -> Vec<Oklab> {
    let step = 1.max((width.max(height) + MAX_SIDE - 1) / MAX_SIDE);
    let mut samples = Vec::new();
    for y0 in (0..height).step_by(step as usize) {
        for x0 in (0..width).step_by(step as usize) {
            let (mut r, mut g, mut b, mut a) = (0.0, 0.0, 0.0, 0.0);
            let mut count = 0;
            for y in y0..(y0 + step).min(height) {
                for x in x0..(x0 + step).min(width) {
                    let at = (y as usize * width as usize + x as usize) * 4;
                    let p = &rgba[at..at + 4];
                    let alpha = p[3] as f32 / 255.0;
                    let un = if premultiplied && alpha > 0.0 { 1.0 / alpha } else { 1.0 };
                    let channel = |v: u8| srgb_to_linear((v as f32 / 255.0 * un).min(1.0)) * alpha;
                    r += channel(p[0]);
                    g += channel(p[1]);
                    b += channel(p[2]);
                    a += alpha;
                    count += 1;
                }
            }
            if count == 0 || a / (count as f32) < ALPHA_FLOOR {
                continue;
            }
            samples.push(oklab_from_linear(r / a, g / a, b / a));
        }
    }
    samples
}

struct Cluster {
    center: Oklab,
    population: usize,
}

/// K-means seeded with the sample nearest the mean, then farthest-point picks.
fn cluster(samples: &[Oklab]) -> Vec<Cluster> {
    let n = samples.len() as f32;
    let sum = samples.iter().fold(Oklab { l: 0.0, a: 0.0, b: 0.0 }, |m, s| Oklab {
        l: m.l + s.l,
        a: m.a + s.a,
        b: m.b + s.b,
    });
    let mean = Oklab { l: sum.l / n, a: sum.a / n, b: sum.b / n };

    let mut first = 0;
    for i in 1..samples.len() {
        if distance_sq(samples[i], mean) < distance_sq(samples[first], mean) {
            first = i;
        }
    }
    let mut clusters = vec![Cluster { center: samples[first], population: 0 }];

    let mut nearest: Vec<f32> = samples.iter().map(|s| distance_sq(*s, clusters[0].center)).collect();
    while clusters.len() < CLUSTER_COUNT {
        let mut far = 0;
        for i in 1..samples.len() {
            if nearest[i] > nearest[far] {
                far = i;
            }
        }
        if nearest[far] < MERGE_DISTANCE {
            break;
        }
        clusters.push(Cluster { center: samples[far], population: 0 });
        for (i, s) in samples.iter().enumerate() {
            nearest[i] = nearest[i].min(distance_sq(*s, samples[far]));
        }
    }

    let mut owner = vec![0usize; samples.len()];
    for _ in 0..ITERATION_CAP {
        let mut moved = false;
        for (i, s) in samples.iter().enumerate() {
            let mut best = 0;
            let mut best_distance = distance_sq(*s, clusters[0].center);
            for (c, cl) in clusters.iter().enumerate().skip(1) {
                let d = distance_sq(*s, cl.center);
                if d < best_distance {
                    best = c;
                    best_distance = d;
                }
            }
            if owner[i] != best {
                moved = true;
                owner[i] = best;
            }
        }
        let mut sums = vec![Oklab { l: 0.0, a: 0.0, b: 0.0 }; clusters.len()];
        let mut counts = vec![0usize; clusters.len()];
        for (s, &o) in samples.iter().zip(&owner) {
            sums[o].l += s.l;
            sums[o].a += s.a;
            sums[o].b += s.b;
            counts[o] += 1;
        }
        for (c, cl) in clusters.iter_mut().enumerate() {
            cl.population = counts[c];
            if counts[c] > 0 {
                let k = counts[c] as f32;
                cl.center = Oklab { l: sums[c].l / k, a: sums[c].a / k, b: sums[c].b / k };
            }
        }
        if !moved {
            break;
        }
    }
    clusters
}

/// Picks up to `MAX_SEEDS` hues at least 40 degrees apart; the second prefers a neighbour within 90.
/// A buffer shorter than `width * height * 4` bytes is treated as no image.
pub fn extract_seeds(rgba: &[u8], width: i32, height: i32, premultiplied: bool) -> Extraction {
    let mut none = Extraction::default();
    if width <= 0 || height <= 0 || rgba.len() < width as usize * height as usize * 4 {
        return none;
    }
    let samples = downsample(rgba, width, height, premultiplied);
    if samples.is_empty() {
        return none;
    }
    let total = samples.len() as f32;
    let tone = samples.iter().map(|s| s.l).sum::<f32>() / total;
    none.tone = tone;

    let mut ranked: Vec<(Seed, f32)> = Vec::new();
    for c in cluster(&samples) {
        let lch = oklch_from_oklab(c.center);
        let score = (c.population as f32 / total).sqrt() * chroma_weight(lch.c);
        if c.population > 0 && score > 0.0 {
            ranked.push((Seed { hue: lch.h, chroma: lch.c, lightness: lch.l }, score));
        }
    }
    if ranked.is_empty() {
        return none;
    }
    ranked.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));

    let mut result = Extraction { seeds: vec![ranked[0].0], found: true, tone };
    let mut used = vec![false; ranked.len()];
    used[0] = true;
    let mut has_secondary = false;
    for limit in [SECONDARY_HUE_NEAR, 360.0] {
        for i in 1..ranked.len() {
            if has_secondary {
                break;
            }
            let gap = hue_distance(ranked[i].0.hue, result.seeds[0].hue);
            if gap >= SECONDARY_HUE_GAP && gap <= limit {
                result.seeds.push(ranked[i].0);
                used[i] = true;
                has_secondary = true;
            }
        }
    }
    for i in 1..ranked.len() {
        if result.seeds.len() >= MAX_SEEDS {
            break;
        }
        if used[i] {
            continue;
        }
        let distinct = result.seeds.iter().all(|s| hue_distance(ranked[i].0.hue, s.hue) >= SECONDARY_HUE_GAP);
        if distinct {
            result.seeds.push(ranked[i].0);
        }
    }
    result
}

/// The most chromatic seed lifted to a bright tone, or a near white when there is no seed.
pub fn ember_colour(e: &Extraction) -> Color {
    if !e.found || e.seeds.is_empty() {
        return color_from_oklch(Oklch { l: EMBER_PLAIN_LIGHTNESS, c: 0.0, h: 0.0 }, 1.0);
    }
    let mut vivid = &e.seeds[0];
    for s in &e.seeds {
        if s.chroma > vivid.chroma {
            vivid = s;
        }
    }
    let l = vivid.lightness.max(EMBER_MIN_LIGHTNESS);
    color_from_oklch(Oklch { l, c: vivid.chroma, h: vivid.hue }, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::oklab::{Oklch, hue_distance, oklch_from_color};
    use crate::render::tokens::color;

    struct Image {
        width: i32,
        height: i32,
        data: Vec<u8>,
    }

    impl Image {
        fn new(w: i32, h: i32, fill: Color) -> Image {
            let mut img = Image { width: w, height: h, data: vec![0; (w * h * 4) as usize] };
            for y in 0..h {
                for x in 0..w {
                    img.set(x, y, fill);
                }
            }
            img
        }

        fn set(&mut self, x: i32, y: i32, c: Color) {
            let at = ((y * self.width + x) * 4) as usize;
            for (i, ch) in c.rgba().into_iter().enumerate() {
                self.data[at + i] = (ch * 255.0).round() as u8;
            }
        }

        fn extract(&self, premultiplied: bool) -> Extraction {
            extract_seeds(&self.data, self.width, self.height, premultiplied)
        }
    }

    fn hue_of(hex: &str) -> f32 {
        oklch_from_color(color(hex)).h
    }

    #[test]
    fn extraction_cases() {
        let solid = Image::new(40, 30, color("#E0405A"));
        let a = solid.extract(false);
        assert!(a.found && hue_distance(a.seeds[0].hue, hue_of("#E0405A")) < 2.0, "solid seeds its own hue");
        assert_eq!(a.seeds.len(), 1, "solid has no further seed");
        let again = solid.extract(false);
        assert!(again.seeds[0].hue == a.seeds[0].hue && again.seeds[0].chroma == a.seeds[0].chroma, "deterministic");

        let mut split = Image::new(200, 100, color("#2060E0"));
        for y in 0..split.height {
            for x in 0..80 {
                split.set(x, y, color("#E0A020"));
            }
        }
        let b = split.extract(false);
        assert!(b.found && hue_distance(b.seeds[0].hue, hue_of("#2060E0")) < 3.0, "larger colour wins");
        assert!(b.seeds.len() == 2 && hue_distance(b.seeds[1].hue, hue_of("#E0A020")) < 3.0, "smaller is second");

        let mut trio = Image::new(300, 100, color("#2060E0"));
        for y in 0..trio.height {
            for x in 0..60 {
                trio.set(x, y, color("#E0A020"));
            }
            for x in 60..120 {
                trio.set(x, y, color("#A040E0"));
            }
        }
        let t3 = trio.extract(false);
        let gap = if t3.seeds.len() > 1 { hue_distance(t3.seeds[1].hue, t3.seeds[0].hue) } else { 0.0 };
        assert!(t3.seeds.len() == 3 && (40.0..=90.0).contains(&gap), "second seed prefers a neighbouring hue");
        for i in 0..t3.seeds.len() {
            for j in i + 1..t3.seeds.len() {
                assert!(hue_distance(t3.seeds[i].hue, t3.seeds[j].hue) >= 40.0, "seeds 40 degrees apart");
            }
        }

        let mut pale = Image::new(200, 100, color("#E3E7EF"));
        for y in 0..100 {
            for x in 0..120 {
                pale.set(x, y, color("#A9C4E0"));
            }
        }
        for y in 0..20 {
            for x in 0..20 {
                pale.set(x, y, color("#C0207A"));
                pale.set(x + 40, y, color("#E0B000"));
            }
        }
        let pm = pale.extract(false);
        let has = |hex| pm.seeds.iter().any(|s| hue_distance(s.hue, hue_of(hex)) < 6.0);
        assert!(pm.found && has("#C0207A") && has("#E0B000"), "small vivid patches survive a pale majority");

        let mut grey = Image::new(100, 100, color("#808080"));
        for y in 0..10 {
            for x in 0..10 {
                grey.set(x, y, color("#D01040"));
            }
        }
        let c = grey.extract(false);
        assert!(c.found && hue_distance(c.seeds[0].hue, hue_of("#D01040")) < 3.0, "grey cannot beat a saturated patch");

        assert!(!Image::new(50, 50, color("#707070")).extract(false).found, "grey has no usable colour");
        let e = Image::new(50, 50, Color { r: 1.0, g: 0.0, b: 0.0, a: 0.0 }).extract(false);
        assert!(!e.found, "transparent has no usable colour");
        let f = extract_seeds(&[], 0, 0, false);
        assert!(!f.found, "no pixels");
        assert!(f.tone == 0.5 && e.tone == 0.5, "default tone kept");

        let dark_tone = Image::new(50, 50, color("#000000")).extract(false);
        let bright_tone = Image::new(50, 50, color("#FFFFFF")).extract(false);
        assert!(dark_tone.tone < 0.05 && bright_tone.tone > 0.95, "tone follows lightness");
        assert!(!dark_tone.found && !bright_tone.found, "black and white carry no seed");
        assert!(a.tone > 0.4 && a.tone < 0.8, "mid-tone colour has a middle tone");

        let mut half = Image::new(40, 40, color("#E0405A"));
        for p in half.data.as_chunks_mut::<4>().0 {
            for ch in &mut p[..3] {
                *ch = (*ch as f32 * 0.8) as u8;
            }
            p[3] = 204;
        }
        let g = half.extract(true);
        assert!(g.found && hue_distance(g.seeds[0].hue, hue_of("#E0405A")) < 3.0, "premultiplied keeps the hue");

        let dim_seeds = Image::new(40, 40, color("#5A1020")).extract(false);
        let ember: Oklch = oklch_from_color(ember_colour(&dim_seeds));
        assert!(dim_seeds.found && hue_distance(ember.h, dim_seeds.seeds[0].hue) < 5.0 && ember.l >= 0.7, "ember lifted");
        let near_white = oklch_from_color(ember_colour(&dark_tone));
        assert!(near_white.c < 0.02 && near_white.l > 0.85, "near-white ember without a seed");
    }

    #[test]
    fn short_buffer_is_empty() {
        assert!(!extract_seeds(&[255; 12], 4, 4, false).found);
    }
}
