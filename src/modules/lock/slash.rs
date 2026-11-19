//! The slash transition plan: seeded cuts, the shattered shards and a per-progress frame of what to draw.

use crate::config::lock_config as cfg;
use crate::render::geometry::Point;
use std::f32::consts::PI;

pub type SlashPolygon = Vec<Point>;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SlashStrike {
    pub a: Point,
    pub b: Point,
    pub progress: f32,
    pub intensity: f32,
    pub seed: f32,
    pub thick: f32,
    pub amp: f32,
    pub time_s: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SlashBeam {
    pub a: Point,
    pub b: Point,
    pub width: f32,
    pub alpha: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SlashMark {
    pub a: Point,
    pub b: Point,
    pub width: f32,
    pub alpha: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SlashShock {
    pub center: Point,
    pub radius: f32,
    pub progress: f32,
    pub intensity: f32,
    pub time_s: f32,
}

#[derive(Clone, Debug, Default)]
pub struct SlashFrame {
    pub desktop: Vec<SlashPolygon>,
    pub sources: Vec<SlashPolygon>,
    pub shattered: bool,
    pub marks: Vec<SlashMark>,
    pub beams: Vec<SlashBeam>,
    pub strikes: Vec<SlashStrike>,
    pub rays: Vec<SlashPolygon>,
    pub slivers: Vec<SlashPolygon>,
    pub shock: Option<SlashShock>,
    pub sliver_alpha: f32,
    pub flash: f32,
    pub dim: f32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct SlashSliver {
    pub along: f32,
    pub perp: f32,
    pub length: f32,
    pub thick: f32,
    pub drift_perp: f32,
    pub drift_along: f32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct SlashCut {
    pub center: Point,
    pub direction: Point,
    pub a: Point,
    pub b: Point,
    pub start: f32,
    pub land: f32,
}

/// splitmix64, so a seed gives the same rays, slivers and fall order on every run.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn unit(&mut self) -> f32 {
        (self.next() >> 40) as f32 / 16_777_216.0
    }

    fn range(&mut self, low: f32, high: f32) -> f32 {
        low + (high - low) * self.unit()
    }
}

fn clamp01(v: f32) -> f32 {
    v.clamp(0.0, 1.0)
}

fn ease_out_cubic(t: f32) -> f32 {
    let inverse = 1.0 - t;
    1.0 - inverse * inverse * inverse
}

fn pt(x: f32, y: f32) -> Point {
    Point { x, y }
}

fn lerp(a: Point, b: Point, t: f32) -> Point {
    pt(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)
}

fn clip_half(polygon: &SlashPolygon, origin: Point, direction: Point, sign: f32) -> SlashPolygon {
    let side = |p: Point| sign * (direction.x * (p.y - origin.y) - direction.y * (p.x - origin.x));
    let mut out = Vec::new();
    let count = polygon.len();
    for i in 0..count {
        let current = polygon[i];
        let next = polygon[(i + 1) % count];
        let side_current = side(current);
        let side_next = side(next);
        if side_current >= 0.0 {
            out.push(current);
        }
        if (side_current >= 0.0) != (side_next >= 0.0) {
            out.push(lerp(current, next, side_current / (side_current - side_next)));
        }
    }
    out
}

fn centroid(polygon: &SlashPolygon) -> Point {
    let mut sum = Point::default();
    for p in polygon {
        sum.x += p.x;
        sum.y += p.y;
    }
    let n = polygon.len() as f32;
    pt(sum.x / n, sum.y / n)
}

fn transformed(polygon: &SlashPolygon, middle: Point, drift: Point, scale: f32, rotation: f32) -> SlashPolygon {
    let (s, c) = rotation.sin_cos();
    polygon
        .iter()
        .map(|p| {
            let dx = (p.x - middle.x) * scale;
            let dy = (p.y - middle.y) * scale;
            pt(middle.x + drift.x + dx * c - dy * s, middle.y + drift.y + dx * s + dy * c)
        })
        .collect()
}

fn clip_axis(origin: f32, direction: f32, low: f32, high: f32, t_min: &mut f32, t_max: &mut f32) {
    if direction.abs() < 1e-6 {
        return;
    }
    let t0 = (low - origin) / direction;
    let t1 = (high - origin) / direction;
    *t_min = t_min.max(t0.min(t1));
    *t_max = t_max.min(t0.max(t1));
}

fn make_cut(center: Point, direction: Point, width: f32, height: f32, reach: f32, start: f32, land: f32) -> SlashCut {
    let diagonal = width.hypot(height) + 2.0 * reach;
    let mut t_min = -diagonal;
    let mut t_max = diagonal;
    clip_axis(center.x, direction.x, -reach, width + reach, &mut t_min, &mut t_max);
    clip_axis(center.y, direction.y, -reach, height + reach, &mut t_min, &mut t_max);
    SlashCut {
        center,
        direction,
        a: pt(center.x + direction.x * t_min, center.y + direction.y * t_min),
        b: pt(center.x + direction.x * t_max, center.y + direction.y * t_max),
        start,
        land,
    }
}

#[allow(clippy::too_many_arguments)]
fn strike_of(cut: &SlashCut, overshoot: f32, progress: f32, intensity: f32, seed: f32, thick: f32, amp: f32, time_s: f32) -> SlashStrike {
    let reach = pt((cut.b.x - cut.a.x) * overshoot, (cut.b.y - cut.a.y) * overshoot);
    SlashStrike { a: pt(cut.a.x - reach.x, cut.a.y - reach.y), b: pt(cut.b.x + reach.x, cut.b.y + reach.y), progress, intensity, seed, thick, amp, time_s }
}

pub fn slash_polygon_area(polygon: &SlashPolygon) -> f32 {
    let count = polygon.len();
    let mut twice = 0.0;
    for i in 0..count {
        let a = polygon[i];
        let b = polygon[(i + 1) % count];
        twice += a.x * b.y - b.x * a.y;
    }
    twice.abs() * 0.5
}

pub fn slash_run_progress(elapsed_ms: f32) -> f32 {
    clamp01(elapsed_ms.max(0.0) / cfg::SLASH_MS)
}

#[derive(Clone, Debug)]
pub struct SlashPlan {
    width: f32,
    height: f32,
    cuts: Vec<SlashCut>,
    final_: SlashCut,
    rays: Vec<SlashPolygon>,
    slivers: Vec<SlashSliver>,
    shards: Vec<SlashPolygon>,
    fall_rank: Vec<usize>,
    shard_spin: Vec<f32>,
}

impl SlashPlan {
    pub fn new(seed: u32, width: f32, height: f32) -> SlashPlan {
        let width = width.max(0.0);
        let height = height.max(0.0);
        let mut rng = Rng(u64::from(seed));
        let diagonal = width.hypot(height);
        let middle = pt(width * 0.5, height * 0.5);

        let final_direction = if diagonal > 0.0 { pt(cfg::FINAL_TILT.cos(), -cfg::FINAL_TILT.sin()) } else { pt(1.0, 0.0) };
        let final_ = make_cut(middle, final_direction, width, height, cfg::SLASH_FINAL_REACH * diagonal, cfg::SLASH_FINAL_START, cfg::SLASH_FINAL_START + cfg::SLASH_FINAL_DRAW_SPAN);

        let mut cuts = Vec::with_capacity(cfg::SLASH_COUNT);
        for k in 0..cfg::STAR_COUNT {
            let from = cfg::STAR_CORNERS[k];
            let to = cfg::STAR_CORNERS[(k + 1) % cfg::STAR_COUNT];
            let a = pt(from.x * width, from.y * height);
            let b = pt(to.x * width, to.y * height);
            let length = (b.x - a.x).hypot(b.y - a.y);
            let direction = if length > 0.0 { pt((b.x - a.x) / length, (b.y - a.y) / length) } else { pt(1.0, 0.0) };
            let start = cfg::SLASH_FIRST_START + k as f32 * cfg::STAR_ADVANCE_MS / cfg::SLASH_MS;
            cuts.push(make_cut(lerp(a, b, 0.5), direction, width, height, 0.0, start, start + cfg::SLASH_DRAW_SPAN));
        }

        for slash in &cfg::FLURRY_SLASHES {
            let angle = slash.degrees * PI / 180.0;
            let heading = pt(angle.cos(), angle.sin());
            let x = 0.5 - heading.y * slash.offset * cfg::REFERENCE_HEIGHT / cfg::REFERENCE_WIDTH;
            let y = 0.5 + heading.x * slash.offset;
            let mut direction = heading;
            if diagonal > 0.0 {
                let scaled_x = heading.x / cfg::REFERENCE_WIDTH * width;
                let scaled_y = heading.y / cfg::REFERENCE_HEIGHT * height;
                let length = scaled_x.hypot(scaled_y);
                direction = pt(scaled_x / length, scaled_y / length);
            }
            let start = cfg::SLASH_FIRST_START + slash.start_ms / cfg::SLASH_MS;
            cuts.push(make_cut(pt(x * width, y * height), direction, width, height, 0.0, start, start + cfg::SLASH_DRAW_SPAN));
        }

        let mut rays = Vec::with_capacity(cfg::SLASH_RAY_COUNT);
        for i in 0..cfg::SLASH_RAY_COUNT {
            let angle = (i as f32 + rng.range(-0.3, 0.3)) * 2.0 * PI / cfg::SLASH_RAY_COUNT as f32;
            let lo = angle - cfg::SLASH_RAY_HALF_ANGLE;
            let hi = angle + cfg::SLASH_RAY_HALF_ANGLE;
            rays.push(vec![middle, pt(middle.x + diagonal * lo.cos(), middle.y + diagonal * lo.sin()), pt(middle.x + diagonal * hi.cos(), middle.y + diagonal * hi.sin())]);
        }

        let mut slivers = Vec::with_capacity(cfg::SLIVER_COUNT);
        for _ in 0..cfg::SLIVER_COUNT {
            let along = rng.unit();
            let perp = (rng.unit() - 0.5) * 2.0 * cfg::SLIVER_SPREAD_FRAC * height;
            let length = rng.range(cfg::SLIVER_LENGTH_MIN, cfg::SLIVER_LENGTH_MAX);
            let thick = rng.range(cfg::SLIVER_THICK_MIN, cfg::SLIVER_THICK_MAX);
            let drift_perp = (rng.unit() - 0.5) * 2.0 * cfg::SLIVER_DRIFT_FRAC * height;
            let drift_along = (rng.unit() - 0.5) * 2.0 * cfg::SLIVER_ALONG_FRAC * diagonal;
            slivers.push(SlashSliver { along, perp, length, thick, drift_perp, drift_along });
        }

        let mut shards: Vec<SlashPolygon> = vec![vec![pt(0.0, 0.0), pt(width, 0.0), pt(width, height), pt(0.0, height)]];
        for line in cuts.iter().chain(std::iter::once(&final_)) {
            let mut next = Vec::new();
            for piece in &shards {
                for sign in [1.0, -1.0] {
                    let half = clip_half(piece, line.center, line.direction, sign);
                    if half.len() >= 3 && slash_polygon_area(&half) > cfg::SLASH_MIN_PIECE_AREA {
                        next.push(half);
                    }
                }
            }
            shards = next;
        }

        let mut keyed: Vec<(f32, usize)> = shards
            .iter()
            .enumerate()
            .map(|(i, shard)| {
                let at = centroid(shard);
                let distance = (at.x - middle.x).hypot(at.y - middle.y);
                (distance * (1.0 + cfg::SLASH_SHARD_RANK_NOISE * rng.unit()), i)
            })
            .collect();
        keyed.sort_by(|a, b| a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)));
        let mut fall_rank = vec![0; shards.len()];
        for (position, &(_, i)) in keyed.iter().enumerate() {
            fall_rank[i] = position;
        }
        let shard_spin = (0..shards.len())
            .map(|_| {
                let sign = if rng.unit() < 0.5 { -1.0 } else { 1.0 };
                sign * rng.range(0.5, 1.0) * cfg::SLASH_SHARD_SPIN
            })
            .collect();

        SlashPlan { width, height, cuts, final_, rays, slivers, shards, fall_rank, shard_spin }
    }

    pub fn width(&self) -> f32 {
        self.width
    }

    pub fn height(&self) -> f32 {
        self.height
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn cuts(&self) -> &[SlashCut] {
        &self.cuts
    }

    fn add_cutmarks(&self, frame: &mut SlashFrame, progress: f32) {
        let time_s = progress * cfg::SLASH_MS * 0.001;
        for (k, cut) in self.cuts.iter().enumerate() {
            if progress < cut.start {
                continue;
            }
            let t = clamp01((progress - cut.start) / (cut.land - cut.start));
            let s = ease_out_cubic(t);
            let reveal = (2.0 * s).min(1.0);
            if progress < self.final_.land {
                frame.marks.push(SlashMark { a: cut.a, b: lerp(cut.a, cut.b, reveal), width: cfg::CUTMARK_WIDTH, alpha: cfg::CUTMARK_ALPHA });
            }
            if t < 1.0 && s > 0.002 {
                let pulse = 4.0 * s * (1.0 - s);
                frame.beams.push(SlashBeam { a: cut.a, b: lerp(cut.a, cut.b, reveal), width: cfg::BEAM_PEAK_FRAC * self.height * pulse, alpha: (pulse * cfg::STRIKE_PEAK).min(1.0) });
                frame.strikes.push(strike_of(cut, cfg::SLASH_STRIKE_OVERSHOOT, reveal, 4.0 * s * (1.0 - s) * cfg::STRIKE_PEAK, k as f32 * 1.7 + 1.0, 1.0, cfg::STRIKE_AMP, time_s));
            }
        }
    }

    fn add_final(&self, frame: &mut SlashFrame, progress: f32) {
        let f = &self.final_;
        if progress < f.start {
            return;
        }
        let time_s = progress * cfg::SLASH_MS * 0.001;
        let peak = cfg::FINAL_BEAM_FRAC * self.height;
        let thin = cfg::FINAL_THIN_FRAC * self.height;
        let mut sweep = 1.0;
        let mut intensity = cfg::FINAL_INTENSITY;
        let mut beam_width = 0.0;
        let mut beam_alpha = 0.0;
        let mut strike = true;
        if progress < f.land {
            sweep = clamp01((progress - f.start) / (f.land - f.start));
            beam_width = thin + (peak - thin) * sweep;
            beam_alpha = 1.0;
        } else {
            let since = progress - f.land;
            let u = since / cfg::SLASH_FINAL_BEAM_SPAN;
            if u < 1.0 {
                beam_width = peak + (thin - peak) * ease_out_cubic(u);
                beam_alpha = 1.0 - clamp01((u - 0.4) / 0.6);
            }
            let fade = since / cfg::SLASH_FINAL_FADE;
            strike = fade < 1.0;
            let crackle = 0.55 + 0.45 * (time_s * 71.0).sin() * (time_s * 127.0).sin();
            intensity = (1.0 - fade) * (1.0 - fade) * cfg::FINAL_INTENSITY * crackle;
        }
        if beam_alpha > 0.0 {
            frame.beams.push(SlashBeam { a: f.a, b: lerp(f.a, f.b, sweep), width: beam_width, alpha: beam_alpha });
        }
        if strike {
            frame.strikes.push(strike_of(f, 0.0, sweep, intensity, cfg::FINAL_SEED, cfg::FINAL_THICK, cfg::STRIKE_AMP * cfg::FINAL_AMP_MULT, time_s));
        }
    }

    fn add_slivers(&self, frame: &mut SlashFrame, progress: f32) {
        let f = &self.final_;
        if progress < f.land || progress >= f.land + cfg::SLASH_SLIVER_SPAN {
            return;
        }
        let u = (progress - f.land) / cfg::SLASH_SLIVER_SPAN;
        let along = pt(f.b.x - f.a.x, f.b.y - f.a.y);
        let length = along.x.hypot(along.y);
        if length < 1e-3 {
            return;
        }
        let along = pt(along.x / length, along.y / length);
        let normal = pt(-along.y, along.x);
        for sliver in &self.slivers {
            let at = lerp(f.a, f.b, sliver.along);
            let shift = sliver.drift_along * u;
            let lift = sliver.perp + sliver.drift_perp * u;
            let c = pt(at.x + along.x * shift + normal.x * lift, at.y + along.y * shift + normal.y * lift);
            let hl = sliver.length * 0.5;
            let ht = sliver.thick * 0.5;
            frame.slivers.push(vec![
                pt(c.x - along.x * hl - normal.x * ht, c.y - along.y * hl - normal.y * ht),
                pt(c.x + along.x * hl - normal.x * ht, c.y + along.y * hl - normal.y * ht),
                pt(c.x + along.x * hl + normal.x * ht, c.y + along.y * hl + normal.y * ht),
                pt(c.x - along.x * hl + normal.x * ht, c.y - along.y * hl + normal.y * ht),
            ]);
        }
        frame.sliver_alpha = 1.0 - u;
    }

    pub fn sample(&self, progress: f32) -> SlashFrame {
        let p = clamp01(progress);
        let (width, height) = (self.width, self.height);
        let land = self.final_.land;
        let mut frame = SlashFrame::default();

        if p < land {
            if width * height > cfg::SLASH_MIN_PIECE_AREA {
                let whole = vec![pt(0.0, 0.0), pt(width, 0.0), pt(width, height), pt(0.0, height)];
                frame.sources.push(whole.clone());
                frame.desktop.push(whole);
            }
        } else {
            frame.shattered = true;
            let fall_begin = land + cfg::SLASH_SHATTER_HOLD;
            let middle = pt(width * 0.5, height * 0.5);
            let reach = cfg::SLASH_SHARD_DRIFT * width.hypot(height);
            let n = self.shards.len();
            for (i, shard) in self.shards.iter().enumerate() {
                let share = if n > 1 { self.fall_rank[i] as f32 / (n - 1) as f32 } else { 0.0 };
                let begin = fall_begin + share * cfg::SLASH_SHATTER_SPREAD;
                let local = clamp01((p - begin) / cfg::SLASH_FALL_SPAN);
                let eased = local * local;
                let scale = (1.0 - cfg::SLASH_GAP_FRAC) * (1.0 - eased);
                if scale <= 0.001 {
                    continue;
                }
                let at = centroid(shard);
                let away = pt(at.x - middle.x, at.y - middle.y);
                let length = away.x.hypot(away.y);
                let away = if length > 1e-3 { pt(away.x / length, away.y / length) } else { pt(0.0, -1.0) };
                frame.sources.push(shard.clone());
                frame.desktop.push(transformed(shard, at, pt(away.x * reach * eased, away.y * reach * eased), scale, self.shard_spin[i] * eased));
            }
        }

        self.add_cutmarks(&mut frame, p);
        self.add_final(&mut frame, p);
        self.add_slivers(&mut frame, p);

        frame.dim = cfg::DIM_PEAK * ease_out_cubic(clamp01(p / cfg::SLASH_DIM_RAMP));
        if p >= land {
            let lift = clamp01((p - land) / cfg::SLASH_DIM_LIFT);
            frame.dim *= 1.0 - ease_out_cubic(lift);
        }

        if p >= land && p < land + cfg::SLASH_FLASH_SPAN {
            let since = p - land;
            frame.flash = if since < cfg::SLASH_FLASH_HOLD {
                1.0
            } else {
                let fade = 1.0 - (since - cfg::SLASH_FLASH_HOLD) / (cfg::SLASH_FLASH_SPAN - cfg::SLASH_FLASH_HOLD);
                fade * fade
            };
        }
        if p >= land && p < land + cfg::SLASH_IMPACT_SPAN {
            frame.rays = self.rays.clone();
        }
        if p >= land && p < land + cfg::SLASH_SHOCK_SPAN {
            frame.shock = Some(SlashShock {
                center: pt(width * 0.5, height * 0.5),
                radius: cfg::SHOCK_RADIUS_FRAC * height,
                progress: (p - land) / cfg::SLASH_SHOCK_SPAN,
                intensity: cfg::SHOCK_INTENSITY,
                time_s: p * cfg::SLASH_MS * 0.001,
            });
        }
        frame
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::PI;

    fn total_area(frame: &SlashFrame) -> f32 {
        frame.desktop.iter().map(slash_polygon_area).sum()
    }

    fn inside(frame: &SlashFrame, w: f32, h: f32) -> bool {
        frame.desktop.iter().all(|piece| piece.iter().all(|p| p.x >= -0.01 && p.y >= -0.01 && p.x <= w + 0.01 && p.y <= h + 0.01))
    }

    #[test]
    fn check_slash() {
        let w = 2560.0f32;
        let h = 1440.0f32;
        let land = cfg::SLASH_FINAL_START + cfg::SLASH_FINAL_DRAW_SPAN;
        let star_step = cfg::STAR_ADVANCE_MS / cfg::SLASH_MS;
        let plan = SlashPlan::new(7, w, h);
        let cuts = plan.cuts();
        let last_land = cuts.last().unwrap().land;

        let first = plan.sample(0.0);
        assert!(first.desktop.len() == 1 && (total_area(&first) - w * h).abs() < 1.0, "progress 0 is one full-screen desktop region");
        assert!(first.marks.is_empty() && first.strikes.is_empty() && first.flash == 0.0 && first.shock.is_none(), "progress 0 draws nothing over the desktop");

        let last = plan.sample(1.0);
        assert!(
            last.desktop.is_empty() && last.marks.is_empty() && last.strikes.is_empty() && last.rays.is_empty() && last.flash == 0.0 && last.shock.is_none(),
            "progress 1 leaves no desktop and nothing else"
        );

        let same = SlashPlan::new(7, w, h).sample(0.8);
        let mid = plan.sample(0.8);
        assert!(same.desktop.len() == mid.desktop.len() && same.desktop[0] == mid.desktop[0], "the same seed gives the same frame");
        let other = SlashPlan::new(8, w, h).sample(last_land + 0.01);
        let seven = plan.sample(last_land + 0.01);
        assert!(other.marks[0].a == seven.marks[0].a && other.marks[0].b == seven.marks[0].b, "the cutmarks follow the reference order whatever the seed");

        let mut monotonic = true;
        let mut bounded = true;
        let mut previous = w * h + 1.0;
        for i in 0..=200 {
            let p = i as f32 / 200.0;
            let frame = plan.sample(p);
            let area = total_area(&frame);
            monotonic = monotonic && area <= previous + 1.0;
            bounded = bounded && (p > land + cfg::SLASH_SHATTER_HOLD || inside(&frame, w, h));
            previous = area;
        }
        assert!(monotonic, "the desktop area never grows with progress");
        assert!(bounded, "every desktop region stays inside the output until the shards fall");

        assert!(
            plan.sample(cfg::SLASH_FIRST_START - 0.001).strikes.is_empty()
                && plan.sample(cfg::SLASH_FIRST_START + 0.005).strikes.len() == 1
                && plan.sample(cfg::SLASH_FIRST_START + star_step + 0.005).strikes.len() == 2
                && star_step < cfg::SLASH_DRAW_SPAN,
            "the cutmark slashes strike one after another, overlapping slightly"
        );
        assert_eq!(plan.sample(cfg::SLASH_FIRST_START + star_step + 0.005).marks.len(), 2, "each strike leaves a cutmark");

        assert!(cuts.len() == cfg::SLASH_COUNT && cuts.windows(2).all(|c| c[0].start <= c[1].start), "the slashes strike in the order of the reference clip");
        assert!(cuts[0].a.x < cuts[0].b.x && cuts[0].a.y < cuts[0].b.y, "the first star stroke goes down to the right");
        assert!(cuts[1].a.x > cuts[1].b.x && cuts[1].a.y > cuts[1].b.y, "the second star stroke goes up to the left");
        assert!(cuts[2].a.x < cuts[2].b.x && (cuts[2].a.y - cuts[2].b.y).abs() < 1.0, "the third star stroke runs level to the right");
        assert!(cuts[3].a.x > cuts[3].b.x && cuts[3].a.y < cuts[3].b.y, "the fourth star stroke goes down to the left");
        assert!(cuts[4].a.x < cuts[4].b.x && cuts[4].a.y > cuts[4].b.y, "the fifth star stroke goes up to the right");
        let s = cfg::STAR_COUNT;
        assert!(cuts[s].a.y > cuts[s].b.y && (cuts[s].a.x - cuts[s].b.x).abs() < w * 0.1, "the flurry opens with a stroke straight up");

        let mut core = Vec::new();
        for i in 0..cfg::STAR_COUNT {
            let p = &cuts[i];
            let q = &cuts[(i + 2) % cfg::STAR_COUNT];
            let cross = p.direction.x * q.direction.y - p.direction.y * q.direction.x;
            let t = ((q.center.x - p.center.x) * q.direction.y - (q.center.y - p.center.y) * q.direction.x) / cross;
            core.push(Point { x: p.center.x + p.direction.x * t, y: p.center.y + p.direction.y * t });
        }
        let mut clear = true;
        for cut in &cuts[cfg::STAR_COUNT..] {
            let mut below = false;
            let mut above = false;
            for p in &core {
                let side = cut.direction.x * (p.y - cut.center.y) - cut.direction.y * (p.x - cut.center.x);
                below = below || side < 0.0;
                above = above || side > 0.0;
            }
            clear = clear && !(below && above);
        }
        assert!(clear, "no flurry slash cuts through the star's inner pentagon");

        let mut spans = true;
        let mut near_centre = true;
        let mut angles = Vec::new();
        for cut in cuts {
            let on_edge = |p: Point| p.x.abs() < 0.5 || (p.x - w).abs() < 0.5 || p.y.abs() < 0.5 || (p.y - h).abs() < 0.5;
            spans = spans && on_edge(cut.a) && on_edge(cut.b);
            let miss = (cut.direction.x * (h * 0.5 - cut.center.y) - cut.direction.y * (w * 0.5 - cut.center.x)).abs();
            near_centre = near_centre && miss < h * 0.4;
            let angle = cut.direction.y.atan2(cut.direction.x);
            angles.push(if angle < 0.0 { angle + PI } else { angle });
        }
        assert!(spans, "every slash spans the display from edge to edge");
        assert!(near_centre, "every slash passes near the centre");
        angles.sort_by(f32::total_cmp);
        let mut gap = PI - angles.last().unwrap() + angles[0];
        for i in 1..angles.len() {
            gap = gap.min(angles[i] - angles[i - 1]);
        }
        assert!(gap > 0.07, "the slashes fan out over every angle");
        let mut same_plan = true;
        for seed in 1..=6 {
            let again = SlashPlan::new(seed, w, h);
            for (x, y) in again.cuts().iter().zip(cuts) {
                same_plan = same_plan && x.a == y.a && x.b == y.b;
            }
        }
        assert!(same_plan, "every seed plays the same slashes");

        let waiting = plan.sample(cfg::SLASH_FINAL_START - 0.01);
        assert!(waiting.desktop.len() == 1 && (total_area(&waiting) - w * h).abs() < 1.0, "the cutmarks leave the desktop whole");
        assert!(waiting.marks.len() == cfg::SLASH_COUNT && waiting.strikes.is_empty(), "during the wait only the cutmarks remain");
        assert!(waiting.marks.iter().all(|m| m.width == cfg::CUTMARK_WIDTH && m.alpha == cfg::CUTMARK_ALPHA), "the cutmarks are thin and steady");
        assert!((cfg::SLASH_FINAL_START - last_land) * cfg::SLASH_MS >= 500.0, "the cutmarks wait a long beat before the final slash");

        let drawing = plan.sample(cfg::SLASH_FINAL_START + cfg::SLASH_FINAL_DRAW_SPAN * 0.5);
        assert!(
            drawing.desktop.len() == 1
                && drawing.marks.len() == cfg::SLASH_COUNT
                && drawing.strikes.len() == 1
                && drawing.strikes[0].thick == cfg::FINAL_THICK
                && drawing.strikes[0].progress > 0.0
                && drawing.strikes[0].progress < 1.0,
            "the final slash is drawn on before anything breaks"
        );
        assert!(drawing.strikes[0].a.x < drawing.strikes[0].b.x && drawing.strikes[0].a.y > drawing.strikes[0].b.y, "the final slash runs from lower left to upper right");

        let slope = (drawing.strikes[0].a.y - drawing.strikes[0].b.y) / (drawing.strikes[0].b.x - drawing.strikes[0].a.x);
        assert!(slope > 0.05 && slope < 0.3, "the final slash rises at a shallow tilt, not corner to corner");

        assert!(
            plan.sample(0.0).dim == 0.0 && (plan.sample(cfg::SLASH_FINAL_START - 0.01).dim - cfg::DIM_PEAK).abs() < 0.01 && plan.sample(land + cfg::SLASH_DIM_LIFT + 0.01).dim == 0.0,
            "the desktop dims during the flurry and lifts after the final slash"
        );

        let striking = plan.sample(cfg::SLASH_FIRST_START + 0.01);
        assert!(
            striking.beams.len() == 1 && striking.beams[0].width > 0.0 && striking.beams[0].width <= cfg::BEAM_PEAK_FRAC * h + 0.01,
            "a strike carries a tapered beam that is thick as it is struck"
        );
        let ending = plan.sample(cfg::SLASH_FIRST_START + cfg::SLASH_DRAW_SPAN - 0.001);
        assert!(ending.beams.is_empty() || ending.beams[0].width < striking.beams[0].width, "the beam thins out as the strike ends");

        let before = plan.sample(land - 0.001);
        let after = plan.sample(land + 0.001);
        assert!(
            !before.shattered && before.sources.len() == before.desktop.len() && after.shattered && after.sources.len() == after.desktop.len() && after.desktop.len() > 1,
            "the desktop breaks into wallpaper shards at the final slash"
        );
        let tail = cfg::SLASH_SHATTER_HOLD + cfg::SLASH_SHATTER_SPREAD + cfg::SLASH_FALL_SPAN;
        assert!(
            plan.sample(land + tail + 0.001).desktop.is_empty() && !plan.sample(land + tail * 0.5).desktop.is_empty() && tail * cfg::SLASH_MS < 600.0,
            "the shards fall away within about half a second"
        );

        let whiteout = plan.sample(land + 0.001);
        let final_beam = whiteout.beams.iter().map(|b| b.width).fold(0.0f32, f32::max);
        assert!(final_beam > cfg::FINAL_BEAM_FRAC * h * 0.9, "the final slash lands as a beam about a third of the screen tall");
        assert!(
            !plan.sample(land + cfg::SLASH_FLASH_SPAN * 0.4).slivers.is_empty() && plan.sample(land + cfg::SLASH_SLIVER_SPAN + 0.01).slivers.is_empty(),
            "slivers fly off the final slash and are gone after a moment"
        );
        assert!(
            plan.sample(land + cfg::SLASH_FLASH_HOLD * 0.5).flash == 1.0 && plan.sample(land + cfg::SLASH_FLASH_SPAN * 0.6).flash < plan.sample(land + cfg::SLASH_FLASH_HOLD + 0.001).flash,
            "the flash holds for a frame, then decays"
        );

        let impact = plan.sample(land + cfg::SLASH_IMPACT_SPAN * 0.3);
        assert!(impact.flash > 0.0 && !impact.rays.is_empty() && impact.shock.is_some(), "the final slash lands with the impact flash, rays and shock");
        assert!(impact.desktop.len() >= cfg::SLASH_COUNT + 2 && impact.marks.is_empty(), "the screen shatters along the cutmarks and the final slash");
        assert!(
            plan.sample(0.02).flash == 0.0
                && plan.sample(land - 0.01).flash == 0.0
                && plan.sample(land + cfg::SLASH_FLASH_SPAN + 0.01).flash == 0.0
                && plan.sample(0.9).rays.is_empty()
                && plan.sample(0.9).shock.is_none(),
            "no flash before the final slash or after its impact"
        );

        let mut strikes_valid = true;
        for i in 0..=200 {
            for strike in plan.sample(i as f32 / 200.0).strikes {
                strikes_valid = strikes_valid && strike.progress >= 0.0 && strike.progress <= 1.0 && strike.intensity >= 0.0;
            }
        }
        assert!(strikes_valid, "strikes have a valid progress and intensity");

        assert!(slash_run_progress(0.0) == 0.0 && slash_run_progress(cfg::SLASH_MS * 2.0) == 1.0, "the run goes from 0 to 1 and clamps");

        let empty = SlashPlan::new(1, 0.0, 0.0);
        let tiny = SlashPlan::new(1, 1.0, 1.0);
        for i in 0..=10 {
            empty.sample(i as f32 / 10.0);
            tiny.sample(i as f32 / 10.0);
        }
        assert!(empty.sample(0.5).desktop.is_empty(), "a zero-sized output has no desktop region");
    }
}
