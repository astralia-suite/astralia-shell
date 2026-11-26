use std::cell::{Ref, RefCell};
use std::io::{Read, Write};
use std::net::Shutdown;
use std::os::unix::net::UnixStream;
use std::rc::{Rc, Weak};
use std::time::Duration;

use async_io::{Async, Timer};
use futures_lite::AsyncReadExt;
use serde_json::Value;

use crate::base::signal::Signal;
use crate::render::tokens::Color;
use crate::service::compositor::{
    CloseScope, Compositor, CompositorClient, CompositorMonitor, CompositorState, CursorPosition, Workspace, clients_in, connect_unix, json,
};
use crate::service::runtime;

const REQUEST_TIMEOUT: Duration = Duration::from_millis(100);
const WORKSPACES_PER_MONITOR: i32 = 5;
const SWAP_TEMP_WORKSPACE: &str = "special:__tmp_swp";
const LOAD_RETRY_INTERVAL: Duration = Duration::from_millis(500);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HyprEventKind {
    None,
    Active,
    Structural,
}

/// One request on a fresh socket; empty on any failure or timeout, which callers read as "no change".
fn request(socket_path: &str, command: &str) -> String {
    let Ok(mut stream) = connect_unix(socket_path, Some(REQUEST_TIMEOUT)) else { return String::new() };
    if stream.write_all(command.as_bytes()).is_err() {
        return String::new();
    }
    let _ = stream.shutdown(Shutdown::Write);
    let mut reply = Vec::new();
    match stream.read_to_end(&mut reply) {
        Ok(_) => String::from_utf8_lossy(&reply).into_owned(),
        Err(_) => String::new(),
    }
}

/// `std::from_chars` into an `int`: a leading optional `-` and digits, else `fallback`.
fn to_int(text: &str, fallback: i32) -> i32 {
    let digits = text.strip_prefix('-').unwrap_or(text).bytes().take_while(u8::is_ascii_digit).count();
    if digits == 0 {
        return fallback;
    }
    let end = digits + usize::from(text.starts_with('-'));
    text[..end].parse().unwrap_or(fallback)
}

fn pair_value(array: Option<&Value>, index: usize, fallback: f64) -> f64 {
    array.and_then(|a| a.as_array()).and_then(|a| a.get(index)).and_then(Value::as_f64).unwrap_or(fallback)
}

/// The request and event socket paths of the running Hyprland instance.
pub fn hypr_socket_paths() -> Option<(String, String)> {
    let signature = std::env::var("HYPRLAND_INSTANCE_SIGNATURE").ok().filter(|s| !s.is_empty())?;
    let is_dir = |d: &str| std::path::Path::new(d).is_dir();
    let mut directory = std::env::var("XDG_RUNTIME_DIR").ok().filter(|d| !d.is_empty()).map(|d| format!("{d}/hypr/{signature}")).unwrap_or_default();
    if directory.is_empty() || !is_dir(&directory) {
        directory = format!("/tmp/hypr/{signature}");
    }
    if !is_dir(&directory) {
        return None;
    }
    Some((format!("{directory}/.socket.sock"), format!("{directory}/.socket2.sock")))
}

pub fn hypr_parse_clients(reply: &str) -> Vec<CompositorClient> {
    let Some(Value::Array(entries)) = json::parse(reply) else { return Vec::new() };
    entries
        .iter()
        .map(|entry| CompositorClient {
            address: json::string(entry, "address"),
            window_class: json::string(entry, "class"),
            title: json::string(entry, "title"),
            initial_title: json::string(entry, "initialTitle"),
            workspace_id: entry.get("workspace").map_or(-1, |w| json::number(w, "id", -1.0) as i32),
            monitor_id: json::number(entry, "monitor", -1.0) as i32,
            at: [pair_value(entry.get("at"), 0, 0.0), pair_value(entry.get("at"), 1, 0.0)],
            size: [pair_value(entry.get("size"), 0, 100.0), pair_value(entry.get("size"), 1, 100.0)],
            floating: json::boolean(entry, "floating", false),
            fullscreen: json::number(entry, "fullscreen", 0.0) as i32,
            pinned: json::boolean(entry, "pinned", false),
            focus_history_id: json::number(entry, "focusHistoryID", 0.0) as i64,
            xwayland: json::boolean(entry, "xwayland", false),
        })
        .collect()
}

