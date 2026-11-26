use std::cell::{Cell, Ref, RefCell};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::rc::{Rc, Weak};
use std::time::Duration;

use async_io::{Async, Timer};
use futures_lite::AsyncReadExt;
use serde_json::{Value, json};

use crate::base::signal::Signal;
use crate::render::tokens::Color;
use crate::service::compositor::{
    CloseScope, Compositor, CompositorClient, CompositorMonitor, CompositorState, Workspace, compositor_monitor_by_id, compositor_monitor_by_name,
    connect_unix, json as field,
};
use crate::service::runtime;

pub const SHOJI_DEFAULT_WORKSPACES_PER_MONITOR: i32 = 5;
const REQUEST_TIMEOUT: Duration = Duration::from_millis(250);
const RECONNECT_INTERVAL: Duration = Duration::from_millis(2000);
const REPLY_LIMIT: usize = 4 * 1024 * 1024;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ShojiWorkspaceRef {
    pub monitor: String,
    pub index: i32,
}

pub fn shoji_socket_path(runtime_dir: &str, display_name: &str) -> String {
    let dir = if runtime_dir.is_empty() { "/tmp" } else { runtime_dir };
    let display = if display_name.is_empty() { "wayland-0" } else { display_name };
    format!("{dir}/shojiwm-{display}.sock")
}

pub fn shoji_per_monitor(state: &CompositorState) -> i32 {
    if state.workspaces_per_monitor > 0 { state.workspaces_per_monitor } else { SHOJI_DEFAULT_WORKSPACES_PER_MONITOR }
}

pub fn shoji_workspace_id(per_monitor: i32, monitor_ordinal: i32, index: i32) -> i32 {
    monitor_ordinal * per_monitor + index
}

/// The object member, or an empty object when missing or of another type.
fn object_member<'a>(parent: &'a Value, key: &str) -> &'a Value {
    static EMPTY: Value = Value::Null;
    parent.get(key).filter(|v| v.is_object()).unwrap_or(&EMPTY)
}

fn reserved_insets(monitor: &CompositorMonitor, entry: &Value) -> [f64; 4] {
    let Some(usable) = entry.get("usable").filter(|u| u.is_object()) else { return [0.0; 4] };
    if monitor.width <= 0.0 || monitor.height <= 0.0 {
        return [0.0; 4];
    }
    let x = field::number(usable, "x", monitor.x);
    let y = field::number(usable, "y", monitor.y);
    let width = field::number(usable, "width", monitor.width);
    let height = field::number(usable, "height", monitor.height);
    [
        (x - monitor.x).max(0.0),
        (y - monitor.y).max(0.0),
        (monitor.x + monitor.width - x - width).max(0.0),
        (monitor.y + monitor.height - y - height).max(0.0),
    ]
}

struct Ranked {
    client: usize,
    active: bool,
    last_focused: f64,
}

