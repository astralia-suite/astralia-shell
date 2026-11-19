//! The seeded arrival map behind the wallpaper spread transition: a rank per cell of when the
//! spread reaches it (RG, 16 bit) and a blurred twin for the glow (BA, 16 bit).

use std::cmp::Ordering;
use std::collections::BinaryHeap;

// Grid
pub const SPREAD_MAP_WIDTH: i32 = 480;
pub const SPREAD_MAP_HEIGHT: i32 = 270;

// Noise octaves
const OCTAVE_WEIGHTS: [f32; 3] = [0.52, 0.30, 0.18];
const OCTAVE_SCALES: [[f32; 2]; 3] = [[18.0, 65.0], [7.0, 20.0], [2.0, 6.0]];

// Cost field
const COST_FLOOR: f32 = 0.12;
const SHARPNESS_MIN: f32 = 5.0;
const SHARPNESS_MAX: f32 = 9.0;
const GRAIN_MIN: f32 = 0.15;
const GRAIN_MAX: f32 = 0.65;

// Colonies
const COLONIES_MIN: i32 = 6;
const COLONIES_MAX: i32 = 12;
const COLONY_MARGIN_X: f32 = 0.06;
const COLONY_MARGIN_Y: f32 = 0.08;
const COLONY_DELAY: f32 = 0.12;

// Glow twin
const BLUR_SIGMA: f32 = 2.5;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SpreadMap {
    pub width: i32,
    pub height: i32,
    pub rgba: Vec<u8>,
}

/// A seeded splitmix64 stream; only determinism per seed matters, the seed itself is random.
struct FieldRng(u64);

impl FieldRng {
    fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn uniform(&mut self, lo: f32, hi: f32) -> f32 {
        let unit = (self.next_u64() >> 40) as f32 / (1u64 << 24) as f32;
        lo + unit * (hi - lo)
    }

    fn int(&mut self, lo: i32, hi: i32) -> i32 {
        lo + (self.next_u64() % (hi - lo + 1) as u64) as i32
    }
}

fn smoothstep01(t: f32) -> f32 {
    t * t * (3.0 - 2.0 * t)
}

fn value_noise(rng: &mut FieldRng, width: i32, height: i32, scale: f32) -> Vec<f32> {
    let nx = (width as f32 / scale) as usize + 3;
    let ny = (height as f32 / scale) as usize + 3;
    let lattice: Vec<f32> = (0..nx * ny).map(|_| rng.uniform(0.0, 1.0)).collect();
    let at = |x: usize, y: usize| lattice[y * nx + x];
    let mut out = Vec::with_capacity((width * height) as usize);
    for y in 0..height {
        let py = y as f32 / scale;
        let iy = py as usize;
        let fy = smoothstep01(py - iy as f32);
        for x in 0..width {
            let px = x as f32 / scale;
            let ix = px as usize;
            let fx = smoothstep01(px - ix as f32);
            let top = at(ix, iy) * (1.0 - fx) + at(ix + 1, iy) * fx;
            let bottom = at(ix, iy + 1) * (1.0 - fx) + at(ix + 1, iy + 1) * fx;
            out.push(top * (1.0 - fy) + bottom * fy);
        }
    }
    out
}

/// Each value's position in sorted order, spread over `[0, 1]`.
fn rank_evenly(values: &[f32]) -> Vec<f32> {
    let mut order: Vec<usize> = (0..values.len()).collect();
    order.sort_by(|&a, &b| values[a].total_cmp(&values[b]));
    let last = values.len().saturating_sub(1).max(1) as f32;
    let mut rank = vec![0.0; values.len()];
    for (k, &i) in order.iter().enumerate() {
        rank[i] = k as f32 / last;
    }
    rank
}

fn gaussian_blur(values: &[f32], width: i32, height: i32, sigma: f32) -> Vec<f32> {
    let radius = (3.0 * sigma) as i32 + 1;
    let mut kernel: Vec<f32> = (-radius..=radius).map(|i| (-((i * i) as f32) / (2.0 * sigma * sigma)).exp()).collect();
    let sum: f32 = kernel.iter().sum();
    kernel.iter_mut().for_each(|k| *k /= sum);
    let pass = |src: &[f32], horizontal: bool| {
        let mut out = vec![0.0; src.len()];
        for y in 0..height {
            for x in 0..width {
                let mut v = 0.0;
                for i in -radius..=radius {
                    let (sx, sy) =
                        if horizontal { ((x + i).clamp(0, width - 1), y) } else { (x, (y + i).clamp(0, height - 1)) };
                    v += kernel[(i + radius) as usize] * src[(sy * width + sx) as usize];
                }
                out[(y * width + x) as usize] = v;
            }
        }
        out
    };
    pass(&pass(values, true), false)
}

fn quantize16(value: f32) -> u16 {
    (value * 65535.0 + 0.5).clamp(0.0, 65535.0) as u16
}

/// A min-heap entry: the earliest arrival pops first.
struct Entry(f32, usize);

