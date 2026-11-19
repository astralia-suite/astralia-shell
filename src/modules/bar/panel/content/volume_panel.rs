//! Output and input sliders, per-application streams, and the default device pickers.

use std::rc::Rc;

use crate::base::input::{KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::glyphs;
use crate::render::theme::palette;
use crate::render::tokens::Color;
use crate::service::audio::{AudioNode, AudioNodeKind, AudioService};

const SLIDER: i32 = 1;
const MUTE: i32 = 2;
const DEVICE: i32 = 3;
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VolumeRowKind {
    Label,
    Slider,
    Section,
    Divider,
    App,
    Device,
}

#[derive(Clone, Debug)]
pub struct VolumeRow {
    pub kind: VolumeRowKind,
    pub height: f32,
    pub text: String,
    pub detail: String,
    pub node: AudioNode,
    pub selected: bool,
}

fn row(kind: VolumeRowKind, height: f32, text: &str) -> VolumeRow {
    VolumeRow { kind, height, text: text.to_string(), detail: String::new(), node: AudioNode::default(), selected: false }
}

/// The rows for the default sink and source ids and every node PipeWire reports.
pub fn volume_rows(sink_id: u32, source_id: u32, nodes: &[AudioNode]) -> Vec<VolumeRow> {
    let find = |id: u32| nodes.iter().find(|n| n.id == id).cloned();
    let of_kind = |kind: AudioNodeKind| nodes.iter().filter(move |n| n.kind == kind);
    let mut rows = Vec::new();
    for (title, id) in [("Output", sink_id), ("Input", source_id)] {
        let node = find(id);
        rows.push(VolumeRow { detail: node.as_ref().map(|n| n.label.clone()).unwrap_or_default(), ..row(VolumeRowKind::Label, cfg::VOLUME_LABEL_ROW, title) });
        rows.push(VolumeRow { node: node.unwrap_or_default(), ..row(VolumeRowKind::Slider, cfg::VOLUME_SLIDER_ROW, "") });
    }
    rows.push(row(VolumeRowKind::Divider, 1.0, ""));
    rows.push(row(VolumeRowKind::Section, cfg::VOLUME_SECTION_ROW, "Applications"));
    rows.extend(of_kind(AudioNodeKind::Playback).map(|n| VolumeRow { node: n.clone(), ..row(VolumeRowKind::App, cfg::VOLUME_APP_ROW, &n.label) }));
    rows.push(row(VolumeRowKind::Divider, 1.0, ""));
    for (title, kind, selected) in [("Output Device", AudioNodeKind::Sink, sink_id), ("Input Device", AudioNodeKind::Source, source_id)] {
        rows.push(row(VolumeRowKind::Section, cfg::VOLUME_SECTION_ROW, title));
        rows.extend(of_kind(kind).map(|n| VolumeRow { node: n.clone(), selected: n.id == selected, ..row(VolumeRowKind::Device, cfg::VOLUME_DEVICE_ROW, &n.label) }));
    }
    rows.push(row(VolumeRowKind::Section, cfg::TRAILING_SPACER, ""));
    rows
}

fn percent_text(node: &AudioNode) -> String {
    if node.muted { "muted".to_string() } else { format!("{}%", node.percent) }
}

fn level(node: &AudioNode) -> f32 {
    if node.muted { 0.0 } else { node.percent as f32 / 100.0 }
}

fn clipped(width: f32) -> TextStyle {
    TextStyle { max_width: width.max(0.0) as i32, ..TEXT_STYLE }
}

pub struct VolumePanel {
    base: ContentBase,
    audio: Rc<AudioService>,
    selected: u32,
    dragging: u32,
    hovered: u32,
}

impl VolumePanel {
    pub fn new(audio: Rc<AudioService>) -> VolumePanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        audio.changed.connect(move |_| notify());
        VolumePanel { base, audio, selected: 0, dragging: 0, hovered: 0 }
    }

    fn rows(&self) -> Vec<VolumeRow> {
        let mut nodes = self.audio.nodes(AudioNodeKind::Sink);
        nodes.extend(self.audio.nodes(AudioNodeKind::Source));
        nodes.extend(self.audio.nodes(AudioNodeKind::Playback));
        volume_rows(self.audio.sink_id(), self.audio.source_id(), &nodes)
    }

    fn reset(&mut self) {
        (self.selected, self.dragging, self.hovered) = (0, 0, 0);
    }

    fn focused(&self, node: &AudioNode) -> bool {
        node.id == self.selected || node.id == self.hovered || node.id == self.dragging
    }

    fn set_percent(&self, id: u32, percent: i32) {
        if self.audio.node(id).is_some_and(|n| n.percent != percent) {
            self.audio.set_volume(id, percent);
        }
    }

    fn step(&self, id: u32, delta: i32) {
        if let Some(node) = self.audio.node(id) {
            self.set_percent(id, (node.percent + delta).clamp(0, 100));
        }
    }

    fn paint_row(&self, canvas: &mut dyn Canvas, r: Rect, row: &VolumeRow, paint: &mut PanelPaint) {
        let p = palette();
        let node = &row.node;
        let node_region = |rect: Rect, id: i32, drag: bool| PanelRegion { rect, id, a: node.id as i32, drag, ..PanelRegion::default() };
        match row.kind {
            VolumeRowKind::Label => {
                let title = canvas.measure(&row.text, &TEXT_STYLE);
                widgets::text_in_row(canvas, &row.text, &TEXT_STYLE, r.x, r, p.text);
                if !row.detail.is_empty() {
                    let style = clipped(cfg::VOLUME_LABEL_CAP);
                    widgets::text_in_row(canvas, &format!("\u{2014} {}", row.detail), &style, r.x + title.w + cfg::TIGHT_GAP, r, p.text_dim);
                }
            }
            VolumeRowKind::Slider => {
                let button = Rect { x: r.x + r.w - cfg::VOLUME_BUTTON, y: r.y + (r.h - cfg::VOLUME_BUTTON) / 2.0, w: cfg::VOLUME_BUTTON, h: cfg::VOLUME_BUTTON };
                widgets::icon_button(canvas, button, glyphs::volume_threshold(node.muted, node.percent), p.text);
                paint.push(node_region(button, MUTE, false));
                let percent_right = button.x - cfg::ROW_GAP;
                widgets::right_text(canvas, &percent_text(node), &TEXT_STYLE, percent_right, r, p.text_dim);
                let track = Rect { w: percent_right - cfg::VOLUME_PERCENT_WIDTH - cfg::VOLUME_SLIDER_GAP - r.x, ..r };
                widgets::slider(canvas, track, level(node), node.muted, self.focused(node), cfg::SLIDER_TRACK);
                paint.push(node_region(track, SLIDER, true));
            }
            VolumeRowKind::Section => {
                if !row.text.is_empty() {
                    let size = canvas.measure(&row.text, &TEXT_STYLE);
                    canvas.text(&row.text, &TEXT_STYLE, r.x, r.y + r.h - size.h, p.text);
                }
            }
            VolumeRowKind::Divider => canvas.rect(Rect { h: 1.0, ..r }, p.text_alpha06),
            VolumeRowKind::App => {
                let button = Rect { x: r.x + r.w - cfg::VOLUME_BUTTON, y: r.y, w: cfg::VOLUME_BUTTON, h: cfg::VOLUME_BUTTON };
                widgets::icon_button(canvas, button, glyphs::volume_threshold(node.muted, node.percent), p.text);
                paint.push(node_region(button, MUTE, false));
                let percent_right = button.x - cfg::ROW_GAP;
                let percent = percent_text(node);
                let size = canvas.measure(&percent, &TEXT_STYLE);
                canvas.text(&percent, &TEXT_STYLE, percent_right - size.w, r.y, p.text_dim);
                let name_right = percent_right - cfg::VOLUME_PERCENT_WIDTH - cfg::ROW_GAP;
                canvas.text(&row.text, &clipped(name_right - r.x), r.x, r.y, p.text);
                let track = Rect { x: r.x, y: r.y + cfg::VOLUME_TEXT_ROW + cfg::VOLUME_APP_GAP, w: name_right - r.x, h: cfg::VOLUME_APP_SLIDER };
                widgets::slider(canvas, track, level(node), node.muted, self.focused(node), cfg::VOLUME_APP_TRACK);
                paint.push(node_region(track, SLIDER, true));
            }
            VolumeRowKind::Device => {
                let dot = cfg::VOLUME_INDICATOR;
                let cy = r.y + r.h / 2.0;
                canvas.rounded(Rect { x: r.x, y: cy - dot / 2.0, w: dot, h: dot }, dot / 2.0, if row.selected { p.accent } else { p.text_alpha20 }, 0.0, Color::default());
                if row.selected {
                    let inner = cfg::VOLUME_INDICATOR_DOT;
                    canvas.rounded(Rect { x: r.x + (dot - inner) / 2.0, y: cy - inner / 2.0, w: inner, h: inner }, inner / 2.0, p.text, 0.0, Color::default());
                }
                let text_x = r.x + dot + cfg::ROW_ICON_GAP;
                widgets::text_in_row(canvas, &row.text, &clipped(r.x + r.w - text_x), text_x, r, if row.selected { p.accent } else { p.text });
                paint.push(node_region(r, DEVICE, false));
            }
        }
    }
}

