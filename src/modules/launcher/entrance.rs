//! The launcher's entrance: a bolt traces the card outline, a strike falls onto it, and the card
//! snaps in under a flash and a burst. Sampled at a time, so it is tested without a clock.

use crate::base::animation::{Easing, apply_easing};
use crate::config::launcher_config as cfg;
use crate::render::geometry::Rect;

const TRACE_START_MS: f32 = 30.0;
const TRACE_END_MS: f32 = 380.0;
const BLAST_MS: f32 = 490.0;
const FALL_MS: f32 = 60.0;
const CHARGE_LEAD_MS: f32 = 40.0;
const SPARK_GROW_MS: f32 = 50.0;
const FORK_DELAY_MS: f32 = 25.0;
const STRIKE_FADE_MS: f32 = 240.0;
const FLASH_PEAK: f32 = 0.6;
const FLASH_MS: f32 = 140.0;
const OUTLINE_FADE_MS: f32 = 240.0;
const BRANCH_MS: f32 = 150.0;
const BRANCH_LENGTH: f32 = 20.0;
const CHARGE_GAIN: f32 = 0.5;
const SCRIM_PEAK: f32 = 0.35;
const SCRIM_IN_MS: f32 = 300.0;
const SCRIM_OUT_MS: f32 = 300.0;
const BURST_MS: f32 = 280.0;

/// One bolt for the thunder renderer.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EntranceBolt {
    pub ax: f32,
    pub ay: f32,
    pub bx: f32,
    pub by: f32,
    pub progress: f32,
    pub intensity: f32,
    pub seed: f32,
    pub amp: f32,
    pub thick: f32,
    pub stitch: bool,
}

/// One rectangle shock wave around the card.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EntranceBurst {
    pub rect: Rect,
    pub reach: f32,
    pub progress: f32,
    pub intensity: f32,
}

/// Where the entrance is, from 0 to 1, and its random seed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EntranceState {
    pub t: f32,
    pub seed: f32,
}