pub fn shoji_parse_view(view: &Value) -> CompositorState {
    let mut state = CompositorState { focused_monitor: field::string(view, "currentMonitor"), ..Default::default() };
    if let Some(gaps) = view.get("gapsOut").and_then(Value::as_f64) {
        state.gaps_out_px = Some(gaps as i32);
        state.hug_radius_px = gaps as i32 + field::number(view, "rounding", 0.0) as i32;
    }
    let per = field::number(view, "workspacesPerMonitor", 0.0) as i32;
    if per >= 1 {
        state.workspaces_per_monitor = per;
    }
    let per_monitor = shoji_per_monitor(&state);
    let Some(monitors) = view.get("monitors").and_then(Value::as_array) else { return state };
    let mut ranking = Vec::new();
    for (ordinal, entry) in (0..).zip(monitors) {
        let rect = object_member(entry, "rect");
        let mut monitor = CompositorMonitor {
            id: ordinal,
            name: field::string(entry, "name"),
            x: field::number(rect, "x", 0.0),
            y: field::number(rect, "y", 0.0),
            width: field::number(rect, "width", 0.0),
            height: field::number(rect, "height", 0.0),
            ..Default::default()
        };
        monitor.reserved = reserved_insets(&monitor, entry);
        let workspaces = state.by_monitor.entry(monitor.name.clone()).or_default();
        let active = field::number(entry, "active", 0.0) as i32;
        if (1..=per_monitor).contains(&active) {
            workspaces.active_id = shoji_workspace_id(per_monitor, ordinal, active);
        }
        for workspace in entry.get("workspaces").and_then(Value::as_array).into_iter().flatten() {
            let index = field::number(workspace, "index", 0.0) as i32;
            if !(1..=per_monitor).contains(&index) {
                continue;
            }
            let id = shoji_workspace_id(per_monitor, ordinal, index);
            workspaces.workspaces.push(Workspace { id, name: index.to_string(), occupied: field::number(workspace, "windowCount", 0.0) > 0.0 });
            for window in workspace.get("windows").and_then(Value::as_array).into_iter().flatten() {
                let position = object_member(window, "position");
                let size = object_member(window, "size");
                let window_class = field::string(window, "appId");
                ranking.push(Ranked {
                    client: state.clients.len(),
                    active: field::boolean(window, "active", false),
                    last_focused: field::number(window, "lastFocusedAt", 0.0),
                });
                state.clients.push(CompositorClient {
                    address: field::string(window, "id"),
                    initial_title: window_class.clone(),
                    window_class,
                    title: field::string(window, "title"),
                    workspace_id: id,
                    monitor_id: ordinal,
                    at: [field::number(position, "x", 0.0), field::number(position, "y", 0.0)],
                    size: [field::number(size, "width", 0.0), field::number(size, "height", 0.0)],
                    ..Default::default()
                });
            }
        }
        workspaces.workspaces.sort_by_key(|w| w.id);
        state.monitors.push(monitor);
    }
    // Active window first, then most recently focused.
    ranking.sort_by(|a, b| b.active.cmp(&a.active).then(b.last_focused.partial_cmp(&a.last_focused).unwrap_or(std::cmp::Ordering::Equal)));
    for (history, rank) in (0..).zip(&ranking) {
        state.clients[rank.client].focus_history_id = history;
    }
    state
}

pub fn shoji_resolve_workspace(state: &CompositorState, id: i32, global: bool) -> i32 {
    let per_monitor = shoji_per_monitor(state);
    if global || !(1..=per_monitor).contains(&id) {
        return id;
    }
    let ordinal = compositor_monitor_by_name(state, &state.focused_monitor).map_or(0, |m| m.id);
    shoji_workspace_id(per_monitor, ordinal, id)
}

pub fn shoji_workspace_ref(state: &CompositorState, id: i32) -> Option<ShojiWorkspaceRef> {
    if id < 1 {
        return None;
    }
    let per_monitor = shoji_per_monitor(state);
    let monitor = compositor_monitor_by_id(state, (id - 1) / per_monitor)?;
    Some(ShojiWorkspaceRef { monitor: monitor.name.clone(), index: (id - 1) % per_monitor + 1 })
}

/// One JSON line; a null `params` is left out.
pub fn shoji_request(id: i32, method: &str, params: &Value) -> String {
    let mut message = json!({"id": id, "method": method});
    if !params.is_null() {
        message["params"] = params.clone();
    }
    format!("{message}\n")
}

fn without_focus(state: &CompositorState) -> CompositorState {
    let mut state = state.clone();
    state.focused_monitor.clear();
    for entry in state.by_monitor.values_mut() {
        entry.active_id = -1;
    }
    for client in &mut state.clients {
        client.focus_history_id = 0;
    }
    state
}

pub fn shoji_structure_differs(a: &CompositorState, b: &CompositorState) -> bool {
    without_focus(a) != without_focus(b)
}

fn hex_color(color: Color, alpha: &str) -> String {
    let hex: String = [color.r, color.g, color.b].iter().map(|c| format!("{:02x}", (c.clamp(0.0, 1.0) * 255.0 + 0.5) as i32)).collect();
    format!("#{hex}{alpha}")
}

pub fn shoji_border_params(active: Color, inactive: Color) -> Value {
    json!({"active": hex_color(active, "ee"), "inactive": hex_color(inactive, "cc")})
}