impl PartialEq for Entry {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == Ordering::Equal
    }
}
impl Eq for Entry {}
impl PartialOrd for Entry {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl Ord for Entry {
    fn cmp(&self, other: &Self) -> Ordering {
        other.0.total_cmp(&self.0).then(other.1.cmp(&self.1))
    }
}

/// Dijkstra over the 8-connected grid from every colony at its start time.
fn arrival(cost: &[f32], colonies: &[(usize, f32)], w: i32, h: i32) -> Vec<f32> {
    const STEPS: [(i32, i32); 8] = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)];
    #[allow(clippy::approx_constant)]
    const DIAGONAL: f32 = 1.4142;
    let mut time = vec![f32::INFINITY; cost.len()];
    let mut heap = BinaryHeap::new();
    for &(cell, start) in colonies {
        if start < time[cell] {
            time[cell] = start;
            heap.push(Entry(start, cell));
        }
    }
    while let Some(Entry(t, cell)) = heap.pop() {
        if t != time[cell] {
            continue;
        }
        let (x, y) = (cell as i32 % w, cell as i32 / w);
        let here = cost[cell];
        for (s, (dx, dy)) in STEPS.iter().enumerate() {
            let (nx, ny) = (x + dx, y + dy);
            if nx < 0 || nx >= w || ny < 0 || ny >= h {
                continue;
            }
            let next = (ny * w + nx) as usize;
            let step = if s < 4 { 1.0 } else { DIAGONAL };
            let candidate = t + (here + cost[next]) * 0.5 * step;
            if candidate < time[next] {
                time[next] = candidate;
                heap.push(Entry(candidate, next));
            }
        }
    }
    time
}

pub fn make_spread_map(seed: u32) -> SpreadMap {
    let mut rng = FieldRng(u64::from(seed));
    let (w, h) = (SPREAD_MAP_WIDTH, SPREAD_MAP_HEIGHT);
    let cells = (w * h) as usize;

    let mut noise = vec![0.0f32; cells];
    for (weight, [lo, hi]) in OCTAVE_WEIGHTS.iter().zip(OCTAVE_SCALES) {
        let scale = rng.uniform(lo, hi);
        let octave = value_noise(&mut rng, w, h, scale);
        noise.iter_mut().zip(octave).for_each(|(n, o)| *n += weight * o);
    }
    let sharpness = rng.uniform(SHARPNESS_MIN, SHARPNESS_MAX);
    let grain = rng.uniform(GRAIN_MIN, GRAIN_MAX);
    let cost: Vec<f32> =
        noise.iter().map(|n| COST_FLOOR + (sharpness * (n - 0.5)).exp() + grain * rng.uniform(0.0, 1.0)).collect();

    let count = rng.int(COLONIES_MIN, COLONIES_MAX);
    let delay = (cells as f32).sqrt() * COLONY_DELAY;
    let colonies: Vec<(usize, f32)> = (0..count)
        .map(|_| {
            let x = (rng.uniform(COLONY_MARGIN_X, 1.0 - COLONY_MARGIN_X) * w as f32) as i32;
            let y = (rng.uniform(COLONY_MARGIN_Y, 1.0 - COLONY_MARGIN_Y) * h as f32) as i32;
            ((y * w + x) as usize, rng.uniform(0.0, delay))
        })
        .collect();

    let rank = rank_evenly(&arrival(&cost, &colonies, w, h));
    let glow = gaussian_blur(&rank, w, h, BLUR_SIGMA);
    let mut rgba = Vec::with_capacity(cells * 4);
    for (r, g) in rank.iter().zip(&glow) {
        let (a, g) = (quantize16(*r), quantize16(*g));
        rgba.extend_from_slice(&[(a >> 8) as u8, (a & 0xFF) as u8, (g >> 8) as u8, (g & 0xFF) as u8]);
    }
    SpreadMap { width: w, height: h, rgba }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn arrival(map: &SpreadMap, cell: usize) -> u16 {
        u16::from(map.rgba[cell * 4]) << 8 | u16::from(map.rgba[cell * 4 + 1])
    }

    fn glow(map: &SpreadMap, cell: usize) -> u16 {
        u16::from(map.rgba[cell * 4 + 2]) << 8 | u16::from(map.rgba[cell * 4 + 3])
    }

    #[test]
    fn spread_map() {
        let a = make_spread_map(7);
        let b = make_spread_map(7);
        let c = make_spread_map(8);
        let cells = (SPREAD_MAP_WIDTH * SPREAD_MAP_HEIGHT) as usize;
        assert!(
            a.width == SPREAD_MAP_WIDTH && a.height == SPREAD_MAP_HEIGHT && a.rgba.len() == cells * 4,
            "the spread map covers the whole grid in RGBA"
        );
        assert!(a.rgba == b.rgba, "the same seed gives the same spread");
        assert!(a.rgba != c.rgba, "another seed gives another spread");

        let ranks: Vec<u16> = (0..cells).map(|i| arrival(&a, i)).collect();
        let (low, high) = (*ranks.iter().min().unwrap(), *ranks.iter().max().unwrap());
        assert!(low == 0 && high == 65535, "arrival ranks run from the first cell to the last");

        let mut sorted = ranks.clone();
        sorted.sort_unstable();
        let quarter = usize::from(sorted[cells / 4]);
        assert!(
            quarter > 65535 / 4 - 512 && quarter < 65535 / 4 + 512,
            "ranks are spread evenly, so the area grows at a steady rate"
        );

        let first = ranks.iter().position(|&r| r == 0).unwrap();
        let last = ranks.iter().position(|&r| r == 65535).unwrap();
        assert!(glow(&a, first) < glow(&a, last), "the glow twin follows the spread, low where it starts");
    }
}
