use std::cell::{Ref, RefCell};
use std::collections::HashMap;
use std::io;
use std::os::unix::net::UnixStream;
use std::rc::Rc;
use std::time::Duration;

use crate::base::signal::Signal;
use crate::render::tokens::Color;

#[derive(Clone, Debug, PartialEq)]
pub struct Workspace {
    pub id: i32,
    pub name: String,
    pub occupied: bool,
}

impl Default for Workspace {
    fn default() -> Self {
        Workspace { id: -1, name: String::new(), occupied: false }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct MonitorWorkspaces {
    pub workspaces: Vec<Workspace>,
    pub active_id: i32,
}

impl Default for MonitorWorkspaces {
    fn default() -> Self {
        MonitorWorkspaces { workspaces: Vec::new(), active_id: -1 }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CompositorMonitor {
    pub id: i32,
    pub name: String,
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
    pub scale: f64,
    pub transform: i32,
    pub reserved: [f64; 4],
}

impl Default for CompositorMonitor {
    fn default() -> Self {
        CompositorMonitor { id: -1, name: String::new(), x: 0.0, y: 0.0, width: 0.0, height: 0.0, scale: 1.0, transform: 0, reserved: [0.0; 4] }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CompositorClient {
    pub address: String,
    pub window_class: String,
    pub title: String,
    pub initial_title: String,
    pub workspace_id: i32,
    pub monitor_id: i32,
    pub at: [f64; 2],
    pub size: [f64; 2],
    pub floating: bool,
    pub fullscreen: i32,
    pub pinned: bool,
    pub focus_history_id: i64,
    pub xwayland: bool,
}

impl Default for CompositorClient {
    fn default() -> Self {
        CompositorClient {
            address: String::new(),
            window_class: String::new(),
            title: String::new(),
            initial_title: String::new(),
            workspace_id: -1,
            monitor_id: -1,
            at: [0.0; 2],
            size: [0.0; 2],
            floating: false,
            fullscreen: 0,
            pinned: false,
            focus_history_id: 0,
            xwayland: false,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CompositorState {
    pub by_monitor: HashMap<String, MonitorWorkspaces>,
    pub focused_monitor: String,
    pub monitors: Vec<CompositorMonitor>,
    pub clients: Vec<CompositorClient>,
    pub hug_radius_px: i32,
    pub gaps_out_px: Option<i32>,
    pub workspaces_per_monitor: i32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CursorPosition {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct WorkArea {
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ToplevelIdentity {
    pub app_id: String,
    pub title: String,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum CloseScope {
    #[default]
    Workspace,
    Monitor,
    All,
}

pub fn compositor_work_area(monitor: &CompositorMonitor) -> WorkArea {
    let rotated = monitor.transform % 2 == 1;
    let [width, height] = compositor_logical_size(monitor);
    let r = monitor.reserved;
    WorkArea {
        x: monitor.x + r[0],
        y: monitor.y + r[1],
        width: width - if rotated { r[1] } else { r[0] } - if rotated { r[3] } else { r[2] },
        height: height - if rotated { r[0] } else { r[1] } - if rotated { r[2] } else { r[3] },
    }
}

pub fn compositor_logical_size(monitor: &CompositorMonitor) -> [f64; 2] {
    let rotated = monitor.transform % 2 == 1;
    let scale = if monitor.scale > 0.0 { monitor.scale } else { 1.0 };
    if rotated { [monitor.height / scale, monitor.width / scale] } else { [monitor.width / scale, monitor.height / scale] }
}

pub fn compositor_monitor_by_name<'a>(state: &'a CompositorState, name: &str) -> Option<&'a CompositorMonitor> {
    state.monitors.iter().find(|m| m.name == name)
}

pub fn compositor_monitor_by_id(state: &CompositorState, id: i32) -> Option<&CompositorMonitor> {
    state.monitors.iter().find(|m| m.id == id)
}

/// `strtoull(address, 16)`: leading hex digits after an optional `0x`, else 0.
fn creation_order(address: &str) -> u64 {
    let text = address.trim_start();
    let text = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")).unwrap_or(text);
    let digits = text.bytes().take_while(u8::is_ascii_hexdigit).count();
    if digits == 0 {
        return 0;
    }
    u64::from_str_radix(&text[..digits], 16).unwrap_or(u64::MAX)
}

/// Client address to toplevel index: an exact class and title match first, then class alone,
/// oldest client first.
pub fn pair_toplevels(clients: &[CompositorClient], toplevels: &[ToplevelIdentity]) -> HashMap<String, usize> {
    let mut order: Vec<usize> = (0..clients.len()).collect();
    order.sort_by_key(|&i| creation_order(&clients[i].address));
    let mut paired = HashMap::new();
    let mut used = vec![false; toplevels.len()];
    let mut take = |paired: &mut HashMap<String, usize>, client: &CompositorClient, same_title: bool| {
        let found = toplevels
            .iter()
            .enumerate()
            .position(|(t, top)| !used[t] && top.app_id == client.window_class && (!same_title || top.title == client.title));
        if let Some(t) = found {
            used[t] = true;
            paired.entry(client.address.clone()).or_insert(t);
        }
    };
    for &i in &order {
        take(&mut paired, &clients[i], true);
    }
    for &i in &order {
        if !paired.contains_key(&clients[i].address) {
            take(&mut paired, &clients[i], false);
        }
    }
    paired
}

/// Clients on workspace or monitor `id`, or every client for `CloseScope::All`.
pub fn clients_in(state: &CompositorState, scope: CloseScope, id: i32) -> Vec<&CompositorClient> {
    state
        .clients
        .iter()
        .filter(|c| match scope {
            CloseScope::All => true,
            CloseScope::Workspace => c.workspace_id == id,
            CloseScope::Monitor => c.monitor_id == id,
        })
        .collect()
}

/// Blocking unix stream; `timeout` bounds each send and receive.
pub fn connect_unix(path: &str, timeout: Option<Duration>) -> io::Result<UnixStream> {
    if path.is_empty() {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    let stream = UnixStream::connect(path)?;
    if let Some(timeout) = timeout.filter(|t| !t.is_zero()) {
        stream.set_read_timeout(Some(timeout))?;
        stream.set_write_timeout(Some(timeout))?;
    }
    Ok(stream)
}

pub trait Compositor {
    fn name(&self) -> &'static str;
    fn state(&self) -> Ref<'_, CompositorState>;

    fn refresh(&self);
    fn refresh_clients(&self) -> bool;
    fn watch_client_order(&self, watch: bool);

    fn focus_workspace(&self, id: i32, global: bool);
    fn move_window(&self, address: &str, id: i32, global: bool);
    fn close_window(&self, address: &str);
    fn close_windows(&self, scope: CloseScope, id: i32);
    fn move_workspace_in(&self, id: i32, global: bool);
    fn swap_workspace(&self, id: i32, global: bool);
    fn apply_border_colors(&self, _active: Color, _inactive: Color) {}
    fn workspaces_per_monitor(&self) -> i32 {
        5
    }
    fn capture_by_toplevel_handle(&self) -> bool {
        false
    }
    fn cursor_position(&self) -> Option<CursorPosition> {
        None
    }

    fn active_changed(&self) -> &Signal;
    fn structure_changed(&self) -> &Signal;
}

#[derive(Default)]
pub struct NullCompositor {
    state: RefCell<CompositorState>,
    active_changed: Signal,
    structure_changed: Signal,
}

#[cfg(test)]
impl NullCompositor {
    /// Stands in for a live compositor in tests.
    pub fn set_state(&self, state: CompositorState) {
        *self.state.borrow_mut() = state;
    }
}

impl Compositor for NullCompositor {
    fn name(&self) -> &'static str {
        "none"
    }
    fn state(&self) -> Ref<'_, CompositorState> {
        self.state.borrow()
    }
    fn refresh(&self) {}
    fn refresh_clients(&self) -> bool {
        false
    }
    fn watch_client_order(&self, _watch: bool) {}
    fn focus_workspace(&self, _id: i32, _global: bool) {}
    fn move_window(&self, _address: &str, _id: i32, _global: bool) {}
    fn close_window(&self, _address: &str) {}
    fn close_windows(&self, _scope: CloseScope, _id: i32) {}
    fn move_workspace_in(&self, _id: i32, _global: bool) {}
    fn swap_workspace(&self, _id: i32, _global: bool) {}
    fn active_changed(&self) -> &Signal {
        &self.active_changed
    }
    fn structure_changed(&self) -> &Signal {
        &self.structure_changed
    }
}

pub fn make_compositor() -> Rc<dyn Compositor> {
    if let Some(hyprland) = crate::service::hyprland::HyprlandCompositor::new() {
        return hyprland;
    }
    use crate::service::shojiwm::ShojiCompositor;
    if ShojiCompositor::available() {
        return ShojiCompositor::new(ShojiCompositor::default_socket_path());
    }
    Rc::new(NullCompositor::default())
}

/// Field readers with the defaults of a missing or wrongly typed value.
pub(crate) mod json {
    use serde_json::Value;

    pub fn string(v: &Value, key: &str) -> String {
        v.get(key).and_then(Value::as_str).unwrap_or_default().to_string()
    }

    pub fn number(v: &Value, key: &str, fallback: f64) -> f64 {
        v.get(key).and_then(Value::as_f64).unwrap_or(fallback)
    }

    pub fn boolean(v: &Value, key: &str, fallback: bool) -> bool {
        v.get(key).and_then(Value::as_bool).unwrap_or(fallback)
    }

    pub fn parse(text: &str) -> Option<Value> {
        serde_json::from_str(text).ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn client(address: &str, class: &str, title: &str, workspace: i32, monitor: i32) -> CompositorClient {
        CompositorClient {
            address: address.into(),
            window_class: class.into(),
            title: title.into(),
            workspace_id: workspace,
            monitor_id: monitor,
            ..Default::default()
        }
    }

    #[test]
    fn compositor_helpers() {
        let monitor = CompositorMonitor {
            width: 2000.0,
            height: 1000.0,
            scale: 2.0,
            transform: 1,
            x: 10.0,
            reserved: [1.0, 2.0, 3.0, 4.0],
            ..Default::default()
        };
        assert_eq!(compositor_logical_size(&monitor), [500.0, 1000.0], "rotated logical size swaps the axes");
        let area = compositor_work_area(&monitor);
        assert_eq!((area.x, area.y, area.width, area.height), (11.0, 2.0, 500.0 - 2.0 - 4.0, 1000.0 - 1.0 - 3.0));
        let flat = CompositorMonitor { width: 100.0, height: 50.0, scale: 0.0, ..Default::default() };
        assert_eq!(compositor_logical_size(&flat), [100.0, 50.0], "a zero scale reads as one");

        let mut state = CompositorState { monitors: vec![CompositorMonitor { id: 3, name: "DP-1".into(), ..Default::default() }], ..Default::default() };
        assert_eq!(compositor_monitor_by_name(&state, "DP-1").map(|m| m.id), Some(3));
        assert!(compositor_monitor_by_id(&state, 3).is_some() && compositor_monitor_by_id(&state, 4).is_none());

        state.clients = vec![client("0x2", "a", "t", 1, 0), client("0x1", "b", "t", 2, 1), client("0x3", "a", "u", 2, 0)];
        let addresses = |v: Vec<&CompositorClient>| v.iter().map(|c| c.address.clone()).collect::<Vec<_>>();
        assert_eq!(addresses(clients_in(&state, CloseScope::Workspace, 2)), ["0x1", "0x3"]);
        assert_eq!(addresses(clients_in(&state, CloseScope::Monitor, 0)), ["0x2", "0x3"]);
        assert_eq!(clients_in(&state, CloseScope::All, -1).len(), 3);
    }

    #[test]
    fn toplevels_pair_by_title_then_class() {
        let clients = vec![client("0x20", "kitty", "b", 1, 0), client("0x10", "kitty", "a", 1, 0), client("0x30", "mpv", "x", 1, 0)];
        let tops = |v: &[(&str, &str)]| v.iter().map(|(a, t)| ToplevelIdentity { app_id: a.to_string(), title: t.to_string() }).collect::<Vec<_>>();
        let paired = pair_toplevels(&clients, &tops(&[("kitty", "a"), ("kitty", "zzz"), ("mpv", "y")]));
        assert_eq!(paired["0x10"], 0, "the exact title wins");
        assert_eq!(paired["0x20"], 1, "the rest pair by class in creation order");
        assert_eq!(paired["0x30"], 2);
        let paired = pair_toplevels(&clients, &tops(&[("kitty", "q")]));
        assert_eq!(paired.len(), 1);
        assert_eq!(paired["0x10"], 0, "the older client takes a class-only match first");
    }

    #[test]
    fn null_compositor_is_inert() {
        let c = NullCompositor::default();
        assert_eq!(c.name(), "none");
        assert!(!c.refresh_clients() && c.state().clients.is_empty());
        assert_eq!(c.workspaces_per_monitor(), 5);
        assert!(c.cursor_position().is_none() && !c.capture_by_toplevel_handle());
    }
}