pub struct ShojiCompositor {
    this: Weak<ShojiCompositor>,
    path: String,
    state: RefCell<CompositorState>,
    events_connected: Cell<bool>,
    border_params: RefCell<Option<Value>>,
    next_id: Cell<i32>,
    active_changed: Signal,
    structure_changed: Signal,
}

impl ShojiCompositor {
    pub fn default_socket_path() -> String {
        let var = |name| std::env::var(name).unwrap_or_default();
        shoji_socket_path(&var("XDG_RUNTIME_DIR"), &var("WAYLAND_DISPLAY"))
    }

    pub fn available() -> bool {
        std::env::var("XDG_CURRENT_DESKTOP").unwrap_or_default().contains("ShojiWM") || std::path::Path::new(&Self::default_socket_path()).exists()
    }

    pub fn new(socket_path: String) -> Rc<ShojiCompositor> {
        let compositor = Rc::new_cyclic(|this| ShojiCompositor {
            this: this.clone(),
            path: socket_path,
            state: RefCell::default(),
            events_connected: Cell::new(false),
            border_params: RefCell::new(None),
            next_id: Cell::new(1),
            active_changed: Signal::default(),
            structure_changed: Signal::default(),
        });
        compositor.refresh();
        compositor.connect_events();
        runtime::spawn(reconnect(Rc::downgrade(&compositor)));
        compositor
    }

    fn connect_events(&self) {
        let Ok(stream) = connect_unix(&self.path, Some(REQUEST_TIMEOUT)).and_then(Async::new) else { return };
        self.events_connected.set(true);
        runtime::spawn(read_events(stream, self.this.clone()));
        ::log::info!("ShojiWM: listening on {}", self.path);
    }

    /// A blocking request bounded by the socket timeout; a reply without `result` is `Some(Null)`.
    fn call(&self, method: &str, params: &Value) -> Option<Value> {
        let mut stream = match connect_unix(&self.path, Some(REQUEST_TIMEOUT)) {
            Ok(stream) => stream,
            Err(e) => {
                ::log::error!("ShojiWM: cannot connect {}: {e}", self.path);
                return None;
            }
        };
        let id = self.next_id.get();
        self.next_id.set(id + 1);
        if stream.write_all(shoji_request(id, method, params).as_bytes()).is_err() {
            ::log::error!("ShojiWM: cannot send {method}");
            return None;
        }
        let mut buffer = Vec::new();
        let mut chunk = [0u8; 4096];
        while buffer.len() < REPLY_LIMIT {
            let Some(end) = buffer.iter().position(|&b| b == b'\n') else {
                match stream.read(&mut chunk) {
                    Ok(0) | Err(_) => break,
                    Ok(n) => buffer.extend_from_slice(&chunk[..n]),
                }
                continue;
            };
            let line: Vec<u8> = buffer.drain(..=end).collect();
            let Ok(line) = serde_json::from_slice::<Value>(&line) else { continue };
            if field::number(&line, "id", -1.0) != f64::from(id) {
                continue;
            }
            if let Some(error) = line.get("error") {
                ::log::error!("ShojiWM: {method} failed: {}", error.as_str().unwrap_or_default());
                return None;
            }
            return Some(line.get("result").cloned().unwrap_or(Value::Null));
        }
        ::log::error!("ShojiWM: no reply to {method}");
        None
    }

    fn apply(&self, next: CompositorState) {
        let structural = {
            let mut state = self.state.borrow_mut();
            if next == *state {
                return;
            }
            let structural = shoji_structure_differs(&state, &next);
            *state = next;
            structural
        };
        if structural {
            self.structure_changed.emit(&());
        } else {
            self.active_changed.emit(&());
        }
    }

    fn workspace_ref(&self, id: i32, global: bool) -> Option<ShojiWorkspaceRef> {
        let state = self.state.borrow();
        shoji_workspace_ref(&state, shoji_resolve_workspace(&state, id, global))
    }

    fn workspace_call(&self, method: &str, id: i32, global: bool) {
        if let Some(target) = self.workspace_ref(id, global) {
            self.call(method, &json!({"monitor": target.monitor, "index": target.index}));
        }
    }
}

