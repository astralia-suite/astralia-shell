//! Placing items, the status pill, workspaces and the dock in the three zones.

use super::zones::{STATUS_MEMBERS, Slot, SlotKind};
use super::{Item, ItemView, Layout, Model};
use crate::config::bar_layout;
use crate::modules::bar::paint::chips::{DOT, icon_cell_extent};
use crate::modules::bar::paint::style::IslandSpan;
use crate::modules::bar::panel::core::panel_set::PanelAnchor;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;

const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: bar_layout::ICON_PX as i32, bold: false, max_width: 0, wrap: false };
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::TEXT_PX, bold: false, max_width: 0, wrap: false };
const NAME_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::WORKSPACE_INFO_NAME_PX, bold: false, max_width: 0, wrap: false };
const TITLE_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::WORKSPACE_INFO_TITLE_PX, bold: false, max_width: 0, wrap: false };

struct Run {
    start: f32,
    end: f32,
    any: bool,
}

fn cell_extent(view: &ItemView) -> f32 {
    icon_cell_extent(view.icon_w, if view.label.is_empty() { 0.0 } else { view.label_w }, view.expand)
}

impl Model {
    /// Collapsed to the icon plus `expand` of the label; a two-line title is capped.
    pub(super) fn item_width(view: &ItemView, pad: f32) -> f32 {
        let collapsed = view.icon_w + 2.0 * pad;
        let mut text = if view.detail_w > 0.0 { view.detail_w } else { view.label_w };
        if view.label.is_empty() || text <= 0.0 {
            return collapsed;
        }
        if view.detail_w > 0.0 {
            text = text.min(bar_layout::WORKSPACE_INFO_CAP - collapsed - bar_layout::LABEL_GAP);
        }
        if view.lead_w > 0.0 {
            text += view.lead_w + bar_layout::LABEL_GAP;
        }
        collapsed + (text + bar_layout::LABEL_GAP) * view.expand
    }

    fn status_members(&self) -> Vec<Item> {
        STATUS_MEMBERS.into_iter().filter(|&id| self.item(id).visible).collect()
    }

    fn workspace_row(&self) -> f32 {
        let s = &self.style;
        self.workspaces.iter().enumerate().map(|(i, ws)| (if i > 0 { s.workspace_gap } else { 0.0 }) + s.workspace_size * (1.0 + (s.workspace_scale - 1.0) * ws.grow)).sum()
    }

    fn slot_width(&self, slot: &Slot, pad: f32) -> f32 {
        let s = &self.style;
        match slot.kind {
            SlotKind::Workspaces if self.workspaces.is_empty() => 0.0,
            SlotKind::Workspaces => self.workspace_row() + if s.workspace_badges { 0.0 } else { 2.0 * pad },
            SlotKind::Status => {
                let list = self.status_members();
                if list.is_empty() {
                    return 0.0;
                }
                let w: f32 = list.iter().map(|&id| cell_extent(self.item(id))).sum();
                w + s.icon_gap * (list.len() - 1) as f32 + 2.0 * pad
            }
            SlotKind::Dock if self.dock.is_empty() => 0.0,
            SlotKind::Dock => self.dock_w.get() + 2.0 * pad,
            SlotKind::Item => match slot.item.map(|id| self.item(id)) {
                Some(view) if view.visible => Model::item_width(view, pad),
                _ => 0.0,
            },
        }
    }

    fn place(&mut self, slot: &Slot, x: f32, w: f32, height: f32, pad: f32, zone: PanelAnchor) {
        let s = self.style;
        match slot.kind {
            SlotKind::Workspaces => {
                self.layout.workspaces = Rect { x, y: 0.0, w, h: height };
                let badges = s.workspace_badges;
                let mut wx = if badges { x } else { x + pad };
                let wy = if badges { 0.0 } else { (height - s.workspace_size) / 2.0 };
                for ws in &mut self.workspaces {
                    let bw = s.workspace_size * (1.0 + (s.workspace_scale - 1.0) * ws.grow);
                    ws.rect = Rect { x: wx, y: wy, w: bw, h: if badges { height } else { s.workspace_size } };
                    wx += bw + s.workspace_gap;
                }
            }
            SlotKind::Status => {
                self.layout.status = Rect { x, y: 0.0, w, h: height };
                let list = self.status_members();
                let mut at = x + pad;
                for (i, &id) in list.iter().enumerate() {
                    let left = if i == 0 { x } else { at - s.icon_gap / 2.0 };
                    at += cell_extent(self.item(id)) + s.icon_gap;
                    let right = if i + 1 == list.len() { x + w } else { at - s.icon_gap / 2.0 };
                    let view = self.item_mut(id);
                    view.rect = Rect { x: left, y: 0.0, w: right - left, h: height };
                    view.grouped = true;
                    view.zone = zone;
                }
            }
            SlotKind::Dock => self.layout.dock = Rect { x, y: 0.0, w, h: height },
            SlotKind::Item => {
                if let Some(id) = slot.item {
                    let view = self.item_mut(id);
                    view.rect = Rect { x, y: 0.0, w, h: height };
                    view.zone = zone;
                }
            }
        }
    }