#[cfg(test)]
pub fn hypr_order_differs(a: &[CompositorClient], b: &[CompositorClient]) -> bool {
    a.len() != b.len()
        || a.iter().zip(b).any(|(a, b)| {
            a.address != b.address || a.workspace_id != b.workspace_id || a.at[0] != b.at[0] || a.focus_history_id != b.focus_history_id
        })
}

pub fn hypr_parse_workspaces(reply: &str, state: &mut CompositorState) {
    let Some(Value::Array(entries)) = json::parse(reply) else { return };
    state.by_monitor.clear();
    for entry in &entries {
        let workspace = Workspace {
            id: json::number(entry, "id", -1.0) as i32,
            name: json::string(entry, "name"),
            occupied: json::number(entry, "windows", 0.0) > 0.0,
        };
        if workspace.id < 0 {
            continue;
        }
        state.by_monitor.entry(json::string(entry, "monitor")).or_default().workspaces.push(workspace);
    }
    for entry in state.by_monitor.values_mut() {
        entry.workspaces.sort_by_key(|w| w.id);
    }
}

pub fn hypr_parse_monitors(reply: &str, state: &mut CompositorState) {
    let Some(Value::Array(entries)) = json::parse(reply) else { return };
    state.monitors.clear();
    for entry in &entries {
        let name = json::string(entry, "name");
        if let Some(workspace) = entry.get("activeWorkspace") {
            state.by_monitor.entry(name.clone()).or_default().active_id = json::number(workspace, "id", -1.0) as i32;
        }
        if json::boolean(entry, "focused", false) {
            state.focused_monitor = name.clone();
        }
        let reserved = entry.get("reserved");
        state.monitors.push(CompositorMonitor {
            id: json::number(entry, "id", -1.0) as i32,
            name,
            x: json::number(entry, "x", 0.0),
            y: json::number(entry, "y", 0.0),
            width: json::number(entry, "width", 0.0),
            height: json::number(entry, "height", 0.0),
            scale: json::number(entry, "scale", 1.0),
            transform: json::number(entry, "transform", 0.0) as i32,
            reserved: std::array::from_fn(|i| pair_value(reserved, i, 0.0)),
        });
    }
}

pub fn hypr_parse_gaps_out(reply: &str) -> i32 {
    json::parse(reply).map_or(0, |v| to_int(&json::string(&v, "css"), 0))
}

pub fn hypr_parse_rounding(reply: &str) -> i32 {
    json::parse(reply).map_or(0, |v| json::number(&v, "int", 0.0) as i32)
}

pub fn hypr_parse_cursor(reply: &str) -> Option<CursorPosition> {
    let v = json::parse(reply)?;
    v.get("x")?;
    v.get("y")?;
    Some(CursorPosition { x: json::number(&v, "x", 0.0), y: json::number(&v, "y", 0.0) })
}

const STRUCTURAL_EVENTS: [&str; 16] = [
    "createworkspacev2",
    "destroyworkspacev2",
    "renameworkspace",
    "moveworkspacev2",
    "openwindow",
    "closewindow",
    "movewindow",
    "movewindowv2",
    "pin",
    "fullscreen",
    "changefloatingmode",
    "activewindowv2",
    "moveintogroup",
    "moveoutofgroup",
    "togglegroup",
    "changegroupactivev2",
];

