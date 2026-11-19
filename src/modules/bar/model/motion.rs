//! Workspace pill growth and dock icon slides.

use std::collections::HashSet;

use super::{DockMotion, DockSlot, Model, WorkspaceSlot};
use crate::base::animation::{self, Easing};
use crate::config::bar_layout;
use crate::service::compositor::Workspace;
use crate::service::dock::dock_entries_for_monitor;

impl Model {
    pub(super) fn refresh_workspaces(&mut self) {
        self.workspaces.clear();
        let compositor = self.sources.compositor.clone();
        let state = compositor.state();
        let mut monitor = state.by_monitor.get(&self.sources.output);
        let (list, active_id): (Vec<Workspace>, i32) = if self.sources.workspace_slots > 0 {
            if monitor.is_none_or(|m| m.active_id < 1) {
                monitor = state.by_monitor.get(&state.focused_monitor);
            }
            let active = monitor.map_or(-1, |m| m.active_id);
            let list = (1..=self.sources.workspace_slots)
                .map(|id| {
                    let occupied = state.by_monitor.values().any(|m| m.workspaces.iter().any(|ws| ws.id == id && ws.occupied));
                    Workspace { id, name: id.to_string(), occupied }
                })
                .collect();
            (list, active)
        } else if let Some(m) = monitor {
            (m.workspaces.clone(), m.active_id)
        } else {
            return;
        };
        drop(state);

        let mut live = HashSet::new();
        for ws in list {
            let active = ws.id == active_id;
            live.insert(ws.id);
            let grow = self.grow.entry(ws.id).or_insert_with(|| animation::value(if active { 1.0 } else { 0.0 })).clone();
            match self.was_active.get(&ws.id).copied() {
                None => {
                    self.was_active.insert(ws.id, active);
                }
                Some(was) if was != active => {
                    self.was_active.insert(ws.id, active);
                    let from = grow.get();
                    self.animations.animate(from, if active { 1.0 } else { 0.0 }, bar_layout::WORKSPACE_ANIM_MS, Easing::Linear, animation::writer(&grow), None, bar_layout::OWNER_WORKSPACE + ws.id as u64);
                }
                Some(_) => {}
            }
            self.workspaces.push(WorkspaceSlot { id: ws.id, active, occupied: ws.occupied, grow: grow.get(), rect: Default::default() });
        }
        self.grow.retain(|id, _| live.contains(id));
        self.was_active.retain(|id, _| live.contains(id));
    }

    pub(super) fn refresh_dock(&mut self) {
        let entries = dock_entries_for_monitor(&self.sources.compositor.state(), &self.sources.output);
        let mut live = HashSet::new();
        self.dock.clear();
        let slot_w = bar_layout::DOCK_ICON + bar_layout::DOCK_SPACING;
        for (i, entry) in entries.iter().enumerate() {
            live.insert(entry.address.clone());
            let target = i as f32 * slot_w;
            if !self.dock_motion.contains_key(&entry.address) {
                let owner = bar_layout::OWNER_DOCK + 2 * self.next_dock_owner;
                self.next_dock_owner += 1;
                let appear = animation::value(if self.dock_seen { 0.0 } else { 1.0 });
                if self.dock_seen {
                    self.animations.animate(0.0, 1.0, bar_layout::DOCK_REORDER_MS, Easing::EaseOutQuad, animation::writer(&appear), None, owner + 1);
                }
                self.dock_motion.insert(entry.address.clone(), DockMotion { x: animation::value(target), target, appear, owner });
            }
            let Some(motion) = self.dock_motion.get_mut(&entry.address) else { continue };
            if (motion.target - target).abs() > 0.5 {
                motion.target = target;
                let from = motion.x.get();
                self.animations.animate(from, target, bar_layout::DOCK_REORDER_MS, Easing::EaseOutQuad, animation::writer(&motion.x), None, motion.owner);
            }
            self.dock.push(DockSlot { entry: entry.clone(), x: motion.x.get(), appear: motion.appear.get() });
        }
        let gone: Vec<String> = self.dock_motion.keys().filter(|a| !live.contains(*a)).cloned().collect();
        for address in gone {
            if let Some(m) = self.dock_motion.remove(&address) {
                self.animations.cancel_owner(m.owner);
                self.animations.cancel_owner(m.owner + 1);
            }
        }
        let n = entries.len() as f32;
        let width_target = if entries.is_empty() { 0.0 } else { n * bar_layout::DOCK_ICON + (n - 1.0) * bar_layout::DOCK_SPACING };
        if entries.is_empty() || !self.dock_seen || self.dock_w.get() <= 0.0 {
            self.animations.cancel_owner(bar_layout::OWNER_DOCK_WIDTH);
            self.dock_w.set(width_target);
            self.dock_w_target = width_target;
        } else if (self.dock_w_target - width_target).abs() > 0.5 {
            self.dock_w_target = width_target;
            let from = self.dock_w.get();
            self.animations.animate(from, width_target, bar_layout::DOCK_REORDER_MS, Easing::EaseOutQuad, animation::writer(&self.dock_w), None, bar_layout::OWNER_DOCK_WIDTH);
        }
        self.dock_seen = self.dock_seen || !entries.is_empty();
    }
}
