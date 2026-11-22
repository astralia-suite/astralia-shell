//! What the bar shows and where: item states, hover and peek, and the hit test.

pub mod item;
pub mod items;
mod layout;
mod motion;
pub mod zones;

use std::collections::HashMap;
use std::rc::Rc;
use std::time::{Duration, Instant};

use crate::base::animation::{self, AnimationManager, Easing, Value};
use crate::base::wall_clock;
use crate::config::bar_layout;
use crate::modules::bar::paint::style::{Frame, IslandSpan, StyleSpec, frame, style_spec};
use crate::modules::bar::panel::core::panel_set::PanelAnchor;
use crate::render::geometry::Rect;
use crate::render::glyphs;
use crate::render::icons;
use crate::render::theme::palette;
use crate::render::tokens::Color;
use crate::service::audio::AudioService;
use crate::service::battery::{BatteryService, BatteryStatus};
use crate::service::bluetooth::{BluetoothService, BluetoothStatus};
use crate::service::brightness::BrightnessService;
use crate::service::compositor::{Compositor, window_count};
use crate::service::dock::DockEntry;
use crate::service::network::{NetworkKind, NetworkService};
pub use crate::config::settings::BarStyle;
pub use item::{ITEM_COUNT, Item};

/// The services the bar reads; a missing one hides its items.
pub struct Sources {
    pub audio: Option<Rc<AudioService>>,
    pub battery: Option<Rc<BatteryService>>,
    pub bluetooth: Option<Rc<BluetoothService>>,
    pub network: Option<Rc<NetworkService>>,
    pub brightness: Option<Rc<BrightnessService>>,
    pub compositor: Rc<dyn Compositor>,
    pub tray_occupied: Option<Rc<dyn Fn() -> bool>>,
    pub output: String,
    pub resource: bool,
    pub workspace_slots: i32,
}

#[derive(Clone, Debug)]
pub struct ItemView {
    pub glyph: Option<&'static str>,
    pub label: String,
    pub detail: String,
    pub tint: Option<Color>,
    pub alert: bool,
    pub visible: bool,
    /// Collapses to its icon when not hovered.
    pub retracts: bool,
    pub expand: f32,
    pub hovered: bool,
    pub icon_w: f32,
    pub label_w: f32,
    pub detail_w: f32,
    /// Drawn inside the status pill.
    pub grouped: bool,
    pub zone: PanelAnchor,
    pub rect: Rect,
}

