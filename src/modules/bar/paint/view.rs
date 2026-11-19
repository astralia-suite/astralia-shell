//! Items, the status pill, workspaces, the dock and the dividers, in that order.

use super::chips::{IconCell, chip_fill, paint_badge, paint_icon_label, paint_icon_row, paint_pill, paint_two_lines};
use super::style::StyleSpec;
use crate::config::bar_layout;
use crate::modules::bar::model::zones::STATUS_MEMBERS;
use crate::modules::bar::model::{Item, ItemView, Model, WorkspaceSlot};
use crate::render::canvas::{Canvas, NO_IMAGE};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{self, Color, metrics, with_alpha};
use crate::service::icon::window_icon_path;

fn function_fill(id: Item, style: &StyleSpec) -> Color {
    if style.uniform_fill {
        return chip_fill();
    }
    let p = palette();
    match id {
        Item::Resource | Item::Battery => p.pop_c,
        Item::Network | Item::Media => p.accent_alt,
        Item::Bluetooth | Item::Date => p.pop_a,
        Item::Volume | Item::Time => p.accent,
        Item::Brightness => p.pop_b,
        _ => style.bg,
    }
}

fn workspace_fill(ws: &WorkspaceSlot, active: Color, occupied: Color, empty: Color) -> Color {
    if ws.active {
        active
    } else if ws.occupied {
        occupied
    } else {
        empty
    }
}

fn paint_item(canvas: &mut dyn Canvas, style: &StyleSpec, view: &ItemView, id: Item) {
    let Some(glyph) = view.glyph else { return };
    if !view.visible || view.grouped || view.rect.w <= 0.0 {
        return;
    }
    paint_pill(canvas, style, view.rect, view.tint.unwrap_or_else(|| function_fill(id, style)));
    if !view.detail.is_empty() {
        paint_two_lines(canvas, style, view.rect, glyph, &view.label, &view.detail, view.expand);
    } else {
        paint_icon_label(canvas, style, view.rect, glyph, &view.label, view.expand);
    }
    if view.alert {
        canvas.rounded(view.rect, metrics::RADIUS_WIDGET, palette().critical_alpha15, 0.0, Color::default());
    }
}

fn paint_status(canvas: &mut dyn Canvas, model: &Model) {
    let layout = model.layout_ref();
    if layout.status.w <= 0.0 {
        return;
    }
    let cells: Vec<IconCell> = STATUS_MEMBERS
        .iter()
        .map(|&id| model.item(id))
        .filter(|v| v.visible)
        .filter_map(|v| Some(IconCell { glyph: v.glyph?, icon_w: v.icon_w, label: &v.label, label_w: v.label_w, amount: v.expand }))
        .collect();
    paint_icon_row(canvas, model.style(), layout.status, &cells, model.style().icon_gap);
}

fn paint_workspaces(canvas: &mut dyn Canvas, model: &Model) {
    let layout = model.layout_ref();
    if layout.workspaces.w <= 0.0 {
        return;
    }
    let style = model.style();
    let p = palette();
    if style.workspace_badges {
        for ws in model.workspaces() {
            paint_badge(canvas, style, ws.rect, &ws.id.to_string(), workspace_fill(ws, p.pop_a, p.pop_b, p.accent));
        }
        return;
    }
    paint_pill(canvas, style, layout.workspaces, style.bg);
    let pill = StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: 0.0, ..StickerStyle::default() };
    for ws in model.workspaces() {
        sticker(canvas, ws.rect, ws.rect.h / 2.0, workspace_fill(ws, p.accent, p.pop_b, p.paper), &pill);
    }
}

fn paint_dock(canvas: &mut dyn Canvas, model: &Model) {
    let layout = model.layout_ref();
    if layout.dock.w <= 0.0 {
        return;
    }
    let style = model.style();
    paint_pill(canvas, style, layout.dock, style.bg);
    let icon = bar_layout::DOCK_ICON;
    let pad = layout.height * style.pad_ratio;
    let y = ((layout.height - icon) / 2.0 - style.text_lift).round();
    for slot in model.dock() {
        let path = window_icon_path(&slot.entry.window_class);
        if path.is_empty() {
            continue;
        }
        let image = canvas.image(&path, icon as i32);
        if image == NO_IMAGE {
            continue;
        }
        let size = canvas.image_size(image);
        let scale = if size.w > 0.0 && size.h > 0.0 { (icon / size.w).min(icon / size.h) } else { 1.0 };
        let (w, h) = ((size.w * scale).round(), (size.h * scale).round());
        let x = (layout.dock.x + pad + slot.x).round();
        let focus = if slot.entry.focused { bar_layout::DOCK_FOCUSED_ALPHA } else { bar_layout::DOCK_UNFOCUSED_ALPHA };
        let r = Rect { x: x + ((icon - w) / 2.0).round(), y: y + ((icon - h) / 2.0).round(), w, h };
        canvas.draw_image(image, r, with_alpha(tokens::WHITE, focus * slot.appear));
    }
}