async fn read_events(stream: Async<UnixStream>, compositor: Weak<ShojiCompositor>) {
    let mut stream = stream;
    let mut pending = Vec::new();
    let mut chunk = [0u8; 4096];
    loop {
        let read = stream.read(&mut chunk).await;
        let Some(compositor) = compositor.upgrade() else { return };
        let n = match read {
            Ok(0) | Err(_) => {
                ::log::info!("ShojiWM: the event socket closed");
                compositor.events_connected.set(false);
                return;
            }
            Ok(n) => n,
        };
        pending.extend_from_slice(&chunk[..n]);
        let mut latest = None;
        while let Some(end) = pending.iter().position(|&b| b == b'\n') {
            let line: Vec<u8> = pending.drain(..=end).collect();
            let Ok(line) = serde_json::from_slice::<Value>(&line) else { continue };
            if field::string(&line, "event") != "workspaces.changed" {
                continue;
            }
            if let Some(payload) = line.get("payload").filter(|p| p.is_object()) {
                latest = Some(shoji_parse_view(payload));
            }
        }
        if let Some(next) = latest {
            compositor.apply(next);
        }
    }
}

/// A config reload recreates ShojiWM's socket; reconnect, refresh and re-send the borders.
async fn reconnect(compositor: Weak<ShojiCompositor>) {
    loop {
        Timer::after(RECONNECT_INTERVAL).await;
        let Some(compositor) = compositor.upgrade() else { return };
        if compositor.events_connected.get() {
            continue;
        }
        compositor.connect_events();
        if !compositor.events_connected.get() {
            continue;
        }
        compositor.refresh();
        compositor.structure_changed.emit(&());
        let borders = compositor.border_params.borrow().clone();
        if let Some(params) = borders {
            compositor.call("theme.borders", &params);
        }
    }
}