pub fn hypr_apply_event(state: &mut CompositorState, line: &str) -> HyprEventKind {
    let Some((event, data)) = line.split_once(">>") else { return HyprEventKind::None };
    match event {
        "focusedmonv2" => {
            let parts: Vec<&str> = data.split(',').collect();
            if parts.len() < 2 {
                return HyprEventKind::None;
            }
            state.focused_monitor = parts[0].to_string();
            state.by_monitor.entry(parts[0].to_string()).or_default().active_id = to_int(parts[1], -1);
            HyprEventKind::Active
        }
        "workspacev2" => {
            if state.focused_monitor.is_empty() {
                return HyprEventKind::None;
            }
            let id = to_int(data.split(',').next().unwrap_or(""), -1);
            state.by_monitor.entry(state.focused_monitor.clone()).or_default().active_id = id;
            HyprEventKind::Active
        }
        _ if STRUCTURAL_EVENTS.contains(&event) => HyprEventKind::Structural,
        _ => HyprEventKind::None,
    }
}

pub fn hypr_resolve_workspace(state: &CompositorState, id: i32, global: bool) -> i32 {
    if global || !(1..=WORKSPACES_PER_MONITOR).contains(&id) {
        return id;
    }
    let active = state.by_monitor.get(&state.focused_monitor).map(|m| m.active_id).filter(|&a| a > 0).unwrap_or(1);
    (active - 1) / WORKSPACES_PER_MONITOR * WORKSPACES_PER_MONITOR + id
}

pub fn hypr_lua_focus(workspace: i32) -> String {
    format!("hl.dsp.focus({{workspace={workspace}}})")
}

pub fn hypr_lua_move(address: &str, workspace: i32, follow: bool) -> String {
    let target = if address.is_empty() { "activewindow".to_string() } else { format!("address:{address}") };
    format!("hl.dsp.window.move({{window='{target}', workspace={workspace}, follow={follow}}})")
}

pub fn hypr_lua_close(address: &str) -> String {
    format!("hl.dsp.window.close({{window='address:{address}'}})")
}

pub fn hypr_lua_border(active: Color, inactive: Color) -> String {
    let rgba = |c: Color, alpha: &str| {
        let hex: String = [c.r, c.g, c.b].iter().map(|ch| format!("{:02x}", (ch.clamp(0.0, 1.0) * 255.0 + 0.5) as i32)).collect();
        format!("rgba({hex}{alpha})")
    };
    format!("hl.config({{general={{col={{active_border=\"{}\",inactive_border=\"{}\"}}}}}})", rgba(active, "ee"), rgba(inactive, "cc"))
}

pub struct HyprlandCompositor {
    state: RefCell<CompositorState>,
    request_path: RefCell<String>,
    clients_reply: RefCell<String>,
    border_lua: RefCell<String>,
    active_changed: Signal,
    structure_changed: Signal,
}

impl HyprlandCompositor {
    pub fn new() -> Option<Rc<HyprlandCompositor>> {
        let (request, events) = hypr_socket_paths()?;
        Some(HyprlandCompositor::with_paths(request, events))
    }

    /// Empty paths leave that side unconnected.
    pub fn with_paths(request_path: String, event_path: String) -> Rc<HyprlandCompositor> {
        let compositor = Rc::new(HyprlandCompositor {
            state: RefCell::default(),
            request_path: RefCell::new(request_path),
            clients_reply: RefCell::default(),
            border_lua: RefCell::default(),
            active_changed: Signal::default(),
            structure_changed: Signal::default(),
        });
        if !compositor.request_path.borrow().is_empty() && !compositor.load() {
            ::log::warn!("hyprland: no reply at startup, retrying");
            runtime::spawn(retry_load(Rc::downgrade(&compositor)));
        }
        if !event_path.is_empty() {
            match connect_unix(&event_path, None).and_then(Async::new) {
                Ok(stream) => runtime::spawn(read_events(stream, Rc::downgrade(&compositor))),
                Err(e) => ::log::error!("hyprland: cannot connect the event socket: {e}"),
            }
        }
        compositor
    }

    /// The full state and the gaps; false when Hyprland did not answer in time.
    fn load(&self) -> bool {
        if !self.try_refresh() {
            return false;
        }
        let gaps_out = hypr_parse_gaps_out(&self.request("j/getoption general:gaps_out"));
        let rounding = hypr_parse_rounding(&self.request("j/getoption decoration:rounding"));
        let mut state = self.state.borrow_mut();
        state.gaps_out_px = Some(gaps_out);
        state.hug_radius_px = gaps_out + rounding;
        true
    }