    fn run(&mut self, placed: &[(Slot, f32)], start: f32, height: f32, pad: f32, dividers: bool, zone: PanelAnchor) -> Run {
        let mut x = start;
        for (i, (slot, w)) in placed.iter().enumerate() {
            if i > 0 {
                x += bar_layout::CAPSULE_GAP;
                if dividers {
                    self.layout.dividers.push(x - bar_layout::CAPSULE_GAP / 2.0);
                }
            }
            self.place(slot, x, *w, height, pad, zone);
            x += w;
        }
        Run { start, end: x, any: !placed.is_empty() }
    }

    /// Measures what changed and places everything across `width`.
    pub fn layout(&mut self, canvas: &mut dyn Canvas, width: f32) {
        self.sync();
        let style = self.style;
        let height = bar_layout::HEIGHT as f32;
        let pad = height * style.pad_ratio;
        let rail = style.has_rail();
        let dividers = rail || style.continuous;
        let island_pad = if rail {
            bar_layout::ISLAND_PAD
        } else if style.continuous {
            height * style.pad_ratio
        } else {
            0.0
        };
        self.layout = Layout { width, height, ..Default::default() };
        for i in 0..self.items.len() {
            let view = &mut self.items[i];
            view.rect = Rect::default();
            view.grouped = false;
            view.zone = PanelAnchor::Right;
            view.detail_w = 0.0;
            view.lead_w = 0.0;
            if !view.visible {
                continue;
            }
            if self.measured_glyph[i] != view.glyph {
                self.measured_glyph[i] = view.glyph;
                view.icon_w = view.glyph.map_or(0.0, |g| canvas.measure(g, &ICON_STYLE).w);
            }
            if self.measured_label[i] != view.label {
                self.measured_label[i].clone_from(&view.label);
                view.label_w = if view.label.is_empty() { 0.0 } else { canvas.measure(&view.label, &TEXT_STYLE).w };
            }
            if !view.detail.is_empty() {
                view.detail_w = canvas.measure(&view.detail, &TITLE_STYLE).w.max(canvas.measure(&view.label, &NAME_STYLE).w);
            }
            if !view.lead.is_empty() {
                view.lead_w = canvas.measure(&view.lead, &TEXT_STYLE).w + bar_layout::LABEL_GAP + canvas.measure(DOT, &TEXT_STYLE).w;
            }
        }
        for slot in &mut self.dock {
            if let Some(m) = self.dock_motion.get(&slot.entry.address) {
                slot.x = m.x.get();
                slot.appear = m.appear.get();
            }
        }

        let measure = |model: &Model, zone: &[Slot]| -> Vec<(Slot, f32)> { zone.iter().map(|s| (*s, model.slot_width(s, pad))).filter(|(_, w)| *w > 0.0).collect() };
        let total = |placed: &[(Slot, f32)]| -> f32 {
            if placed.is_empty() { 0.0 } else { placed.iter().map(|p| p.1).sum::<f32>() + bar_layout::CAPSULE_GAP * (placed.len() - 1) as f32 }
        };
        let zones = style.zones;
        let left = measure(self, &zones.left);
        let center = measure(self, &zones.center);
        let right = measure(self, &zones.right);
        let (center_w, right_w) = (total(&center), total(&right));
        let l = self.run(&left, island_pad, height, pad, dividers, PanelAnchor::Left);
        let c = self.run(&center, (width - center_w) / 2.0, height, pad, dividers, PanelAnchor::Center);
        let r = self.run(&right, width - island_pad - right_w, height, pad, dividers, PanelAnchor::Right);
        if rail {
            if l.any {
                self.layout.left_end = Some((l.end + island_pad).round());
            }
            if c.any {
                self.layout.center = Some(IslandSpan { left: (c.start - island_pad).round(), right: (c.end + island_pad).round() });
            }
            if r.any {
                self.layout.right_start = Some((r.start - island_pad).round());
            }
        }
    }
}