impl Compositor for ShojiCompositor {
    fn name(&self) -> &'static str {
        "ShojiWM"
    }

    fn state(&self) -> Ref<'_, CompositorState> {
        self.state.borrow()
    }

    fn refresh(&self) {
        if let Some(view) = self.call("workspaces.get", &Value::Null).filter(Value::is_object) {
            *self.state.borrow_mut() = shoji_parse_view(&view);
        }
    }

    #[cfg(test)]
    fn refresh_clients(&self) -> bool {
        let before = self.state.borrow().clients.clone();
        self.refresh();
        before != self.state.borrow().clients
    }

    fn workspaces_per_monitor(&self) -> i32 {
        shoji_per_monitor(&self.state.borrow())
    }

    fn capture_by_toplevel_handle(&self) -> bool {
        true
    }

    fn focus_workspace(&self, id: i32, global: bool) {
        self.workspace_call("workspaces.activate", id, global);
    }

    fn move_window(&self, address: &str, id: i32, global: bool) {
        if let Some(target) = self.workspace_ref(id, global) {
            let params = json!({"windowId": address, "monitor": target.monitor, "index": target.index, "follow": false});
            self.call("windows.moveTo", &params);
        }
    }

    fn close_window(&self, address: &str) {
        self.call("windows.close", &json!({"windowId": address}));
    }

    fn close_windows(&self, scope: CloseScope, id: i32) {
        match scope {
            CloseScope::All => {
                self.call("windows.closeAll", &Value::Null);
            }
            CloseScope::Workspace => self.workspace_call("workspaces.clear", id, true),
            CloseScope::Monitor => {
                let targets: Vec<String> = self.state.borrow().clients.iter().filter(|c| c.monitor_id == id).map(|c| c.address.clone()).collect();
                for address in targets {
                    self.close_window(&address);
                }
            }
        }
    }

    fn move_workspace_in(&self, id: i32, global: bool) {
        self.workspace_call("workspaces.moveInto", id, global);
    }

    fn swap_workspace(&self, id: i32, global: bool) {
        self.workspace_call("workspaces.swap", id, global);
    }

    fn apply_border_colors(&self, active: Color, inactive: Color) {
        let params = shoji_border_params(active, inactive);
        self.call("theme.borders", &params);
        *self.border_params.borrow_mut() = Some(params);
    }

    fn active_changed(&self) -> &Signal {
        &self.active_changed
    }

    fn structure_changed(&self) -> &Signal {
        &self.structure_changed
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::tokens::Color;
    use crate::service::compositor::compositor_work_area;
    use crate::service::runtime;
    use calloop::EventLoop;
    use std::cell::Cell;
    use std::io::{BufRead, BufReader, Write};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::sync::{Arc, Mutex};
    use std::time::{Duration, Instant};

    const TWO_MONITORS: &str = r#"{
  "currentMonitor": "DP-2",
  "workspacesPerMonitor": 10,
  "monitors": [
    {"name": "DP-1", "active": 2, "rect": {"x": 0, "y": 0, "width": 1920, "height": 1080},
     "usable": {"x": 10, "y": 40, "width": 1900, "height": 990},
     "workspaces": [
       {"index": 1, "windowCount": 1, "active": false, "windows": [
         {"id": "w1", "appId": "kitty", "title": "shell", "active": false, "lastFocusedAt": 100,
          "position": {"x": 10, "y": 20}, "size": {"width": 800, "height": 600}}]},
       {"index": 2, "windowCount": 0, "active": true, "windows": []},
       {"index": 11, "windowCount": 1, "active": false, "windows": [
         {"id": "w9", "appId": "stray", "title": "", "lastFocusedAt": 1}]}]},
    {"name": "DP-2", "active": 1, "usable": {"x": 0, "y": 30, "width": 800, "height": 600},
     "workspaces": [
       {"index": 1, "windowCount": 2, "active": true, "windows": [
         {"id": "w2", "appId": "firefox", "title": "web", "active": true, "lastFocusedAt": 50,
          "position": {"x": 1930, "y": 5}, "size": {"width": 1000, "height": 700}},
         {"id": "w3", "appId": "foot", "title": "term", "active": false, "lastFocusedAt": 300}]}]}
  ]}"#;

    const CHANGED: &str = r#"{"event":"workspaces.changed","payload":{"currentMonitor":"DP-1","monitors":[{"name":"DP-1","active":1,"workspaces":[{"index":1,"windowCount":0,"active":true,"windows":[]}]}]}}"#;

    fn parse(text: &str) -> Value {
        serde_json::from_str(text).unwrap()
    }

    #[test]
    fn shojiwm_cases() {
        assert_eq!(shoji_socket_path("/run/user/1000", "wayland-1"), "/run/user/1000/shojiwm-wayland-1.sock");
        assert_eq!(shoji_socket_path("", ""), "/tmp/shojiwm-wayland-0.sock", "shoji socket path falls back like defaultSocketPath");

        let state = shoji_parse_view(&parse(TWO_MONITORS));
        assert_eq!(state.focused_monitor, "DP-2", "focused monitor comes from currentMonitor");
        assert!(state.monitors.len() == 2 && state.monitors[0].id == 0 && state.monitors[1].id == 1, "monitors keep their view order as ids");
        assert!(state.monitors[0].width == 1920.0 && state.monitors[0].scale == 1.0, "monitor rect is taken as logical size");
        assert_eq!(state.monitors[1].width, 0.0, "a missing rect leaves a zero monitor");
        assert_eq!(state.monitors[0].reserved, [10.0, 40.0, 10.0, 50.0], "reserved insets are the rect minus the usable area");
        let area = compositor_work_area(&state.monitors[0]);
        assert!(area.x == 10.0 && area.y == 40.0 && area.width == 1900.0 && area.height == 990.0, "the work area matches the usable area");
        assert_eq!(state.monitors[1].reserved, [0.0; 4], "a usable area without a rect reserves nothing");
        let bare = shoji_parse_view(&parse(r#"{"monitors":[{"name":"X","rect":{"width":100,"height":100}}]}"#));
        assert_eq!(bare.monitors[0].reserved, [0.0; 4], "a view without a usable area reserves nothing");
        assert_eq!(shoji_per_monitor(&bare), SHOJI_DEFAULT_WORKSPACES_PER_MONITOR, "a view without workspacesPerMonitor uses the default");

        let borders = shoji_border_params(Color { r: 1.0, g: 0.5, b: 0.0, a: 1.0 }, Color { r: 0.0, g: 0.0, b: 2.0, a: 1.0 });
        assert!(borders["active"] == "#ff8000ee" && borders["inactive"] == "#0000ffcc", "border params are hex with the hyprland alphas, channels clamped");
        let first = &state.by_monitor["DP-1"];
        assert!(
            first.workspaces.len() == 2 && first.workspaces[0].id == 1 && first.workspaces[1].id == 2 && first.workspaces[1].name == "2",
            "workspaces past the per-monitor limit are dropped"
        );
        assert!(first.workspaces[0].occupied && !first.workspaces[1].occupied, "occupied follows windowCount");
        assert_eq!(first.active_id, 2, "an empty active workspace stays active");
        let second = &state.by_monitor["DP-2"];
        assert!(
            second.workspaces.len() == 1 && second.workspaces[0].id == 11 && second.workspaces[0].name == "1",
            "the second monitor's index 1 maps to id 11 and keeps its label"
        );
        assert_eq!(second.active_id, 11, "second monitor active id is offset");
        assert_eq!(state.clients.len(), 3, "windows of dropped workspaces are skipped");
        let client = |a: &str| state.clients.iter().find(|c| c.address == a).unwrap();
        let (kitty, firefox, foot) = (client("w1"), client("w2"), client("w3"));
        assert!(kitty.window_class == "kitty" && kitty.workspace_id == 1 && kitty.monitor_id == 0, "client fields map from the window");
        assert!(kitty.at == [10.0, 20.0] && kitty.size[0] == 800.0, "client geometry maps from position and size");
        assert!(firefox.workspace_id == 11 && firefox.monitor_id == 1, "client on the second monitor gets the offset id");
        assert!(
            firefox.focus_history_id == 0 && foot.focus_history_id == 1 && kitty.focus_history_id == 2,
            "the active window leads, then most recent focus"
        );

        assert!(state.workspaces_per_monitor == 10 && shoji_per_monitor(&state) == 10, "the per-monitor count comes from the view");
        assert_eq!(shoji_workspace_id(10, 1, 3), 13, "workspace id offsets by monitor");
        assert_eq!(shoji_resolve_workspace(&state, 3, false), 13, "a bare index resolves on the focused monitor");
        assert_eq!(shoji_resolve_workspace(&state, 3, true), 3, "a global id stays");
        assert_eq!(shoji_resolve_workspace(&state, 13, false), 13, "an id past one monitor stays");
        assert_eq!(shoji_workspace_ref(&state, 13), Some(ShojiWorkspaceRef { monitor: "DP-2".into(), index: 3 }), "id 13 is DP-2 index 3");
        assert_eq!(shoji_workspace_ref(&state, 10), Some(ShojiWorkspaceRef { monitor: "DP-1".into(), index: 10 }), "id 10 is DP-1 index 10");
        assert!(shoji_workspace_ref(&state, 21).is_none(), "an id past the last monitor has no ref");
        assert!(shoji_workspace_ref(&state, 0).is_none(), "id 0 has no ref");

        assert_eq!(shoji_request(4, "workspaces.get", &Value::Null), "{\"id\":4,\"method\":\"workspaces.get\"}\n", "a request without params omits them");
        let encoded = parse(&shoji_request(5, "workspaces.activate", &serde_json::json!({"monitor": "DP-1"})));
        assert!(encoded["method"] == "workspaces.activate" && encoded["params"]["monitor"] == "DP-1", "a request carries its params");

        let mut refocused = state.clone();
        refocused.focused_monitor = "DP-1".into();
        refocused.by_monitor.get_mut("DP-1").unwrap().active_id = 1;
        refocused.clients[0].focus_history_id = 7;
        assert!(!shoji_structure_differs(&state, &refocused), "focus changes are not structural");
        refocused.clients.pop();
        assert!(shoji_structure_differs(&state, &refocused), "a closed window is structural");
    }

    fn read_line(stream: &UnixStream) -> String {
        let mut line = String::new();
        let _ = BufReader::new(stream).read_line(&mut line);
        line.trim_end().to_string()
    }

    /// Answers one request: `workspaces.get` with a broadcast line first, then the view; anything else with an empty result.
    fn answer(listener: &UnixListener, requests: &Mutex<Vec<String>>) {
        let (mut stream, _) = listener.accept().unwrap();
        let line = read_line(&stream);
        let request = parse(&line);
        requests.lock().unwrap().push(line);
        let id = request["id"].as_i64().unwrap_or(0);
        let reply = if request["method"] == "workspaces.get" {
            let view = TWO_MONITORS.replace('\n', "");
            format!("{{\"event\":\"dock.proximity\",\"payload\":{{}}}}\n{{\"id\":{id},\"result\":{view}}}\n")
        } else {
            format!("{{\"id\":{id}}}\n")
        };
        let _ = stream.write_all(reply.as_bytes());
    }

    fn pump(event_loop: &mut EventLoop<'static, ()>, limit: Duration, done: impl Fn() -> bool) -> bool {
        let deadline = Instant::now() + limit;
        while !done() && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
        }
        done()
    }

    #[test]
    fn shoji_against_fake_server() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("shoji.sock");
        let listener = UnixListener::bind(&path).unwrap();
        let requests = Arc::new(Mutex::new(Vec::new()));
        let seen = requests.clone();
        let server = std::thread::spawn(move || {
            answer(&listener, &seen);
            let (mut events, _) = listener.accept().unwrap();
            events.write_all(format!("{CHANGED}\n").as_bytes()).unwrap();
            answer(&listener, &seen);
        });
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let compositor = ShojiCompositor::new(path.to_str().unwrap().into());
        assert_eq!(compositor.state().monitors.len(), 2, "workspaces.get fills the state past a broadcast line");
        let structure = Rc::new(Cell::new(0));
        let s = structure.clone();
        compositor.structure_changed().connect(move |_| s.set(s.get() + 1));
        assert!(pump(&mut event_loop, Duration::from_secs(2), || structure.get() > 0), "a workspaces.changed broadcast arrives");
        assert!(
            compositor.state().monitors.len() == 1 && compositor.state().focused_monitor == "DP-1",
            "the broadcast replaces the state"
        );
        compositor.focus_workspace(1, true);
        server.join().unwrap();
        let requests = requests.lock().unwrap();
        assert_eq!(requests.len(), 2, "the fake server saw two requests");
        assert_eq!(parse(&requests[0])["method"], "workspaces.get", "first request is workspaces.get");
        let activate = parse(&requests[1]);
        assert!(
            activate["method"] == "workspaces.activate" && activate["params"]["monitor"] == "DP-1" && activate["params"]["index"] == 1,
            "focus_workspace sends workspaces.activate with monitor and index"
        );
    }

    #[test]
    fn shoji_reconnects() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("shoji.sock");
        let listener = UnixListener::bind(&path).unwrap();
        let requests = Arc::new(Mutex::new(Vec::new()));
        let seen = requests.clone();
        let (finish, finished) = std::sync::mpsc::channel::<()>();
        let server = std::thread::spawn(move || {
            answer(&listener, &seen);
            let (mut events, _) = listener.accept().unwrap();
            events.write_all(format!("{CHANGED}\n").as_bytes()).unwrap();
            drop(events);
            let (_events_again, _) = listener.accept().unwrap();
            answer(&listener, &seen);
            let _ = finished.recv_timeout(Duration::from_secs(5));
        });
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let compositor = ShojiCompositor::new(path.to_str().unwrap().into());
        let structure = Rc::new(Cell::new(0));
        let s = structure.clone();
        compositor.structure_changed().connect(move |_| s.set(s.get() + 1));
        assert!(pump(&mut event_loop, Duration::from_secs(1), || structure.get() == 1), "the first broadcast arrives");
        assert!(pump(&mut event_loop, Duration::from_secs(3), || structure.get() == 2), "the reconnect refreshes and signals");
        assert_eq!(compositor.state().monitors.len(), 2, "the refresh after the reconnect replaced the broadcast state");
        let _ = finish.send(());
        server.join().unwrap();
        assert_eq!(requests.lock().unwrap().iter().filter(|r| r.contains("workspaces.get")).count(), 2);
    }
}