    fn try_refresh(&self) -> bool {
        let workspaces = self.request("j/workspaces");
        let monitors = self.request("j/monitors");
        let clients = self.request("j/clients");
        if workspaces.is_empty() || monitors.is_empty() || clients.is_empty() {
            return false;
        }
        let mut state = self.state.borrow_mut();
        hypr_parse_workspaces(&workspaces, &mut state);
        hypr_parse_monitors(&monitors, &mut state);
        state.clients = hypr_parse_clients(&clients);
        *self.clients_reply.borrow_mut() = clients;
        true
    }

    #[cfg(test)]
    pub fn set_request_socket(&self, path: String) {
        *self.request_path.borrow_mut() = path;
    }

    fn request(&self, command: &str) -> String {
        let path = self.request_path.borrow().clone();
        if path.is_empty() { String::new() } else { request(&path, command) }
    }

    fn dispatch(&self, lua: &str) {
        self.request(&format!("dispatch {lua}"));
    }

    fn move_all(&self, addresses: &[String], workspace_lua: &str) {
        for address in addresses {
            self.dispatch(&format!("hl.dsp.window.move({{window='address:{address}', workspace={workspace_lua}, follow=false}})"));
        }
    }

    fn addresses_in(&self, scope: CloseScope, id: i32) -> Vec<String> {
        clients_in(&self.state.borrow(), scope, id).iter().map(|c| c.address.clone()).collect()
    }

    fn active_on_focused(&self) -> i32 {
        let state = self.state.borrow();
        state.by_monitor.get(&state.focused_monitor).map_or(-1, |m| m.active_id)
    }

    /// Applies one chunk of event lines and emits at most one signal for the batch.
    fn apply_events(&self, lines: &[String]) {
        let mut result = HyprEventKind::None;
        for line in lines {
            if line.starts_with("configreloaded>>") {
                let border = self.border_lua.borrow().clone();
                if !border.is_empty() {
                    self.request(&format!("eval {border}"));
                }
            }
            let kind = hypr_apply_event(&mut self.state.borrow_mut(), line);
            if kind == HyprEventKind::Structural || (kind == HyprEventKind::Active && result == HyprEventKind::None) {
                result = kind;
            }
        }
        match result {
            HyprEventKind::Structural => {
                self.refresh();
                self.structure_changed.emit(&());
            }
            HyprEventKind::Active => self.active_changed.emit(&()),
            HyprEventKind::None => {}
        }
    }
}

async fn read_events(stream: Async<UnixStream>, compositor: Weak<HyprlandCompositor>) {
    let mut stream = stream;
    let mut pending = Vec::new();
    let mut buffer = [0u8; 4096];
    loop {
        let n = match stream.read(&mut buffer).await {
            Ok(0) | Err(_) => {
                ::log::error!("hyprland: the event socket closed");
                return;
            }
            Ok(n) => n,
        };
        pending.extend_from_slice(&buffer[..n]);
        let Some(last) = pending.iter().rposition(|&b| b == b'\n') else { continue };
        let complete: Vec<u8> = pending.drain(..=last).collect();
        let lines: Vec<String> = String::from_utf8_lossy(&complete).lines().map(str::to_string).collect();
        let Some(compositor) = compositor.upgrade() else { return };
        compositor.apply_events(&lines);
    }
}

/// Hyprland can be too busy at login to answer within `REQUEST_TIMEOUT`; keep asking until it does.
async fn retry_load(compositor: Weak<HyprlandCompositor>) {
    loop {
        Timer::after(LOAD_RETRY_INTERVAL).await;
        let Some(compositor) = compositor.upgrade() else { return };
        if compositor.load() {
            ::log::info!("hyprland: loaded after retry");
            compositor.structure_changed.emit(&());
            return;
        }
    }
}