impl Default for EntranceState {
    fn default() -> Self {
        EntranceState { t: 1.0, seed: 0.0 }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EntranceFrame {
    pub opacity: f32,
    pub scrim: f32,
    pub flash: f32,
    pub time_s: f32,
    pub bolts: Vec<EntranceBolt>,
    pub bursts: Vec<EntranceBurst>,
}

fn seg(ms: f32, from: f32, to: f32) -> f32 {
    ((ms - from) / (to - from)).clamp(0.0, 1.0)
}

/// The time at which `EaseInOutCubic` reaches `p`.
fn ease_in_out_cubic_inverse(p: f32) -> f32 {
    let p = p.clamp(0.0, 1.0);
    if p < 0.5 { (p / 4.0).cbrt() } else { 1.0 - (2.0 * (1.0 - p)).cbrt() / 2.0 }
}

fn hash01(seed: f32, a: i32) -> f32 {
    let mut x = ((seed * 1000.0) as u32).wrapping_mul(2_654_435_761);
    x ^= (a as u32).wrapping_mul(40503);
    x ^= x >> 15;
    x = x.wrapping_mul(2_246_822_519);
    x ^= x >> 13;
    (x & 0xFF_FFFF) as f32 / 16_777_216.0
}

#[allow(clippy::too_many_arguments)]
fn bolt(ax: f32, ay: f32, bx: f32, by: f32, progress: f32, intensity: f32, seed: f32, amp: f32, thick: f32, stitch: bool) -> EntranceBolt {
    EntranceBolt { ax, ay, bx, by, progress, intensity, seed, amp, thick, stitch }
}

/// The entrance at `state.t` around the card `r`.
pub fn entrance_sample(state: &EntranceState, r: Rect) -> EntranceFrame {
    let ms = state.t.clamp(0.0, 1.0) * cfg::ENTRANCE_MS;
    let mut frame = EntranceFrame {
        time_s: ms / 1000.0,
        opacity: if ms >= BLAST_MS { 1.0 } else { 0.0 },
        flash: if ms >= BLAST_MS { FLASH_PEAK * (1.0 - seg(ms, BLAST_MS, BLAST_MS + FLASH_MS)) } else { 0.0 },
        scrim: SCRIM_PEAK * seg(ms, 0.0, SCRIM_IN_MS) * (1.0 - seg(ms, BLAST_MS, BLAST_MS + SCRIM_OUT_MS)),
        ..EntranceFrame::default()
    };

    let x = [r.x, r.x + r.w, r.x + r.w, r.x];
    let y = [r.y, r.y, r.y + r.h, r.y + r.h];
    let length = [r.w, r.h, r.w, r.h];
    const DIR_X: [f32; 4] = [-1.0, 1.0, 1.0, -1.0];
    const DIR_Y: [f32; 4] = [-1.0, -1.0, 1.0, 1.0];
    let perimeter = 2.0 * (r.w + r.h);
    let head = perimeter * apply_easing(Easing::EaseInOutCubic, seg(ms, TRACE_START_MS, TRACE_END_MS));
    let charge = 1.0 + CHARGE_GAIN * seg(ms, TRACE_END_MS - CHARGE_LEAD_MS, BLAST_MS);
    let outline = if ms < BLAST_MS { charge } else { 1.0 - seg(ms, BLAST_MS, BLAST_MS + OUTLINE_FADE_MS) };

    let add_spark = |bolts: &mut Vec<EntranceBolt>, i: usize, arrival: f32| {
        let grow = seg(ms, arrival, arrival + SPARK_GROW_MS);
        let fade = 1.0 - seg(ms, arrival + SPARK_GROW_MS, arrival + SPARK_GROW_MS + BRANCH_MS);
        if grow > 0.0 && fade > 0.0 {
            let (bx, by) = (x[i] + DIR_X[i] * BRANCH_LENGTH, y[i] + DIR_Y[i] * BRANCH_LENGTH);
            bolts.push(bolt(x[i], y[i], bx, by, grow, fade, state.seed + 7.0 + i as f32, 6.0, 0.6, true));
        }
    };

    let mut start = 0.0;
    for i in 0..4 {
        let next = (i + 1) % 4;
        let progress = ((head - start) / length[i]).clamp(0.0, 1.0);
        if progress > 0.0 && outline > 0.0 {
            frame.bolts.push(bolt(x[i], y[i], x[next], y[next], progress, outline, state.seed + i as f32, 4.0, 0.6, true));
        }
        add_spark(&mut frame.bolts, i, TRACE_START_MS + (TRACE_END_MS - TRACE_START_MS) * ease_in_out_cubic_inverse(start / perimeter));
        start += length[i];
    }
    add_spark(&mut frame.bolts, 0, TRACE_END_MS);

    let cx = r.x + r.w / 2.0;
    let jitter = (hash01(state.seed, 0) - 0.5) * 120.0;
    let land = (hash01(state.seed, 1) - 0.5) * 40.0;
    let side = if hash01(state.seed, 2) < 0.5 { -1.0 } else { 1.0 };
    let strike = if ms < BLAST_MS { 1.0 } else { 1.0 - seg(ms, BLAST_MS + 60.0, BLAST_MS + STRIKE_FADE_MS + 60.0) };
    let drop = seg(ms, BLAST_MS - FALL_MS, BLAST_MS);
    let fork = seg(ms, BLAST_MS - FALL_MS + FORK_DELAY_MS, BLAST_MS + FORK_DELAY_MS);
    if drop > 0.0 && strike > 0.0 {
        frame.bolts.push(bolt(cx + jitter, -10.0, cx + land, r.y, drop, strike, state.seed + 11.0, 22.0, 1.0, false));
    }
    if fork > 0.0 && strike > 0.0 {
        frame.bolts.push(bolt(cx - jitter * 1.6, -10.0, cx + side * 90.0, r.y, fork, 0.6 * strike, state.seed + 14.0, 18.0, 0.8, false));
    }

    let blast = seg(ms, BLAST_MS, BLAST_MS + BURST_MS);
    if ms >= BLAST_MS && blast < 1.0 {
        frame.bursts.push(EntranceBurst { rect: r, reach: cfg::BLAST_REACH, progress: blast, intensity: 1.0 });
    }
    frame
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::launcher_config as cfg;

    const BOX: Rect = Rect { x: 610.0, y: 400.0, w: 700.0, h: 300.0 };

    fn at(ms: f32) -> EntranceFrame {
        entrance_sample(&EntranceState { t: ms / cfg::ENTRANCE_MS, seed: 3.0 }, BOX)
    }

    fn lit_fraction(frame: &EntranceFrame) -> f32 {
        let lit: f32 = frame.bolts.iter().filter(|b| b.amp == 4.0).map(|b| b.progress * (b.bx - b.ax).hypot(b.by - b.ay)).sum();
        lit / (2.0 * (BOX.w + BOX.h))
    }

    fn sky_intensity(frame: &EntranceFrame) -> f32 {
        frame.bolts.iter().filter(|b| b.amp == 22.0).map(|b| b.intensity).fold(0.0, f32::max)
    }

    #[test]
    fn launcher_entrance_ends() {
        let start = at(0.0);
        assert!(
            start.opacity == 0.0 && start.bolts.is_empty() && start.bursts.is_empty() && start.scrim == 0.0 && start.flash == 0.0,
            "the entrance starts empty and hidden"
        );
        let done = at(cfg::ENTRANCE_MS);
        assert!(
            done.opacity == 1.0 && done.bolts.is_empty() && done.bursts.is_empty() && done.scrim == 0.0 && done.flash == 0.0,
            "the entrance ends on the plain card"
        );
    }

    #[test]
    fn launcher_entrance_trace() {
        let tracing = at(200.0);
        let on_border = !tracing.bolts.is_empty()
            && tracing.bolts.iter().all(|b| b.ax >= BOX.x - 25.0 && b.ax <= BOX.x + BOX.w + 25.0 && b.ay >= BOX.y - 25.0 && b.ay <= BOX.y + BOX.h + 25.0);
        assert!(on_border, "the bolt stays on the card outline");
        assert!(tracing.opacity == 0.0 && tracing.bursts.is_empty() && tracing.flash == 0.0, "the card stays hidden and nothing flashes while the bolt traces it");

        let mut last = 0.0;
        let mut grows = true;
        for step in 1..=40 {
            let lit: f32 = at(step as f32 * 10.0).bolts.iter().filter(|b| b.amp == 4.0).map(|b| b.progress).sum();
            grows &= lit >= last;
            last = lit;
        }
        assert!(grows, "the traced outline only grows");
        assert!(at(430.0).bolts.len() >= 4, "the whole outline is lit when the trace ends");
        assert!(lit_fraction(&at(380.0)) >= 0.99, "the trace closes the outline when it ends");

        let (mut widest, mut before) = (0.0f32, 0.0);
        let mut ms = 30.0;
        while ms <= 380.0 {
            let lit = lit_fraction(&at(ms));
            widest = widest.max(lit - before);
            before = lit;
            ms += 8.0;
        }
        assert!(widest < 0.12, "the trace never jumps between frames");
        assert!(lit_fraction(&at(38.0)) < 0.01 && lit_fraction(&at(380.0)) - lit_fraction(&at(372.0)) < 0.03, "the trace eases in and eases out");

        let stitched = [250.0, 450.0].iter().flat_map(|&ms| at(ms).bolts).all(|b| b.stitch == (b.amp == 4.0 || b.amp == 6.0));
        assert!(stitched, "only the outline and its sparks use the stitched bolt");
    }

    #[test]
    fn launcher_entrance_sparks() {
        assert!(at(50.0).bolts.iter().any(|b| b.amp == 6.0 && b.progress > 0.0 && b.progress < 1.0), "a corner spark grows instead of popping in");
        let closing = at(385.0).bolts.iter().filter(|b| b.amp == 6.0 && b.ax == BOX.x && b.ay == BOX.y).count();
        assert_eq!(closing, 1, "a spark marks the corner where the trace closes");
        assert_eq!(at(700.0).bolts.iter().filter(|b| b.amp == 6.0).count(), 0, "the sparks are gone before the entrance ends");
    }

    #[test]
    fn launcher_entrance_steady_trace() {
        let steady = (0..=41).flat_map(|i| at(i as f32 * 8.0).bolts).filter(|b| b.amp == 4.0).all(|b| b.intensity == 1.0);
        assert!(steady, "the outline bolt never flickers while it traces");
    }

    #[test]
    fn launcher_entrance_fall() {
        let fall = at(450.0);
        let charged = fall.bolts.iter().any(|b| b.amp == 4.0 && b.intensity > 1.0);
        let falling = fall.bolts.iter().any(|b| b.amp == 22.0 && b.ay < 0.0 && b.by == BOX.y && b.progress > 0.0 && b.progress < 1.0);
        assert!(fall.opacity == 0.0 && fall.bursts.is_empty() && fall.flash == 0.0, "the card and the flash wait for the strike");
        assert!(charged && falling, "a bolt falls from the top onto the card while the outline charges");
    }

    #[test]
    fn launcher_entrance_strike() {
        let before = at(480.0);
        assert!(before.opacity == 0.0 && before.flash == 0.0, "nothing lands before the bolt arrives");
        let strike = at(500.0);
        assert!(strike.opacity == 1.0 && strike.flash > 0.4, "the card snaps in whole under a bright flash on impact");
        assert!(strike.bolts.iter().any(|b| b.amp == 22.0 && b.progress == 1.0 && b.by == BOX.y), "the bolt has reached the card top");
        assert!(at(560.0).flash < strike.flash && at(650.0).flash == 0.0, "the flash decays quickly");
        assert!(sky_intensity(&at(700.0)) < sky_intensity(&strike), "the sky bolt fades after the strike");
    }

    #[test]
    fn launcher_entrance_blast() {
        let blast = at(500.0);
        let b = &blast.bursts;
        assert!(b.len() == 1 && b[0].rect.x == BOX.x && b[0].rect.w == BOX.w && b[0].rect.h == BOX.h && b[0].reach > 0.0, "the burst follows the card rectangle and reaches past it");
        let later = at(600.0);
        assert!(later.bursts.len() == 1 && later.bursts[0].progress > b[0].progress, "there is a single burst and it keeps spreading");
        assert!(at(540.0).bursts[0].progress > 0.1, "the burst moves quickly");
        assert!(at(780.0).bursts.is_empty(), "the burst is over before the entrance ends");
    }

    #[test]
    fn launcher_entrance_scrim() {
        assert_eq!(at(0.0).scrim, 0.0, "the backdrop starts undimmed");
        let (mut peak, mut before, mut gentle) = (0.0f32, 0.0f32, true);
        let mut ms = 0.0;
        while ms <= cfg::ENTRANCE_MS {
            let scrim = at(ms).scrim;
            peak = peak.max(scrim);
            gentle &= (scrim - before).abs() < 0.05;
            before = scrim;
            ms += 8.0;
        }
        assert!(peak > 0.2 && peak <= 0.4 && gentle, "the backdrop dims and returns gradually");
    }

    #[test]
    fn launcher_entrance_ranges() {
        let mut sane = true;
        let mut ms = 0.0;
        while ms <= cfg::ENTRANCE_MS {
            let f = at(ms);
            sane &= (f.opacity == 0.0 || f.opacity == 1.0) && (0.0..=1.0).contains(&f.flash);
            for b in &f.bolts {
                sane &= b.ax.is_finite() && b.by.is_finite() && b.progress > 0.0 && b.progress <= 1.0 && b.intensity > 0.0 && b.intensity <= 1.5;
            }
            for burst in &f.bursts {
                sane &= burst.reach > 0.0 && (0.0..1.0).contains(&burst.progress);
            }
            ms += 5.0;
        }
        assert!(sane, "every frame stays within range");
    }
}
