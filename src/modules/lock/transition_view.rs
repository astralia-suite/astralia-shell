//! Paints one frame of the slash transition from a sampled `SlashPlan`.

use crate::config::lock_config as cfg;
use crate::modules::lock::slash::{SlashBeam, SlashPlan, SlashShock, SlashStrike};
use crate::render::canvas::Canvas;
use crate::render::geometry::{Point, Rect};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, color, with_alpha};

const DIM_COLOR: Color = color(cfg::DIM_HEX);
const FLASH_COLOR: Color = color(cfg::FLASH_HEX);

/// Paints what lies under the cut pieces.
pub type PaintBase<'a, C> = &'a mut dyn FnMut(&mut C);

/// Lightning drawn by the host's shaders, through the canvas it paints on.
pub trait Thunder<C> {
    fn strike(&mut self, canvas: &mut C, strike: &SlashStrike);
    fn shock(&mut self, canvas: &mut C, shock: &SlashShock);
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransitionMode {
    Lock,
    Unlock,
}

fn stroke(canvas: &mut dyn Canvas, a: Point, b: Point, width: f32, color: Color) {
    let (dx, dy) = (b.x - a.x, b.y - a.y);
    let length = dx.hypot(dy);
    if length < 1e-4 {
        return;
    }
    let nx = -dy / length * width * 0.5;
    let ny = dx / length * width * 0.5;
    let quad = [Point { x: a.x + nx, y: a.y + ny }, Point { x: b.x + nx, y: b.y + ny }, Point { x: b.x - nx, y: b.y - ny }, Point { x: a.x - nx, y: a.y - ny }];
    canvas.polygon(&quad, color);
}

fn beam(canvas: &mut dyn Canvas, beam: &SlashBeam, electro: Color) {
    let (dx, dy) = (beam.b.x - beam.a.x, beam.b.y - beam.a.y);
    let length = dx.hypot(dy);
    if length < 1e-4 || beam.width <= 0.0 {
        return;
    }
    let middle = Point { x: (beam.a.x + beam.b.x) * 0.5, y: (beam.a.y + beam.b.y) * 0.5 };
    let (nx, ny) = (-dy / length, dx / length);
    let shape = |width: f32| {
        let half = width * 0.5;
        [beam.a, Point { x: middle.x + nx * half, y: middle.y + ny * half }, beam.b, Point { x: middle.x - nx * half, y: middle.y - ny * half }]
    };
    canvas.polygon(&shape(beam.width * cfg::BEAM_GLOW_SCALE), with_alpha(electro, cfg::BEAM_GLOW_ALPHA * beam.alpha));
    canvas.polygon(&shape(beam.width), with_alpha(FLASH_COLOR, beam.alpha));
}

fn fallback_strike(canvas: &mut dyn Canvas, strike: &SlashStrike, electro: Color) {
    let tip = Point { x: strike.a.x + (strike.b.x - strike.a.x) * strike.progress, y: strike.a.y + (strike.b.y - strike.a.y) * strike.progress };
    let width = cfg::FALLBACK_BOLT_WIDTH * strike.thick;
    let alpha = strike.intensity.clamp(0.0, 1.0);
    stroke(canvas, strike.a, tip, width * cfg::BOLT_GLOW_FAR_SCALE, with_alpha(electro, cfg::BOLT_GLOW_FAR_ALPHA * alpha));
    stroke(canvas, strike.a, tip, width * cfg::BOLT_GLOW_NEAR_SCALE, with_alpha(electro, cfg::BOLT_GLOW_NEAR_ALPHA * alpha));
    stroke(canvas, strike.a, tip, width, with_alpha(WHITE, alpha));
}

/// Draws the transition at `progress`. Locking paints `paint_base` (the lock card) under the cut desktop;
/// unlocking cuts the held lock picture instead and skips the base.
pub fn paint_transition<C: Canvas>(canvas: &mut C, plan: &SlashPlan, progress: f32, paint_base: Option<PaintBase<C>>, mut thunder: Option<&mut dyn Thunder<C>>, mode: TransitionMode) {
    let unlock = mode == TransitionMode::Unlock;
    if let Some(base) = paint_base
        && !unlock
    {
        base(canvas);
    }
    let electro = palette().electro;
    let frame = plan.sample(progress);

    for (i, piece) in frame.desktop.iter().enumerate() {
        match frame.sources.get(i) {
            Some(source) if frame.shattered || unlock => canvas.snapshot_polygon(source, piece, 1.0),
            _ => canvas.cut_polygon(piece),
        }
        if frame.dim > 0.0 {
            canvas.polygon(piece, with_alpha(DIM_COLOR, frame.dim));
        }
    }

    for mark in &frame.marks {
        stroke(canvas, mark.a, mark.b, mark.width, with_alpha(FLASH_COLOR, mark.alpha));
    }
    for b in &frame.beams {
        beam(canvas, b, electro);
    }
    for ray in &frame.rays {
        canvas.polygon(ray, with_alpha(WHITE, cfg::SLASH_RAY_ALPHA * frame.flash));
    }
    for strike in &frame.strikes {
        match thunder.as_deref_mut() {
            Some(t) => t.strike(canvas, strike),
            None => fallback_strike(canvas, strike, electro),
        }
    }
    if let (Some(shock), Some(t)) = (&frame.shock, thunder) {
        t.shock(canvas, shock);
    }
    for sliver in &frame.slivers {
        canvas.polygon(sliver, with_alpha(FLASH_COLOR, cfg::SLIVER_ALPHA * frame.sliver_alpha));
    }
    if frame.flash > 0.0 {
        canvas.rect(Rect { x: 0.0, y: 0.0, w: plan.width(), h: plan.height() }, with_alpha(FLASH_COLOR, cfg::SLASH_FLASH_PEAK * frame.flash));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modules::lock::slash::SlashPlan;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    #[derive(Default)]
    struct Counts {
        strikes: usize,
        shocks: usize,
    }

    impl Thunder<RecordingCanvas> for Counts {
        fn strike(&mut self, _canvas: &mut RecordingCanvas, _strike: &SlashStrike) {
            self.strikes += 1;
        }
        fn shock(&mut self, _canvas: &mut RecordingCanvas, _shock: &SlashShock) {
            self.shocks += 1;
        }
    }

    #[test]
    fn transition_view_ops() {
        let (w, h) = (2560.0, 1440.0);
        let land = cfg::SLASH_FINAL_START + cfg::SLASH_FINAL_DRAW_SPAN;
        let plan = SlashPlan::new(7, w, h);
        let mut base = |c: &mut RecordingCanvas| c.rect(Rect { x: 0.0, y: 0.0, w, h }, Color::default());
        let mut canvas = RecordingCanvas::default();

        paint_transition(&mut canvas, &plan, 0.0, Some(&mut base), None, TransitionMode::Lock);
        assert!(canvas.ops.len() == 2 && canvas.ops[0].kind == OpKind::Rect && canvas.ops[1].kind == OpKind::CutPolygon, "progress 0 paints the base and cuts the whole screen");

        let mut thunder = Counts::default();
        canvas.ops.clear();
        paint_transition(&mut canvas, &plan, cfg::SLASH_FIRST_START + 0.005, Some(&mut base), Some(&mut thunder), TransitionMode::Lock);
        assert!(thunder.strikes == 1 && thunder.shocks == 0 && canvas.count(OpKind::Polygon) >= 4, "a strike goes to the thunder hook, beside its beam, cutmark and the dimming");

        canvas.ops.clear();
        thunder.strikes = 0;
        paint_transition(&mut canvas, &plan, land + cfg::SLASH_IMPACT_SPAN * 0.3, Some(&mut base), Some(&mut thunder), TransitionMode::Lock);
        assert!(
            thunder.shocks == 1
                && thunder.strikes == 1
                && canvas.ops[0].kind == OpKind::Rect
                && canvas.ops.last().unwrap().kind == OpKind::Rect
                && canvas.count(OpKind::SnapshotPolygon) >= cfg::SLASH_COUNT + 2
                && canvas.count(OpKind::CutPolygon) == 0,
            "the impact frame draws the wallpaper shards, calls the thunder, then flashes"
        );

        canvas.ops.clear();
        paint_transition(&mut canvas, &plan, cfg::SLASH_FIRST_START + 0.005, Some(&mut base), None, TransitionMode::Lock);
        assert!(canvas.count(OpKind::Polygon) > 1, "without a thunder hook the strike falls back to plain strokes");

        canvas.ops.clear();
        paint_transition(&mut canvas, &plan, 0.0, Some(&mut base), None, TransitionMode::Unlock);
        assert!(canvas.ops.len() == 1 && canvas.ops[0].kind == OpKind::SnapshotPolygon, "unlocking at progress 0 paints the held lock picture and no base");
        canvas.ops.clear();
        paint_transition(&mut canvas, &plan, cfg::SLASH_FIRST_START + 0.005, Some(&mut base), None, TransitionMode::Unlock);
        assert!(
            canvas.count(OpKind::SnapshotPolygon) == 1 && canvas.count(OpKind::CutPolygon) == 0 && canvas.count(OpKind::Rect) == 0,
            "unlocking cuts the lock picture, never the desktop, and skips the base"
        );

        canvas.ops.clear();
        paint_transition(&mut canvas, &plan, 1.0, Some(&mut base), None, TransitionMode::Lock);
        assert!(canvas.ops.len() == 1 && canvas.ops[0].kind == OpKind::Rect, "progress 1 paints only the base");
    }
}
