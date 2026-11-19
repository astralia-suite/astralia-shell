//! Panel identities and the set of panels one monitor owns, at most one of them active.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use super::panel::{Hook, Panel, PanelContent};
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PanelId {
    Tray,
    Resource,
    Network,
    Bluetooth,
    Volume,
    Battery,
    Media,
    Brightness,
    Time,
    Date,
}

impl PanelId {
    pub const ALL: [PanelId; 10] = [
        PanelId::Tray,
        PanelId::Resource,
        PanelId::Network,
        PanelId::Bluetooth,
        PanelId::Volume,
        PanelId::Battery,
        PanelId::Media,
        PanelId::Brightness,
        PanelId::Time,
        PanelId::Date,
    ];
}

/// Which edge of its item a panel card lines up with.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PanelAnchor {
    Left,
    #[default]
    Center,
    Right,
}

#[derive(Default)]
pub struct PanelSet {
    panels: [Option<Panel>; 10],
    latest: Rc<Cell<Option<PanelId>>>,
    /// Called whenever any panel's picture changed.
    pub on_changed: Hook<dyn Fn()>,
    /// Called with `true` when a panel opens and `false` once it has fully closed.
    pub on_state: Hook<dyn Fn(PanelId, bool)>,
}

impl PanelSet {
    pub fn new() -> PanelSet {
        PanelSet::default()
    }

    pub fn add(&mut self, id: PanelId, content: Box<dyn PanelContent>) {
        let mut panel = Panel::new(content);
        let changed = self.on_changed.clone();
        panel.on_changed.set(Rc::new(move || changed.call()));
        let (latest, state, changed) = (self.latest.clone(), self.on_state.clone(), self.on_changed.clone());
        panel.on_closed = Some(Box::new(move || {
            if latest.get() == Some(id) {
                latest.set(None);
            }
            if let Some(f) = state.get() {
                f(id, false);
            }
            changed.call();
        }));
        self.panels[id as usize] = Some(panel);
    }

    pub fn find(&self, id: PanelId) -> Option<&Panel> {
        self.panels[id as usize].as_ref()
    }

    pub fn find_mut(&mut self, id: PanelId) -> Option<&mut Panel> {
        self.panels[id as usize].as_mut()
    }

    /// The panel opened last, while it is open and not closing.
    pub fn active_id(&self) -> Option<PanelId> {
        let id = self.latest.get()?;
        self.find(id).filter(|p| p.is_open() && !p.closing()).map(|_| id)
    }

    pub fn active(&mut self) -> Option<&mut Panel> {
        let id = self.active_id()?;
        self.find_mut(id)
    }

    fn open_panels(&mut self) -> impl Iterator<Item = &mut Panel> {
        self.panels.iter_mut().flatten().filter(|p| p.is_open())
    }

    pub fn any_open(&self) -> bool {
        self.panels.iter().flatten().any(Panel::is_open)
    }

    pub fn animating(&self) -> bool {
        self.panels.iter().flatten().any(|p| p.is_open() && p.animating())
    }

    /// Closes the open panel `id`, or opens it and closes the others.
    pub fn toggle(&mut self, id: PanelId, anchor: Option<PanelAnchor>) {
        let Some(panel) = self.find_mut(id) else { return };
        panel.set_anchor(anchor);
        if panel.is_open() && !panel.closing() {
            panel.close();
            return;
        }
        self.close_except(Some(id));
        self.latest.set(Some(id));
        if let Some(panel) = self.find_mut(id) {
            panel.open();
        }
        if let Some(f) = self.on_state.get() {
            f(id, true);
        }
    }

    pub fn close_except(&mut self, keep: Option<PanelId>) {
        for (i, panel) in self.panels.iter_mut().enumerate() {
            if let Some(panel) = panel.as_mut().filter(|p| p.is_open())
                && keep.is_none_or(|k| k as usize != i)
            {
                panel.close();
            }
        }
    }

    pub fn close_all(&mut self) {
        self.close_except(None);
    }

    pub fn tick(&mut self, now: Instant) {
        for panel in self.open_panels() {
            panel.tick(now);
        }
    }

    /// Paints every open panel; returns the active panel's card.
    pub fn paint(&mut self, canvas: &mut dyn Canvas, surface_width: f32, top: f32) -> Rect {
        let active = self.active_id();
        let mut card = Rect::default();
        for (i, panel) in self.panels.iter_mut().enumerate() {
            let Some(panel) = panel.as_mut().filter(|p| p.is_open()) else { continue };
            let r = panel.paint(canvas, surface_width, top);
            if active.is_some_and(|a| a as usize == i) {
                card = r;
            }
        }
        card
    }

    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.panels.iter().flatten().any(|p| p.clickable(x, y))
    }

    pub fn contains(&self, x: f64, y: f64) -> bool {
        self.panels.iter().flatten().any(|p| p.contains(x, y))
    }
}