impl PanelContent for VolumePanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Volume"
    }
    fn tint(&self) -> Color {
        palette().pop_a
    }
    fn opened(&mut self) {
        self.reset();
    }
    fn closed(&mut self) {
        self.reset();
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        let height: f32 = self.rows().iter().map(|r| r.height + cfg::LIST_SPACING).sum();
        (height - cfg::LIST_SPACING).max(0.0)
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint) {
        let mut y = view.y - scroll;
        for row in self.rows() {
            let r = Rect { x: view.x, y, w: view.w, h: row.height };
            y += row.height + cfg::LIST_SPACING;
            if r.y + r.h < view.y || r.y > view.y + view.h {
                continue;
            }
            self.paint_row(canvas, r, &row, paint);
        }
    }
    fn hover(&mut self, id: i32, a: i32) -> bool {
        let next = if id == SLIDER && a > 0 { a as u32 } else { 0 };
        if next == self.hovered {
            return false;
        }
        self.hovered = next;
        true
    }
    fn activate(&mut self, region: &PanelRegion, x: f64, _y: f64) -> bool {
        let id = region.a as u32;
        match region.id {
            SLIDER => {
                (self.selected, self.dragging) = (id, id);
                self.set_percent(id, (widgets::slider_value(region.rect, x) * 100.0).round() as i32);
            }
            MUTE => {
                if let Some(node) = self.audio.node(id) {
                    self.audio.set_mute(id, !node.muted);
                }
            }
            DEVICE => self.audio.set_default(id),
            _ => return false,
        }
        true
    }
    fn drag(&mut self, region: &PanelRegion, x: f64, _y: f64) -> bool {
        if region.id != SLIDER {
            return false;
        }
        self.set_percent(region.a as u32, (widgets::slider_value(region.rect, x) * 100.0).round() as i32);
        true
    }
    fn drop_region(&mut self, _region: &PanelRegion) {
        self.dragging = 0;
    }
    fn key(&mut self, event: &KeyEvent) -> bool {
        if self.selected == 0 || !matches!(event.kind, KeyKind::Left | KeyKind::Right) {
            return false;
        }
        self.step(self.selected, if event.kind == KeyKind::Right { 1 } else { -1 });
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service::audio::{AudioNode, AudioNodeKind};

    fn node(id: u32, kind: AudioNodeKind, label: &str) -> AudioNode {
        AudioNode { id, kind, label: label.into(), percent: 50, muted: false }
    }

    #[test]
    fn volume_rows_order() {
        let nodes = [
            node(1, AudioNodeKind::Sink, "Speakers"),
            node(2, AudioNodeKind::Sink, "HDMI"),
            node(3, AudioNodeKind::Source, "Mic"),
            node(4, AudioNodeKind::Playback, "Firefox"),
            node(5, AudioNodeKind::Playback, "mpv"),
            node(6, AudioNodeKind::Capture, "Recorder"),
        ];
        let rows = volume_rows(2, 3, &nodes);
        use VolumeRowKind::*;
        let shape: Vec<(VolumeRowKind, &str, u32, bool)> = rows.iter().map(|r| (r.kind, r.text.as_str(), r.node.id, r.selected)).collect();
        assert_eq!(
            shape,
            [
                (Label, "Output", 0, false),
                (Slider, "", 2, false),
                (Label, "Input", 0, false),
                (Slider, "", 3, false),
                (Divider, "", 0, false),
                (Section, "Applications", 0, false),
                (App, "Firefox", 4, false),
                (App, "mpv", 5, false),
                (Divider, "", 0, false),
                (Section, "Output Device", 0, false),
                (Device, "Speakers", 1, false),
                (Device, "HDMI", 2, true),
                (Section, "Input Device", 0, false),
                (Device, "Mic", 3, true),
                (Section, "", 0, false),
            ]
        );
        assert_eq!(rows[0].detail, "HDMI", "the output label names the default sink");
        assert_eq!(rows[2].detail, "Mic");
        assert_eq!(rows.last().map(|r| r.height), Some(cfg::TRAILING_SPACER));

        let empty = volume_rows(0, 0, &[]);
        assert!(empty[0].detail.is_empty() && empty[1].node.id == 0, "no default sink leaves an empty slider");
    }
}
