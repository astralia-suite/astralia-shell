//! The bar's background: the continuous strip, or okinami's rail, islands and fillets.

use super::chips::paint_ringed;
use super::style::{Frame, StyleSpec};
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;

/// Drawn under the items: islands hang from the rail, outline first, then their fill cut in with erase.
pub fn frame_base(canvas: &mut dyn Canvas, style: &StyleSpec, frame: &Frame, width: f32, height: f32) {
    if style.continuous {
        paint_ringed(canvas, style, Rect { x: 0.0, y: 0.0, w: width, h: height }, style.bg);
        return;
    }
    if !style.has_rail() {
        return;
    }
    let radius = style.island_radius;
    let bw = style.border_width;
    canvas.rect(Rect { x: 0.0, y: 0.0, w: width, h: style.rail_height }, style.border);
    for island in &frame.islands {
        canvas.rounded(Rect { x: island.outer_x, y: -radius, w: island.outer_width, h: height + radius }, radius, style.border, 0.0, style.border);
    }
    canvas.set_erase(true);
    canvas.rect(Rect { x: 0.0, y: 0.0, w: width, h: style.rail_height - bw }, style.bg);
    for island in &frame.islands {
        canvas.rounded(Rect { x: island.inner_x, y: -radius, w: island.inner_width, h: height + radius - bw }, radius - bw, style.bg, 0.0, style.bg);
    }
    canvas.set_erase(false);
}

/// Drawn over the items: the concave fillets where islands meet the rail, and the hug corners under a flush bar.
#[allow(clippy::too_many_arguments)]
pub fn frame_overlay(canvas: &mut dyn Canvas, style: &StyleSpec, frame: &Frame, width: f32, height: f32, hug_radius: f32, left_flush: bool, right_flush: bool) {
    if !style.has_rail() {
        return;
    }
    let bw = style.border_width;
    let fr = style.fillet_radius;
    for f in &frame.fillets {
        let x = if f.right_of_island { f.edge_x } else { f.edge_x - fr };
        canvas.fillet(Rect { x, y: style.rail_height, w: fr, h: fr }, f.right_of_island, style.border);
    }
    canvas.set_erase(true);
    for f in &frame.fillets {
        let x = if f.right_of_island { f.edge_x - bw } else { f.edge_x - fr };
        canvas.fillet(Rect { x, y: style.rail_height - bw, w: fr + bw, h: fr + bw }, f.right_of_island, style.bg);
    }
    canvas.set_erase(false);
    if hug_radius <= 0.0 {
        return;
    }
    if left_flush {
        canvas.fillet(Rect { x: 0.0, y: height, w: hug_radius, h: hug_radius }, true, style.border);
        canvas.set_erase(true);
        canvas.fillet(Rect { x: -bw, y: height - bw, w: hug_radius + bw, h: hug_radius + bw }, true, style.bg);
        canvas.set_erase(false);
    }
    if right_flush {
        canvas.fillet(Rect { x: width - hug_radius, y: height, w: hug_radius, h: hug_radius }, false, style.border);
        canvas.set_erase(true);
        canvas.fillet(Rect { x: width - hug_radius, y: height - bw, w: hug_radius + bw, h: hug_radius + bw }, false, style.bg);
        canvas.set_erase(false);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modules::bar::paint::style::{BarStyle, IslandSpan, frame, style_spec};
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    #[test]
    fn bar_frame() {
        let okinami = style_spec(BarStyle::Okinami, false);
        let f = frame(&okinami, Some(300.0), Some(IslandSpan { left: 500.0, right: 700.0 }), Some(900.0), 1000.0);

        let mut base = RecordingCanvas::default();
        frame_base(&mut base, &okinami, &f, 1000.0, 40.0);
        assert!(base.count(OpKind::Rounded) == 2 * f.islands.len() && base.count(OpKind::Rect) == 2, "the base draws the rail and each island twice, outline then fill");
        let last = base.ops.last().unwrap();
        assert!(base.count(OpKind::Erase) == 2 && last.kind == OpKind::Erase && last.value == 0.0, "the base ends with erase off");

        let mut overlay = RecordingCanvas::default();
        frame_overlay(&mut overlay, &okinami, &f, 1000.0, 40.0, 0.0, true, true);
        assert_eq!(overlay.count(OpKind::Fillet), 2 * f.fillets.len(), "each fillet draws an outline and an inner cutout");
        let last = overlay.ops.last().unwrap();
        assert!(last.kind == OpKind::Erase && last.value == 0.0, "the overlay ends with erase off");
        let outer = &overlay.ops[0];
        assert!(outer.kind == OpKind::Fillet && outer.rect.w == okinami.fillet_radius && outer.rect.h == okinami.fillet_radius && outer.rect.y == okinami.rail_height, "the outer fillet is a square of the fillet radius under the rail");

        let mut hugged = RecordingCanvas::default();
        frame_overlay(&mut hugged, &okinami, &f, 1000.0, 40.0, 12.0, true, true);
        assert_eq!(hugged.count(OpKind::Fillet), overlay.count(OpKind::Fillet) + 4, "hug corners add an outline and a cutout per flush side");

        let mut pill = RecordingCanvas::default();
        frame_overlay(&mut pill, &style_spec(BarStyle::Continuous, false), &f, 1000.0, 40.0, 12.0, true, true);
        assert!(pill.ops.is_empty(), "a style without a rail has no overlay");
    }
}