impl Compositor for HyprlandCompositor {
    fn name(&self) -> &'static str {
        "Hyprland"
    }

    fn state(&self) -> Ref<'_, CompositorState> {
        self.state.borrow()
    }

    fn refresh(&self) {
        self.try_refresh();
    }

    /// True when the client order changed; an empty or identical reply is no change.
    #[cfg(test)]
    fn refresh_clients(&self) -> bool {
        let reply = self.request("j/clients");
        if reply.is_empty() || reply == *self.clients_reply.borrow() {
            return false;
        }
        let fresh = hypr_parse_clients(&reply);
        *self.clients_reply.borrow_mut() = reply;
        let mut state = self.state.borrow_mut();
        if !hypr_order_differs(&fresh, &state.clients) {
            return false;
        }
        state.clients = fresh;
        true
    }

    fn focus_workspace(&self, id: i32, global: bool) {
        let target = hypr_resolve_workspace(&self.state.borrow(), id, global);
        self.dispatch(&hypr_lua_focus(target));
    }

    fn move_window(&self, address: &str, id: i32, global: bool) {
        let target = hypr_resolve_workspace(&self.state.borrow(), id, global);
        self.dispatch(&hypr_lua_move(address, target, false));
    }

    fn close_window(&self, address: &str) {
        self.dispatch(&hypr_lua_close(address));
    }

    fn close_windows(&self, scope: CloseScope, id: i32) {
        for address in self.addresses_in(scope, id) {
            self.dispatch(&hypr_lua_close(&address));
        }
        if scope == CloseScope::All {
            self.focus_workspace(1, false);
        }
    }

    fn move_workspace_in(&self, id: i32, global: bool) {
        let destination = hypr_resolve_workspace(&self.state.borrow(), id, global);
        let source = self.active_on_focused();
        if source < 0 || source == destination {
            return;
        }
        self.move_all(&self.addresses_in(CloseScope::Workspace, source), &destination.to_string());
        self.focus_workspace(id, global);
    }

    fn swap_workspace(&self, id: i32, global: bool) {
        let destination = hypr_resolve_workspace(&self.state.borrow(), id, global);
        let source = self.active_on_focused();
        if source < 0 || source == destination {
            return;
        }
        let from_source = self.addresses_in(CloseScope::Workspace, source);
        let from_destination = self.addresses_in(CloseScope::Workspace, destination);
        if from_source.is_empty() && from_destination.is_empty() {
            return;
        }
        self.move_all(&from_source, &format!("'{SWAP_TEMP_WORKSPACE}'"));
        self.move_all(&from_destination, &source.to_string());
        self.move_all(&from_source, &destination.to_string());
        self.focus_workspace(id, global);
    }

    fn apply_border_colors(&self, active: Color, inactive: Color) {
        let lua = hypr_lua_border(active, inactive);
        self.request(&format!("eval {lua}"));
        *self.border_lua.borrow_mut() = lua;
    }

    fn cursor_position(&self) -> Option<CursorPosition> {
        hypr_parse_cursor(&self.request("j/cursorpos"))
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
    use crate::render::tokens::{Color, color};
    use crate::service::runtime;
    use calloop::EventLoop;
    use std::cell::Cell;
    use std::io::{Read, Write};
    use std::os::unix::net::UnixListener;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    #[test]
    fn hyprland_parsers() {
        let clients = hypr_parse_clients(
            r#"[{"address":"0x1","class":"kitty","title":"t","initialTitle":"Kitty","workspace":{"id":3,"name":"3"},"monitor":1,"at":[10,20],"size":[300,200],"floating":true,"fullscreen":2,"pinned":true,"focusHistoryID":4,"xwayland":true},{"address":"0x2"}]"#,
        );
        assert_eq!(clients.len(), 2, "two clients parse");
        assert!(clients[0].initial_title == "Kitty" && clients[1].initial_title.is_empty(), "client initial title");
        assert!(
            clients[0].address == "0x1" && clients[0].window_class == "kitty" && clients[0].workspace_id == 3 && clients[0].monitor_id == 1,
            "client identity"
        );
        assert!(clients[0].at == [10.0, 20.0] && clients[0].size == [300.0, 200.0], "client geometry");
        assert!(
            clients[0].floating && clients[0].fullscreen == 2 && clients[0].pinned && clients[0].focus_history_id == 4 && clients[0].xwayland,
            "client flags"
        );
        assert!(clients[1].workspace_id == -1 && clients[1].size[0] == 100.0, "missing fields take the defaults");
        assert!(hypr_parse_clients("garbage").is_empty() && hypr_parse_clients("{}").is_empty(), "invalid replies give no clients");
        let mut moved = clients.clone();
        assert!(!hypr_order_differs(&clients, &moved), "identical lists do not differ");
        moved[0].at[0] = 99.0;
        assert!(hypr_order_differs(&clients, &moved), "a moved window differs");
        let mut moved = clients.clone();
        moved.pop();
        assert!(hypr_order_differs(&clients, &moved), "a different count differs");

        let mut state = CompositorState::default();
        hypr_parse_workspaces(
            r#"[{"id":2,"name":"2","monitor":"DP-1","windows":0},{"id":1,"name":"1","monitor":"DP-1","windows":3},{"id":-99,"name":"special","monitor":"DP-1","windows":1},{"id":11,"name":"11","monitor":"HDMI-1","windows":1}]"#,
            &mut state,
        );
        let dp = &state.by_monitor["DP-1"].workspaces;
        assert!(dp.len() == 2 && dp[0].id == 1 && dp[0].occupied && !dp[1].occupied, "workspaces are sorted and the special one is dropped");
        assert_eq!(state.by_monitor["HDMI-1"].workspaces.len(), 1, "workspaces group by monitor");
        hypr_parse_monitors(
            r#"[{"id":0,"name":"DP-1","x":0,"y":0,"width":1920,"height":1080,"scale":1.5,"transform":1,"focused":true,"activeWorkspace":{"id":2},"reserved":[0,40,0,0]},{"id":1,"name":"HDMI-1","width":1280,"height":720,"activeWorkspace":{"id":11}}]"#,
            &mut state,
        );
        assert!(
            state.monitors.len() == 2 && state.monitors[0].scale == 1.5 && state.monitors[0].transform == 1 && state.monitors[0].reserved[1] == 40.0,
            "monitors parse"
        );
        assert!(
            state.focused_monitor == "DP-1" && state.by_monitor["DP-1"].active_id == 2 && state.by_monitor["HDMI-1"].active_id == 11,
            "focus and active workspaces"
        );

        let mut state = CompositorState::default();
        assert_eq!(hypr_apply_event(&mut state, "garbage"), HyprEventKind::None, "an event without a separator is ignored");
        assert!(
            hypr_apply_event(&mut state, "focusedmonv2>>DP-1,4") == HyprEventKind::Active
                && state.focused_monitor == "DP-1"
                && state.by_monitor["DP-1"].active_id == 4,
            "focused monitor event"
        );
        assert!(
            hypr_apply_event(&mut state, "workspacev2>>7,seven") == HyprEventKind::Active && state.by_monitor["DP-1"].active_id == 7,
            "workspace event updates the focused monitor"
        );
        assert_eq!(hypr_apply_event(&mut state, "openwindow>>0x1,1,kitty,t"), HyprEventKind::Structural, "openwindow is structural");
        assert_eq!(hypr_apply_event(&mut state, "activewindowv2>>0x1"), HyprEventKind::Structural, "focus change is structural");
        assert_eq!(hypr_apply_event(&mut state, "submap>>x"), HyprEventKind::None, "unrelated events are ignored");
        let mut empty = CompositorState::default();
        assert_eq!(
            hypr_apply_event(&mut empty, "workspacev2>>3,three"),
            HyprEventKind::None,
            "a workspace event without a focused monitor is ignored"
        );

        let mut state = CompositorState { focused_monitor: "DP-1".into(), ..Default::default() };
        state.by_monitor.entry("DP-1".into()).or_default().active_id = 14;
        assert_eq!(hypr_resolve_workspace(&state, 3, false), 13, "workspace ids resolve into the focused monitor's page");
        assert_eq!(hypr_resolve_workspace(&state, 3, true), 3, "global ids are kept");
        assert!(hypr_resolve_workspace(&state, 25, false) == 25 && hypr_resolve_workspace(&state, 0, false) == 0, "ids outside a page are kept");
        assert_eq!(hypr_resolve_workspace(&CompositorState::default(), 4, false), 4, "without an active workspace the first page is used");
        assert_eq!(hypr_lua_focus(5), "hl.dsp.focus({workspace=5})", "focus command");
        assert_eq!(hypr_lua_move("0x9", 3, true), "hl.dsp.window.move({window='address:0x9', workspace=3, follow=true})", "move command");
        assert_eq!(
            hypr_lua_move("", 3, false),
            "hl.dsp.window.move({window='activewindow', workspace=3, follow=false})",
            "move command without an address targets the active window"
        );
        assert_eq!(hypr_lua_close("0x9"), "hl.dsp.window.close({window='address:0x9'})", "close command");

        assert!(
            hypr_parse_gaps_out(r#"{"option":"general:gaps_out","css":"20 20 20 20","set":true}"#) == 20
                && hypr_parse_gaps_out("x") == 0
                && hypr_parse_gaps_out("{}") == 0,
            "gaps_out parses"
        );
        assert!(
            hypr_parse_rounding(r#"{"option":"decoration:rounding","int":10,"set":true}"#) == 10 && hypr_parse_rounding("x") == 0,
            "rounding parses"
        );
        let cursor = hypr_parse_cursor(r#"{"x":1610,"y":-27}"#).unwrap();
        assert!(cursor.x == 1610.0 && cursor.y == -27.0, "cursor position parses");
        assert!(hypr_parse_cursor("unknown request").is_none() && hypr_parse_cursor(r#"{"x":1}"#).is_none(), "a malformed cursor reply yields nothing");
        assert_eq!(
            hypr_lua_border(color("#00feee"), color("#757bcc")),
            r#"hl.config({general={col={active_border="rgba(00feeeee)",inactive_border="rgba(757bcccc)"}}})"#,
            "border colours become a Lua config table"
        );
        assert!(
            hypr_lua_border(Color { r: 2.0, g: -1.0, b: 1.0, a: 1.0 }, Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }).contains("rgba(ff00ffee)"),
            "border channels are clamped"
        );
    }

    /// Accepts one request, drains it, answers `reply`, then holds the connection open.
    fn one_shot(listener: UnixListener, reply: Option<&'static str>, hold: Duration) -> std::thread::JoinHandle<()> {
        std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut sink = Vec::new();
            let _ = stream.read_to_end(&mut sink);
            if let Some(reply) = reply {
                let _ = stream.write_all(reply.as_bytes());
            }
            std::thread::sleep(hold);
        })
    }

    const ONE_CLIENT: &str = r#"[{"address": "0x1", "class": "kitty", "title": "t", "workspace": {"id": 1}, "monitor": 0, "at": [10, 0]}]"#;

    #[test]
    fn hypr_request_times_out() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("h.sock");
        let compositor = HyprlandCompositor::with_paths(String::new(), String::new());
        compositor.set_request_socket(path.to_str().unwrap().into());
        let server = one_shot(UnixListener::bind(&path).unwrap(), None, Duration::from_millis(400));
        let start = Instant::now();
        let changed = compositor.refresh_clients();
        assert!(!changed && start.elapsed() < Duration::from_millis(300), "a stalled compositor times out and reads as no change");
        assert!(compositor.state().clients.is_empty(), "a timeout keeps the previous clients");
        server.join().unwrap();

        std::fs::remove_file(&path).unwrap();
        let server = one_shot(UnixListener::bind(&path).unwrap(), Some(ONE_CLIENT), Duration::ZERO);
        assert!(compositor.refresh_clients(), "a new client list is a change");
        assert!(compositor.state().clients.len() == 1 && compositor.state().clients[0].address == "0x1", "clients are stored");
        server.join().unwrap();
    }

    #[test]
    fn hypr_empty_reply_keeps_state() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("h.sock");
        let compositor = HyprlandCompositor::with_paths(String::new(), String::new());
        compositor.set_request_socket(path.to_str().unwrap().into());
        let server = one_shot(UnixListener::bind(&path).unwrap(), Some(ONE_CLIENT), Duration::ZERO);
        assert!(compositor.refresh_clients());
        server.join().unwrap();
        std::fs::remove_file(&path).unwrap();
        let server = one_shot(UnixListener::bind(&path).unwrap(), None, Duration::ZERO);
        assert!(!compositor.refresh_clients(), "an empty reply is no change");
        assert_eq!(compositor.state().clients.len(), 1, "an empty reply keeps the clients");
        server.join().unwrap();
    }

    #[test]
    fn hypr_events_reach_signal() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("e.sock");
        let listener = UnixListener::bind(&path).unwrap();
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let compositor = HyprlandCompositor::with_paths(String::new(), path.to_str().unwrap().into());
        let (mut events, _) = listener.accept().unwrap();
        let (active, structure) = (Rc::new(Cell::new(0)), Rc::new(Cell::new(0)));
        let (a, s) = (active.clone(), structure.clone());
        compositor.active_changed().connect(move |_| a.set(a.get() + 1));
        compositor.structure_changed().connect(move |_| s.set(s.get() + 1));
        let mut pump = |done: &dyn Fn() -> bool| {
            let deadline = Instant::now() + Duration::from_secs(2);
            while !done() && Instant::now() < deadline {
                event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
            }
        };
        events.write_all(b"focusedmonv2>>DP-1,2\n").unwrap();
        pump(&|| active.get() > 0);
        assert_eq!(active.get(), 1, "an active event fires active_changed");
        assert_eq!(compositor.state().by_monitor["DP-1"].active_id, 2);
        events.write_all(b"workspacev2>>3,3\nactivewindowv2>>abc\n").unwrap();
        pump(&|| structure.get() > 0);
        assert_eq!(structure.get(), 1, "a batch with a structural event fires structure_changed once");
        assert_eq!(active.get(), 1, "the structural refresh replaces the active signal");
        assert_eq!(compositor.state().by_monitor["DP-1"].active_id, 3);
    }

    #[test]
    fn hypr_startup_retries_until_answered() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("h.sock");
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let compositor = HyprlandCompositor::with_paths(path.to_str().unwrap().into(), String::new());
        assert!(compositor.state().monitors.is_empty(), "no socket yet, nothing loaded");
        let structure = Rc::new(Cell::new(0));
        let s = structure.clone();
        compositor.structure_changed().connect(move |_| s.set(s.get() + 1));
        let listener = UnixListener::bind(&path).unwrap();
        std::thread::spawn(move || {
            for stream in listener.incoming() {
                let mut stream = stream.unwrap();
                let mut command = String::new();
                let _ = stream.read_to_string(&mut command);
                let reply = match command.as_str() {
                    "j/workspaces" => r#"[{"id": 1, "name": "1", "monitor": "DP-1", "windows": 1}]"#,
                    "j/monitors" => r#"[{"id": 0, "name": "DP-1", "focused": true, "activeWorkspace": {"id": 1}}]"#,
                    "j/clients" => ONE_CLIENT,
                    _ => r#"{"css": "8 8 8 8"}"#,
                };
                let _ = stream.write_all(reply.as_bytes());
            }
        });
        let deadline = Instant::now() + Duration::from_secs(3);
        while structure.get() == 0 && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
        }
        assert_eq!(structure.get(), 1, "a late answer loads the state and fires structure_changed");
        assert_eq!(compositor.state().by_monitor["DP-1"].active_id, 1);
        assert_eq!(compositor.state().gaps_out_px, Some(8), "the gaps load with the retry");
    }
}