/// Everything over the frame base, in the order the frame overlay expects to cover.
pub fn paint(canvas: &mut dyn Canvas, model: &Model) {
    let style = model.style();
    for id in Item::ALL {
        paint_item(canvas, style, model.item(id), id);
    }
    paint_status(canvas, model);
    paint_workspaces(canvas, model);
    paint_dock(canvas, model);
    let layout = model.layout_ref();
    let h = layout.height * bar_layout::DIVIDER_HEIGHT_RATIO;
    for &x in &layout.dividers {
        canvas.rect(Rect { x: x.floor(), y: (layout.height - h) / 2.0, w: bar_layout::DIVIDER_WIDTH, h }, palette().text_alpha20);
    }
}

#[cfg(test)]
mod tests {
    use std::rc::Rc;
    use std::time::Instant;

    use super::*;
    use crate::base::animation;
    use crate::modules::bar::model::Sources;
    use crate::modules::bar::paint::style::{BarStyle, style_spec};
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};
    use crate::service::compositor::{CompositorClient, CompositorState, MonitorWorkspaces, NullCompositor, Workspace};

    fn model(style: BarStyle) -> (Model, RecordingCanvas) {
        animation::set_instant(true);
        let mut state = CompositorState::default();
        let ws = |id| Workspace { id, name: id.to_string(), occupied: true };
        state.by_monitor.insert("DP-1".into(), MonitorWorkspaces { active_id: 2, workspaces: vec![ws(1), ws(2)] });
        state.clients.push(CompositorClient { address: "0x1".into(), window_class: "kitty".into(), workspace_id: 2, ..Default::default() });
        let compositor = Rc::new(NullCompositor::default());
        compositor.set_state(state);
        let sources = Sources {
            audio: None,
            battery: None,
            bluetooth: None,
            network: None,
            brightness: None,
            compositor,
            tray_occupied: Some(Rc::new(|| true)),
            output: "DP-1".into(),
            resource: false,
            workspace_slots: 0,
        };
        let now = Instant::now();
        let mut model = Model::new(sources, now);
        model.set_style(&style_spec(style, true));
        model.refresh(now);
        let mut canvas = RecordingCanvas::default();
        model.layout(&mut canvas, 1000.0);
        animation::set_instant(false);
        (model, canvas)
    }

    /// One letter per operation; dock icons depend on the machine's icon theme and are left out.
    fn signature(canvas: &RecordingCanvas) -> String {
        canvas
            .ops
            .iter()
            .filter_map(|op| match op.kind {
                OpKind::Rect => Some('r'),
                OpKind::Rounded => Some('R'),
                OpKind::Text => Some('T'),
                OpKind::BeginGroup => Some('G'),
                OpKind::EndGroup => Some('E'),
                OpKind::Image => None,
                other => panic!("unexpected {other:?}"),
            })
            .collect()
    }

    #[test]
    fn bar_paint_ops() {
        // Items in `Item` order (logout, tray, media, time, date, active window), the status pill (empty),
        // workspaces, the dock, then the dividers; only the time is expanded and draws its label.
        let flat = "GTE".repeat(3) + "GTTE" + &"GTE".repeat(2) + "RRRR" + "rrrrr";
        for style in [BarStyle::Continuous, BarStyle::Okinami] {
            let (m, mut canvas) = model(style);
            paint(&mut canvas, &m);
            assert_eq!(signature(&canvas), flat, "{style:?}");
            assert!(canvas.count(OpKind::Image) <= m.dock().len());
        }
        // Pop: a three-layer sticker pill under every item, badge and the dock, and no dividers.
        let pop = "RRRGTE".repeat(3) + "RRRGTTE" + &"RRRGTE".repeat(2) + "RRRTRRRT" + "RRR";
        let (m, mut canvas) = model(BarStyle::Pop);
        paint(&mut canvas, &m);
        assert_eq!(signature(&canvas), pop);
    }

    #[test]
    fn bar_interaction_paints() {
        let (m, mut canvas) = model(BarStyle::Continuous);
        paint(&mut canvas, &m);
        assert!(canvas.count(OpKind::Text) >= 4 && canvas.count(OpKind::Rect) >= 1, "painting draws glyphs and dividers");
        assert_eq!(canvas.count(OpKind::BeginGroup), canvas.count(OpKind::EndGroup), "groups are balanced");
    }
}