impl Default for ItemView {
    fn default() -> Self {
        ItemView {
            glyph: None,
            label: String::new(),
            detail: String::new(),
            tint: None,
            alert: false,
            visible: false,
            retracts: true,
            expand: 0.0,
            hovered: false,
            icon_w: 0.0,
            label_w: 0.0,
            detail_w: 0.0,
            grouped: false,
            zone: PanelAnchor::Right,
            rect: Rect::default(),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct WorkspaceSlot {
    pub id: i32,
    pub active: bool,
    pub occupied: bool,
    pub grow: f32,
    pub rect: Rect,
}

#[derive(Clone, Debug, PartialEq)]
pub struct DockSlot {
    pub entry: DockEntry,
    pub x: f32,
    pub appear: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ActionKind {
    #[default]
    None,
    Toggle,
    Overview,
    Workspace,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Action {
    pub kind: ActionKind,
    pub item: Option<Item>,
    pub workspace: i32,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Layout {
    pub dividers: Vec<f32>,
    pub left_end: Option<f32>,
    pub center: Option<IslandSpan>,
    pub right_start: Option<f32>,
    pub workspaces: Rect,
    pub status: Rect,
    pub dock: Rect,
    pub height: f32,
    pub width: f32,
}

pub fn wifi_glyph(strength: i32) -> &'static str {
    match strength {
        s if s > 75 => icons::WIFI,
        s if s > 50 => icons::WIFI2,
        s if s > 25 => icons::WIFI1,
        _ => icons::WIFI0,
    }
}

pub fn bluetooth_glyph(status: &BluetoothStatus) -> &'static str {
    if !status.powered {
        icons::BLUETOOTH_OFF
    } else if status.connected {
        icons::BLUETOOTH_CONNECTED
    } else {
        icons::BLUETOOTH_ON
    }
}

pub fn bluetooth_label(status: &BluetoothStatus) -> String {
    if !status.present {
        String::new()
    } else if !status.powered {
        "Off".into()
    } else if status.connected && !status.device.is_empty() {
        clip(&status.device)
    } else {
        "Idle".into()
    }
}

/// Free text (SSID, device name) capped so expanded labels stay a similar width.
pub fn clip(s: &str) -> String {
    const MAX: usize = 14;
    if s.chars().count() <= MAX { s.to_string() } else { s.chars().take(MAX - 1).chain(['\u{2026}']).collect() }
}

pub fn battery_glyph(status: &BatteryStatus) -> &'static str {
    if status.full {
        icons::PLUGGED_IN
    } else if status.charging {
        icons::BATTERY_CHARGING
    } else if status.percent <= 25 {
        icons::BATTERY1
    } else if status.percent <= 50 {
        icons::BATTERY2
    } else if status.percent <= 75 {
        icons::BATTERY3
    } else {
        icons::BATTERY4
    }
}

pub fn battery_label(status: &BatteryStatus) -> String {
    if status.full { "Plugged in".into() } else { format!("{}%", status.percent) }
}

struct DockMotion {
    x: Value,
    target: f32,
    appear: Value,
    owner: u64,
}

pub struct Model {
    sources: Sources,
    style: StyleSpec,
    items: [ItemView; ITEM_COUNT],
    expand: [Value; ITEM_COUNT],
    measured_glyph: [Option<&'static str>; ITEM_COUNT],
    measured_label: [String; ITEM_COUNT],
    workspaces: Vec<WorkspaceSlot>,
    grow: HashMap<i32, Value>,
    was_active: HashMap<i32, bool>,
    dock: Vec<DockSlot>,
    dock_motion: HashMap<String, DockMotion>,
    next_dock_owner: u64,
    dock_w: Value,
    dock_w_target: f32,
    dock_seen: bool,
    layout: Layout,
    animations: AnimationManager,
    open: Option<Item>,
    hovered: Option<Item>,
    linger: Option<Item>,
    linger_until: Instant,
    started: Instant,
    peek_ready: bool,
    peek_active: bool,
    peek_deadline: Instant,
    peek_level: i32,
    peek_muted: bool,
    /// Keeps every item's label out instead of only the hovered one's.
    expand_all: bool,
}

#[cfg_attr(not(test), expect(dead_code))]
/// Whole milliseconds from `now` to `at`, rounded up, at least 1.
fn ceil_ms(at: Instant, now: Instant) -> Duration {
    let left = at.saturating_duration_since(now);
    Duration::from_millis((left.as_nanos().div_ceil(1_000_000) as u64).max(1))
}

impl Model {
    pub fn new(sources: Sources, now: Instant) -> Model {
        Model {
            sources,
            style: style_spec(BarStyle::Continuous, false),
            items: std::array::from_fn(|_| ItemView::default()),
            expand: std::array::from_fn(|_| animation::value(0.0)),
            measured_glyph: [None; ITEM_COUNT],
            measured_label: std::array::from_fn(|_| String::new()),
            workspaces: Vec::new(),
            grow: HashMap::new(),
            was_active: HashMap::new(),
            dock: Vec::new(),
            dock_motion: HashMap::new(),
            next_dock_owner: 0,
            dock_w: animation::value(0.0),
            dock_w_target: 0.0,
            dock_seen: false,
            layout: Layout::default(),
            animations: AnimationManager::default(),
            open: None,
            hovered: None,
            linger: None,
            linger_until: now,
            started: now,
            peek_ready: false,
            peek_active: false,
            peek_deadline: now,
            peek_level: -1,
            peek_muted: false,
            expand_all: false,
        }
    }

    pub fn set_style(&mut self, style: &StyleSpec) {
        self.style = *style;
    }

    pub fn set_expand_all(&mut self, on: bool) {
        self.expand_all = on;
    }

    pub fn style(&self) -> &StyleSpec {
        &self.style
    }

    fn item_mut(&mut self, id: Item) -> &mut ItemView {
        &mut self.items[id as usize]
    }

    fn set(&mut self, id: Item, glyph: Option<&'static str>, label: String, visible: bool, retracts: bool) {
        let view = self.item_mut(id);
        view.retracts = retracts;
        view.glyph = if visible { glyph } else { None };
        view.label = if visible { label } else { String::new() };
        view.detail.clear();
        view.visible = visible && glyph.is_some();
        view.tint = None;
        view.alert = false;
    }

    /// Re-reads every source.
    pub fn refresh(&mut self, now: Instant) {
        let p = palette();
        self.set(Item::Logout, Some(icons::POWER), "Logout".into(), true, true);
        let tray = self.sources.tray_occupied.as_ref().is_some_and(|f| f());
        self.set(Item::Tray, Some(icons::TRAY), "Tray".into(), tray, true);
        self.set(Item::Resource, Some(icons::CPU), "Resource".into(), self.sources.resource, true);
        self.set(Item::Media, Some(icons::MUSIC_NOTE), "Media".into(), true, true);
        let local = wall_clock::now_tm();
        self.set(Item::Time, Some(icons::CLOCK), wall_clock::format_tm(&local, "%H:%M:%S"), true, false);
        self.set(Item::Date, Some(icons::CALENDAR), wall_clock::format_tm(&local, "%a %d %b"), true, true);
        self.refresh_active_window();
        let (_, windows) = window_count(&self.sources.compositor.state(), &self.sources.output);
        self.set(Item::WorkspaceInfo, Some(icons::APP_WINDOW), windows.to_string(), true, true);

        match self.sources.network.clone() {
            Some(net) => {
                let status = net.status().clone();
                let portal = net.connectivity() == "portal" || status.portal;
                let (glyph, label) = if portal {
                    (icons::LOCK, "Sign in".to_string())
                } else if status.kind == NetworkKind::Ethernet || net.ethernet_connected() {
                    (icons::ROUTER, "Ethernet".to_string())
                } else if status.kind == NetworkKind::Wifi {
                    (wifi_glyph(status.strength), clip(&status.ssid))
                } else if net.ethernet_available() && !net.wifi_available() {
                    (icons::ROUTER, String::new())
                } else {
                    (icons::WIFI_OFF, String::new())
                };
                self.set(Item::Network, Some(glyph), if label.is_empty() { "Offline".into() } else { label }, true, true);
            }
            None => self.set(Item::Network, None, String::new(), false, true),
        }

        let bluetooth = self.sources.bluetooth.as_ref().map(|b| b.status().clone()).filter(|s| s.present);
        match bluetooth {
            Some(status) => self.set(Item::Bluetooth, Some(bluetooth_glyph(&status)), bluetooth_label(&status), true, true),
            None => self.set(Item::Bluetooth, None, String::new(), false, true),
        }

        let sink = self.sources.audio.as_ref().map(|a| a.sink()).filter(|l| l.present);
        match sink {
            Some(level) => {
                let label = if level.muted { "Muted".into() } else { format!("{}%", level.percent) };
                self.set(Item::Volume, Some(glyphs::volume_threshold(level.muted, level.percent)), label, true, true);
                if !self.peek_ready && now.saturating_duration_since(self.started) >= bar_layout::PEEK_READY_DELAY {
                    self.peek_ready = true;
                }
                if self.peek_ready {
                    let changed = self.peek_level < 0 || self.peek_level != level.percent || self.peek_muted != level.muted;
                    if changed {
                        self.peek_active = true;
                        self.peek_deadline = now.checked_add(bar_layout::PEEK_DURATION).unwrap_or(now);
                    }
                }
                self.peek_level = level.percent;
                self.peek_muted = level.muted;
            }
            None => self.set(Item::Volume, None, String::new(), false, true),
        }

        let output = self.sources.output.clone();
        let brightness = self.sources.brightness.as_ref().filter(|b| b.available_on(&output)).map(|b| b.percent_on(&output));
        match brightness {
            Some(percent) => self.set(Item::Brightness, Some(glyphs::brightness_threshold(percent)), format!("{percent}%"), true, true),
            None => self.set(Item::Brightness, None, String::new(), false, true),
        }

        let battery = self.sources.battery.as_ref().map(|b| b.status()).filter(|s| s.present);
        match battery {
            Some(status) => {
                self.set(Item::Battery, Some(battery_glyph(&status)), battery_label(&status), true, true);
                let view = self.item_mut(Item::Battery);
                view.tint = if status.full {
                    None
                } else if status.charging {
                    Some(p.accent_alt)
                } else if status.percent <= 25 {
                    Some(p.critical_container)
                } else {
                    Some(p.accent)
                };
                view.alert = !status.charging && !status.full && status.percent <= bar_layout::BATTERY_CRITICAL_PERCENT;
            }
            None => self.set(Item::Battery, None, String::new(), false, true),
        }

        self.refresh_workspaces();
        self.refresh_dock();
        self.apply_hover(now);
    }

    fn refresh_active_window(&mut self) {
        let compositor = self.sources.compositor.clone();
        let state = compositor.state();
        let active = state.by_monitor.get(&self.sources.output).map_or(-1, |m| m.active_id);
        let view = &mut self.items[Item::ActiveWindow as usize];
        view.glyph = Some(icons::APP_WINDOW);
        view.visible = true;
        view.detail.clear();
        view.label = format!("Workspace {}", if active > 0 { active } else { 1 });
        if let Some(client) = state.clients.iter().find(|c| c.focus_history_id == 0 && c.workspace_id == active) {
            view.label = if client.initial_title.is_empty() { client.window_class.clone() } else { client.initial_title.clone() };
            view.detail = client.title.clone();
        }
    }

    fn update_expand(&mut self, id: Item, target: bool, instant: bool) -> bool {
        let i = id as usize;
        if self.items[i].hovered == target {
            return false;
        }
        self.items[i].hovered = target;
        let ms = if instant { 0.0 } else { bar_layout::EXPAND_MS };
        let from = self.expand[i].get();
        self.animations.animate(from, if target { 1.0 } else { 0.0 }, ms, Easing::EaseOutCubic, animation::writer(&self.expand[i]), None, bar_layout::OWNER_EXPAND + i as u64);
        true
    }

    /// Copies the tweened values into the views the painter reads.
    fn sync(&mut self) {
        for (view, v) in self.items.iter_mut().zip(&self.expand) {
            view.expand = v.get();
        }
        for ws in &mut self.workspaces {
            if let Some(g) = self.grow.get(&ws.id) {
                ws.grow = g.get();
            }
        }
    }

    fn apply_hover(&mut self, now: Instant) -> bool {
        let mut target = if self.open.is_some() {
            self.open
        } else if self.linger.is_some() && now < self.linger_until {
            self.linger
        } else {
            self.hovered
        };
        if target.is_none() && self.peek_active {
            target = Some(Item::Volume);
        }
        let mut changed = false;
        for id in Item::ALL {
            let view = &self.items[id as usize];
            // Name-only labels ("Logout", "Tray"…) stay a hover hint; expand-all shows only labels that carry a value.
            let name_only = matches!(id, Item::Logout | Item::Tray | Item::Resource | Item::Media);
            let want = view.visible && (target == Some(id) || !view.retracts || (self.expand_all && !name_only)) && !view.label.is_empty();
            changed |= self.update_expand(id, want, want && self.open == Some(id));
        }
        self.sync();
        changed
    }

    pub fn set_open(&mut self, item: Option<Item>, now: Instant) {
        if item == self.open {
            return;
        }
        if item.is_none() && self.open.is_some() {
            self.linger = self.open;
            self.linger_until = now.checked_add(bar_layout::CLOSE_LINGER).unwrap_or(now);
        }
        self.open = item;
        self.apply_hover(now);
    }

    /// The pointer position, `None` when it left the bar; true when an item started expanding or collapsing.
    pub fn hover(&mut self, x: Option<f64>, y: Option<f64>, now: Instant) -> bool {
        self.hovered = match (x, y) {
            (Some(x), Some(y)) => Item::ALL.into_iter().find(|&id| {
                let view = &self.items[id as usize];
                view.visible && view.rect.contains(x, y)
            }),
            _ => None,
        };
        self.apply_hover(now)
    }

    pub fn tick(&mut self, now: Instant) -> bool {
        if self.peek_active && now >= self.peek_deadline {
            self.peek_active = false;
        }
        self.animations.tick(now);
        self.apply_hover(now)
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    /// How long nothing will change by itself; an hour when idle.
    #[cfg_attr(not(test), expect(dead_code))]
    pub fn until_idle(&self, now: Instant) -> Duration {
        let mut wait = Duration::from_secs(3600);
        let mut consider = |at: Instant| wait = wait.min(ceil_ms(at, now));
        if self.peek_active {
            consider(self.peek_deadline);
        }
        if self.linger.is_some() && now < self.linger_until {
            consider(self.linger_until);
        }
        if self.animations.has_active() {
            consider(now.checked_add(Duration::from_millis(16)).unwrap_or(now));
        }
        wait
    }

    pub fn frame(&self) -> Frame {
        frame(&self.style, self.layout.left_end, self.layout.center, self.layout.right_start, self.layout.width)
    }

    pub fn press(&self, x: f64, y: f64) -> Action {
        for id in Item::ALL {
            let view = &self.items[id as usize];
            if !view.visible || !view.rect.contains(x, y) {
                continue;
            }
            if matches!(id, Item::ActiveWindow | Item::WorkspaceInfo) {
                return Action { kind: ActionKind::Overview, ..Default::default() };
            }
            return Action { kind: ActionKind::Toggle, item: Some(id), workspace: 0 };
        }
        let gap = self.style.workspace_gap;
        for ws in &self.workspaces {
            let hit = Rect { x: ws.rect.x - gap / 2.0, y: 0.0, w: ws.rect.w + gap, h: self.layout.height };
            if hit.contains(x, y) {
                return Action { kind: ActionKind::Workspace, item: None, workspace: ws.id };
            }
        }
        Action::default()
    }

    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.press(x, y).kind != ActionKind::None
    }

    pub fn zone(&self, id: Item) -> PanelAnchor {
        self.item(id).zone
    }

    pub fn item(&self, id: Item) -> &ItemView {
        &self.items[id as usize]
    }

    pub fn workspaces(&self) -> &[WorkspaceSlot] {
        &self.workspaces
    }

    pub fn dock(&self) -> &[DockSlot] {
        &self.dock
    }

    pub fn layout_ref(&self) -> &Layout {
        &self.layout
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn open(&self) -> Option<Item> {
        self.open
    }
}

#[cfg(test)]
mod tests;
