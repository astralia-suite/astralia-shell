use std::cell::{Cell, Ref, RefCell};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::rc::{Rc, Weak};
use std::time::Duration;

use async_io::Timer;
use futures_lite::StreamExt;
use zbus::message::Type as MessageType;
use zbus::zvariant::{OwnedObjectPath, OwnedValue};

use crate::base::dbus::{get_all, value};
use crate::base::process::{AsyncProcess, spawn_detached};
use crate::base::signal::Signal;
use crate::service::runtime;

const NM_SERVICE: &str = "org.freedesktop.NetworkManager";
const NM_PATH: &str = "/org/freedesktop/NetworkManager";
const NM_ACTIVE_INTERFACE: &str = "org.freedesktop.NetworkManager.Connection.Active";
const NM_ACCESS_POINT_INTERFACE: &str = "org.freedesktop.NetworkManager.AccessPoint";
const CONNECTIVITY_INTERVAL: Duration = Duration::from_secs(30);

const RESCAN_PENDING: Duration = Duration::from_millis(750);
const RESCAN_EMPTY: Duration = Duration::from_millis(2000);
const RESCAN_IDLE: Duration = Duration::from_millis(7000);
const RESCAN_AFTER_CONNECT: Duration = Duration::from_millis(5000);
const RESCAN_AFTER_CHANGE: Duration = Duration::from_millis(3000);
const RESCAN_AFTER_STATE: Duration = Duration::from_millis(1000);

const PORTAL_URLS: [&str; 4] = ["http://httpforever.com", "http://captive.apple.com/hotspot-detect.html", "http://example.com", "http://neverssl.net"];
const PORTAL_PROBE_SECONDS: i32 = 3;
const BROWSER: &str = "astralia-open browser";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum NetworkKind {
    #[default]
    None,
    Ethernet,
    Wifi,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NetworkStatus {
    pub kind: NetworkKind,
    pub strength: i32,
    pub ssid: String,
    pub portal: bool,
}

/// A user-facing notice about a connection change; `click_command` runs when it is clicked.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct StatusMessage {
    pub summary: String,
    pub body: String,
    pub click_command: String,
}

impl StatusMessage {
    pub fn new(summary: &str, body: String) -> StatusMessage {
        StatusMessage { summary: summary.into(), body, click_command: String::new() }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NetworkInfo {
    pub ssid: String,
    pub security: String,
    pub signal: i32,
    pub connected: bool,
    pub existing: bool,
    pub in_range: bool,
}

pub type NetworkMap = BTreeMap<String, NetworkInfo>;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NetworkDeviceStatus {
    pub wifi: bool,
    pub ethernet: bool,
    pub ethernet_connected: bool,
    pub ethernet_name: String,
}

fn wifi_ssid(status: &NetworkStatus) -> &str {
    if status.kind == NetworkKind::Wifi { &status.ssid } else { "" }
}

/// `nmcli -t` fields: `:` separates, `\` escapes the next character.
fn split_fields(line: &str) -> Vec<String> {
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut chars = line.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '\\' if chars.peek().is_some() => current.extend(chars.next()),
            ':' => parts.push(std::mem::take(&mut current)),
            _ => current.push(c),
        }
    }
    parts.push(current);
    parts
}

fn lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines().map(|l| l.trim_matches([' ', '\t', '\r', '\n'])).filter(|l| !l.is_empty())
}

fn leading_int(text: &str) -> i32 {
    let digits = text.strip_prefix('-').unwrap_or(text).bytes().take_while(u8::is_ascii_digit).count();
    let end = digits + usize::from(text.starts_with('-'));
    if digits == 0 { 0 } else { text[..end].parse().unwrap_or(0) }
}

fn connect_error(output: &str) -> &'static str {
    if output.contains("Secrets were required") || output.contains("no secrets provided") {
        "Incorrect password"
    } else if output.contains("No network with SSID") {
        "Network not found"
    } else if output.contains("Timeout") {
        "Connection timed out"
    } else {
        "Connection failed"
    }
}

/// Probes plain-HTTP pages and opens the first that answers, which a captive portal redirects.
pub fn network_portal_command() -> String {
    let urls: String = PORTAL_URLS.iter().map(|u| format!(" {u}")).collect();
    format!(
        "for url in{urls}; do curl -so /dev/null --max-time {PORTAL_PROBE_SECONDS} \"$url\" && exec {BROWSER} \"$url\"; done; exec {BROWSER} {}",
        PORTAL_URLS[0]
    )
}

pub fn network_changes(prev: &NetworkStatus, next: &NetworkStatus) -> Vec<StatusMessage> {
    let mut messages = Vec::new();
    let (was, now) = (wifi_ssid(prev), wifi_ssid(next));
    if now != was {
        messages.push(if now.is_empty() {
            StatusMessage::new("Disconnected", format!("Disconnected from {was}"))
        } else {
            StatusMessage::new("Connected", format!("Connected to {now}"))
        });
    }
    let was_ethernet = prev.kind == NetworkKind::Ethernet;
    let now_ethernet = next.kind == NetworkKind::Ethernet;
    if now_ethernet && !was_ethernet {
        messages.push(StatusMessage::new("Connected", "Connected via Ethernet".into()));
    } else if was_ethernet && !now_ethernet {
        messages.push(StatusMessage::new("Disconnected", "Ethernet disconnected".into()));
    }
    if next.portal && !prev.portal {
        let body = if now.is_empty() { "Sign in required".to_string() } else { format!("Sign in required for {now}") };
        messages.push(StatusMessage { summary: "Captive Portal".into(), body, click_command: network_portal_command() });
    }
    messages
}

pub fn network_parse_networks(text: &str, profiles: &BTreeSet<String>) -> NetworkMap {
    let mut result = NetworkMap::new();
    for line in lines(text) {
        let parts = split_fields(line);
        if parts.len() != 4 || parts[0].is_empty() {
            continue;
        }
        let ssid = &parts[0];
        let security = parts[1].replacen("WPA2 WPA3", "WPA2/WPA3", 1).replacen("WPA1 WPA2", "WPA1/WPA2", 1);
        let connected = parts[3] == "*";
        match result.get_mut(ssid) {
            Some(existing) => existing.connected |= connected,
            None => {
                let security = if security.is_empty() { "--".to_string() } else { security };
                let info = NetworkInfo { ssid: ssid.clone(), security, signal: leading_int(&parts[2]), connected, existing: profiles.contains(ssid), in_range: true };
                result.insert(ssid.clone(), info);
            }
        }
    }
    for name in profiles {
        result.entry(name.clone()).or_insert_with(|| profile_stub(name));
    }
    result
}

/// A saved profile not seen in a scan.
fn profile_stub(name: &str) -> NetworkInfo {
    NetworkInfo { ssid: name.into(), security: "--".into(), signal: 0, connected: false, existing: true, in_range: false }
}

pub fn network_parse_device_status(text: &str) -> NetworkDeviceStatus {
    let mut status = NetworkDeviceStatus::default();
    for line in lines(text) {
        let parts = split_fields(line);
        if parts.len() < 3 || parts[2] == "unmanaged" {
            continue;
        }
        match parts[1].as_str() {
            "wifi" => status.wifi = true,
            "ethernet" => {
                status.ethernet = true;
                if parts[2].starts_with("connected") {
                    status.ethernet_connected = true;
                    if status.ethernet_name.is_empty() {
                        status.ethernet_name = parts[3..].join(":");
                    }
                }
            }
            _ => {}
        }
    }
    status
}

#[cfg(test)]
pub fn network_parse_wifi_device(text: &str) -> bool {
    network_parse_device_status(text).wifi
}

pub fn connectivity_name(value: u32) -> &'static str {
    match value {
        1 => "none",
        2 => "portal",
        3 => "limited",
        4 => "full",
        _ => "unknown",
    }
}

pub fn network_parse_profiles(text: &str) -> BTreeSet<String> {
    lines(text)
        .map(split_fields)
        .filter(|parts| parts.len() == 2 && parts[1] == "802-11-wireless" && !parts[0].is_empty())
        .map(|mut parts| parts.swap_remove(0))
        .collect()
}

pub fn network_visible_count(networks: &NetworkMap) -> i32 {
    networks.values().filter(|n| n.connected || n.in_range || !n.existing).count() as i32
}

/// A rescan that sees only the connected network while more were listed is a driver hiccup.
pub fn network_scan_collapses(current: &NetworkMap, parsed: &NetworkMap) -> bool {
    let in_range: Vec<&NetworkInfo> = parsed.values().filter(|n| n.in_range).collect();
    in_range.len() == 1 && in_range[0].connected && network_visible_count(current) > 1
}

fn device_argv() -> Vec<String> {
    to_args(&["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"])
}

fn list_argv(rescan: bool) -> Vec<String> {
    to_args(&["nmcli", "-t", "-f", "SSID,SECURITY,SIGNAL,IN-USE", "device", "wifi", "list", "--rescan", if rescan { "yes" } else { "no" }])
}

fn connect_argv(ssid: &str, password: &str, saved: bool) -> Vec<String> {
    if saved {
        return to_args(&["nmcli", "-t", "connection", "up", "id", ssid]);
    }
    let mut argv = to_args(&["nmcli", "-t", "device", "wifi", "connect", ssid]);
    if !password.is_empty() {
        argv.extend(to_args(&["password", password]));
    }
    argv
}

const FORGET_SCRIPT: &str = r#"
ssid="$1"
uuid=$(nmcli -t -f NAME,UUID,TYPE connection show | awk -F: -v target="$ssid" '$1 == target && $3 == "802-11-wireless" { print $2; exit }')
[ -n "$uuid" ] && nmcli connection delete uuid "$uuid"
"#;

fn to_args(args: &[&str]) -> Vec<String> {
    args.iter().map(|a| a.to_string()).collect()
}

/// Selects one of the service's process slots.
type Slot = fn(&NetworkService) -> &RefCell<AsyncProcess>;

pub struct NetworkService {
    this: Weak<NetworkService>,
    bus: zbus::Connection,
    status: RefCell<NetworkStatus>,
    device: RefCell<NetworkDeviceStatus>,
    connectivity: RefCell<String>,
    initialized: Cell<bool>,
    refresh_generation: Cell<u64>,
    networks: RefCell<NetworkMap>,
    profiles: RefCell<BTreeSet<String>>,
    watching: Cell<bool>,
    wifi_enabled: Cell<bool>,
    scanning: Cell<bool>,
    scan_pending: Cell<bool>,
    connecting_to: RefCell<String>,
    last_error: RefCell<String>,
    device_proc: RefCell<AsyncProcess>,
    profile_proc: RefCell<AsyncProcess>,
    quick_scan_proc: RefCell<AsyncProcess>,
    scan_proc: RefCell<AsyncProcess>,
    connect_proc: RefCell<AsyncProcess>,
    disconnect_proc: RefCell<AsyncProcess>,
    forget_proc: RefCell<AsyncProcess>,
    connectivity_proc: RefCell<AsyncProcess>,
    /// Bumped to cancel a pending rescan or connectivity timer.
    rescan_generation: Cell<u64>,
    check_generation: Cell<u64>,
    pub changed: Signal,
    pub messages: Signal<StatusMessage>,
}

impl NetworkService {
    pub fn start(bus: &zbus::Connection) -> Rc<NetworkService> {
        let service = Rc::new_cyclic(|this| NetworkService {
            this: this.clone(),
            bus: bus.clone(),
            status: RefCell::default(),
            device: RefCell::default(),
            connectivity: RefCell::new("unknown".into()),
            initialized: Cell::new(false),
            refresh_generation: Cell::new(0),
            networks: RefCell::default(),
            profiles: RefCell::default(),
            watching: Cell::new(false),
            wifi_enabled: Cell::new(false),
            scanning: Cell::new(false),
            scan_pending: Cell::new(false),
            connecting_to: RefCell::default(),
            last_error: RefCell::default(),
            device_proc: RefCell::default(),
            profile_proc: RefCell::default(),
            quick_scan_proc: RefCell::default(),
            scan_proc: RefCell::default(),
            connect_proc: RefCell::default(),
            disconnect_proc: RefCell::default(),
            forget_proc: RefCell::default(),
            connectivity_proc: RefCell::default(),
            rescan_generation: Cell::new(0),
            check_generation: Cell::new(0),
            changed: Signal::default(),
            messages: Signal::default(),
        });
        runtime::spawn(listen(bus.clone(), Rc::downgrade(&service)));
        service
    }

    pub fn status(&self) -> Ref<'_, NetworkStatus> {
        self.status.borrow()
    }
    pub fn networks(&self) -> Ref<'_, NetworkMap> {
        self.networks.borrow()
    }
    pub fn wifi_available(&self) -> bool {
        self.device.borrow().wifi
    }
    pub fn wifi_enabled(&self) -> bool {
        self.wifi_enabled.get()
    }
    pub fn ethernet_available(&self) -> bool {
        self.device.borrow().ethernet
    }
    pub fn ethernet_connected(&self) -> bool {
        self.device.borrow().ethernet_connected
    }
    pub fn connectivity(&self) -> String {
        self.connectivity.borrow().clone()
    }
    pub fn scanning(&self) -> bool {
        self.scanning.get()
    }
    pub fn connecting_to(&self) -> String {
        self.connecting_to.borrow().clone()
    }
    pub fn last_error(&self) -> String {
        self.last_error.borrow().clone()
    }

    fn emit_changed(&self) {
        self.changed.emit(&());
    }

    /// Starts `argv` in `slot` and hands its output to `then`; false when it could not start.
    fn run(&self, slot: Slot, argv: &[String], merge_stderr: bool, then: impl FnOnce(&NetworkService, String) + 'static) -> bool {
        let args: Vec<&str> = argv.iter().map(String::as_str).collect();
        let run = slot(self).borrow_mut().start(&args, merge_stderr);
        let started = slot(self).borrow().running();
        let weak = self.this.clone();
        runtime::spawn(async move {
            let Some(output) = run.await else { return };
            if let Some(service) = weak.upgrade() {
                then(&service, output);
            }
        });
        started
    }

    fn on_manager_changed(&self, properties: &HashMap<String, OwnedValue>) {
        let watching = self.watching.get();
        if watching && (properties.contains_key("State") || properties.contains_key("ActiveConnections")) {
            self.run(|s| &s.device_proc, &device_argv(), false, |service, output| {
                let next = network_parse_device_status(&output);
                if *service.device.borrow() != next {
                    *service.device.borrow_mut() = next;
                    service.emit_changed();
                }
            });
            self.schedule_rescan(RESCAN_AFTER_STATE);
        }
        if watching && properties.contains_key("Connectivity") {
            self.check_connectivity();
        }
        self.refresh();
    }

    fn refresh(&self) {
        let generation = self.refresh_generation.get() + 1;
        self.refresh_generation.set(generation);
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        runtime::spawn(async move {
            let current = |weak: &Weak<NetworkService>| weak.upgrade().filter(|s| s.refresh_generation.get() == generation);
            let properties = match get_all(&bus, NM_SERVICE, NM_PATH, NM_SERVICE).await {
                Ok(p) => p,
                Err(e) => {
                    ::log::error!("network: {e}");
                    return;
                }
            };
            let Some(service) = current(&weak) else { return };
            let Some(mut next) = service.apply_manager(&properties) else { return };
            let active = value::<OwnedObjectPath>(&properties, "PrimaryConnection");
            let Some(active) = active.filter(|p| p.as_str() != "/") else { return service.finish_refresh(next) };
            drop(service);
            // A failed read abandons the refresh: half a status would announce a disconnect.
            let Ok(active) = get_all(&bus, NM_SERVICE, active.as_str(), NM_ACTIVE_INTERFACE).await else { return };
            let access_point = value::<OwnedObjectPath>(&active, "SpecificObject").filter(|p| p.as_str() != "/");
            if let Some(access_point) = access_point {
                let Ok(ap) = get_all(&bus, NM_SERVICE, access_point.as_str(), NM_ACCESS_POINT_INTERFACE).await else { return };
                next.strength = i32::from(value::<u8>(&ap, "Strength").unwrap_or(0));
                if let Some(ssid) = value::<Vec<u8>>(&ap, "Ssid") {
                    next.ssid = String::from_utf8_lossy(&ssid).into_owned();
                }
            }
            if let Some(service) = current(&weak) {
                service.finish_refresh(next);
            }
        });
    }

    /// Applies the manager properties; returns the status to finish with, `None` when it is already finished.
    fn apply_manager(&self, properties: &HashMap<String, OwnedValue>) -> Option<NetworkStatus> {
        let enabled = value::<bool>(properties, "WirelessEnabled").unwrap_or(false);
        if self.wifi_enabled.replace(enabled) != enabled && self.initialized.get() {
            ::log::info!("network: wifi {}", if enabled { "on" } else { "off" });
            if !enabled {
                self.cancel_scans();
                self.networks.borrow_mut().clear();
            } else if self.watching.get() {
                self.scan();
            }
            self.emit_changed();
        }
        let kind = value::<String>(properties, "PrimaryConnectionType").unwrap_or_default();
        *self.connectivity.borrow_mut() = connectivity_name(value::<u32>(properties, "Connectivity").unwrap_or(0)).into();
        let mut next = NetworkStatus { portal: *self.connectivity.borrow() == "portal", ..Default::default() };
        if kind == "802-3-ethernet" {
            next.kind = NetworkKind::Ethernet;
            self.finish_refresh(next);
            return None;
        }
        if kind != "802-11-wireless" {
            self.finish_refresh(next);
            return None;
        }
        next.kind = NetworkKind::Wifi;
        Some(next)
    }

    fn finish_refresh(&self, next: NetworkStatus) {
        let first = !self.initialized.replace(true);
        if *self.status.borrow() == next {
            return;
        }
        let prev = self.status.replace(next.clone());
        ::log::info!("network: {next:?}");
        if !first {
            self.post(network_changes(&prev, &next));
            if self.watching.get() {
                self.schedule_rescan(RESCAN_AFTER_STATE);
            }
        }
        if next.kind != NetworkKind::None && (first || prev.kind != next.kind || prev.ssid != next.ssid) {
            self.check_connectivity();
        }
        self.emit_changed();
    }

    fn post(&self, messages: Vec<StatusMessage>) {
        for message in messages {
            ::log::info!("network: {}: {}", message.summary, message.body);
            self.messages.emit(&message);
        }
    }

    fn check_connectivity(&self) {
        let argv = to_args(&["nmcli", "networking", "connectivity", "check"]);
        self.run(|s| &s.connectivity_proc, &argv, false, |service, output| {
            let text = output.trim().to_lowercase();
            *service.connectivity.borrow_mut() = if text == "none" || text.is_empty() { "unknown".into() } else { text };
            let portal = *service.connectivity.borrow() == "portal";
            let prev = service.status.borrow().clone();
            if prev.portal == portal {
                return;
            }
            let next = NetworkStatus { portal, ..prev.clone() };
            *service.status.borrow_mut() = next.clone();
            ::log::info!("network: {next:?}");
            service.post(network_changes(&prev, &next));
            service.emit_changed();
        });
    }

    pub fn start_watch(&self) {
        self.watching.set(true);
        self.run(|s| &s.device_proc, &device_argv(), false, |service, output| {
            *service.device.borrow_mut() = network_parse_device_status(&output);
            service.emit_changed();
        });
        let generation = self.check_generation.get() + 1;
        self.check_generation.set(generation);
        let weak = self.this.clone();
        runtime::spawn(async move {
            loop {
                Timer::after(CONNECTIVITY_INTERVAL).await;
                let Some(service) = weak.upgrade().filter(|s| s.check_generation.get() == generation) else { return };
                if service.status.borrow().kind != NetworkKind::None {
                    service.check_connectivity();
                }
            }
        });
        self.scan();
    }

    pub fn stop_watch(&self) {
        self.watching.set(false);
        self.rescan_generation.set(self.rescan_generation.get() + 1);
        self.check_generation.set(self.check_generation.get() + 1);
    }

    pub fn scan(&self) {
        if !self.wifi_enabled.get() {
            return;
        }
        self.last_error.borrow_mut().clear();
        if self.profile_proc.borrow().running() || self.quick_scan_proc.borrow().running() || self.scan_proc.borrow().running() {
            self.scan_pending.set(true);
            return;
        }
        let argv = to_args(&["nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"]);
        let started = self.run(|s| &s.profile_proc, &argv, false, |service, output| service.on_profiles(&output));
        self.scanning.set(started);
        self.emit_changed();
    }

    fn on_profiles(&self, output: &str) {
        *self.profiles.borrow_mut() = network_parse_profiles(output);
        if !self.wifi_enabled.get() {
            self.scanning.set(false);
            self.emit_changed();
            return;
        }
        {
            let mut networks = self.networks.borrow_mut();
            if networks.is_empty() {
                for name in self.profiles.borrow().iter() {
                    networks.insert(name.clone(), profile_stub(name));
                }
            }
        }
        self.run(|s| &s.quick_scan_proc, &list_argv(false), false, |service, output| service.on_quick_scan(&output));
        self.emit_changed();
    }

    fn on_quick_scan(&self, output: &str) {
        if !self.wifi_enabled.get() {
            self.scanning.set(false);
            self.emit_changed();
            return;
        }
        let quick = network_parse_networks(output, &self.profiles.borrow());
        if quick.values().any(|n| n.in_range) && quick != *self.networks.borrow() {
            *self.networks.borrow_mut() = quick;
            self.emit_changed();
        }
        self.run(|s| &s.scan_proc, &list_argv(true), false, |service, output| service.on_scan(&output));
    }

    fn on_scan(&self, output: &str) {
        let parsed = network_parse_networks(output, &self.profiles.borrow());
        if !network_scan_collapses(&self.networks.borrow(), &parsed) {
            *self.networks.borrow_mut() = parsed;
        }
        let has_real = self.networks.borrow().values().any(|n| n.in_range);
        let next = if self.scan_pending.replace(false) {
            self.scanning.set(false);
            RESCAN_PENDING
        } else if self.wifi_enabled.get() && !has_real {
            RESCAN_EMPTY
        } else {
            self.scanning.set(false);
            RESCAN_IDLE
        };
        self.schedule_rescan(next);
        self.emit_changed();
    }

    pub fn connect(&self, ssid: &str, password: &str) {
        if self.connect_proc.borrow().running() {
            return;
        }
        *self.connecting_to.borrow_mut() = ssid.into();
        self.last_error.borrow_mut().clear();
        let mut argv = connect_argv(ssid, password, self.profiles.borrow().contains(ssid));
        self.run(|s| &s.connect_proc, &argv, true, |service, output| service.on_connect(&output));
        for arg in &mut argv {
            // SAFETY: zero bytes keep the string valid UTF-8.
            unsafe { arg.as_mut_vec().fill(0) };
        }
        self.emit_changed();
    }

    fn on_connect(&self, output: &str) {
        let ssid = std::mem::take(&mut *self.connecting_to.borrow_mut());
        if output.contains("successfully activated") || output.contains("Connection successfully") {
            if let Some(network) = self.networks.borrow_mut().get_mut(&ssid) {
                network.connected = true;
                network.existing = true;
            }
        } else if !output.trim().is_empty() {
            let error = connect_error(output);
            *self.last_error.borrow_mut() = error.into();
            ::log::error!("network: connect {ssid} failed: {}", output.trim());
            if error == "Incorrect password" {
                self.forget(&ssid);
            }
        }
        self.schedule_rescan(RESCAN_AFTER_CONNECT);
        self.emit_changed();
    }

    pub fn disconnect(&self, ssid: &str) {
        let argv = to_args(&["nmcli", "connection", "down", "id", ssid]);
        let ssid = ssid.to_string();
        self.run(|s| &s.disconnect_proc, &argv, false, move |service, _| {
            if let Some(network) = service.networks.borrow_mut().get_mut(&ssid) {
                network.connected = false;
            }
            service.schedule_rescan(RESCAN_AFTER_CHANGE);
            service.emit_changed();
        });
    }

    pub fn forget(&self, ssid: &str) {
        let argv = to_args(&["sh", "-c", FORGET_SCRIPT, "--", ssid]);
        let ssid = ssid.to_string();
        self.run(|s| &s.forget_proc, &argv, false, move |service, _| {
            service.profiles.borrow_mut().remove(&ssid);
            if let Some(network) = service.networks.borrow_mut().get_mut(&ssid) {
                network.existing = false;
            }
            service.schedule_rescan(RESCAN_AFTER_CHANGE);
            service.emit_changed();
        });
    }

    pub fn set_wifi_enabled(&self, enabled: bool) {
        let bus = self.bus.clone();
        runtime::spawn(async move {
            let result = async {
                let proxy = zbus::fdo::PropertiesProxy::builder(&bus).destination(NM_SERVICE)?.path(NM_PATH)?.build().await?;
                let interface = zbus::names::InterfaceName::try_from(NM_SERVICE)?;
                proxy.set(interface, "WirelessEnabled", zbus::zvariant::Value::from(enabled)).await?;
                Ok::<(), zbus::Error>(())
            };
            if let Err(e) = result.await {
                ::log::error!("network: cannot set WirelessEnabled: {e}");
            }
        });
    }

    pub fn clear_error(&self) {
        self.last_error.borrow_mut().clear();
        self.emit_changed();
    }

    pub fn open_portal(&self) {
        spawn_detached(&network_portal_command());
    }

    fn schedule_rescan(&self, delay: Duration) {
        if !self.watching.get() {
            return;
        }
        let generation = self.rescan_generation.get() + 1;
        self.rescan_generation.set(generation);
        let weak = self.this.clone();
        runtime::spawn(async move {
            Timer::after(delay).await;
            if let Some(service) = weak.upgrade().filter(|s| s.rescan_generation.get() == generation) {
                service.scan();
            }
        });
    }

    fn cancel_scans(&self) {
        self.profile_proc.borrow_mut().cancel();
        self.quick_scan_proc.borrow_mut().cancel();
        self.scan_proc.borrow_mut().cancel();
        self.scanning.set(false);
        self.scan_pending.set(false);
    }
}

/// Subscribes to the manager's `PropertiesChanged` before the first read, so no change falls between.
async fn listen(bus: zbus::Connection, service: Weak<NetworkService>) {
    let rule = zbus::MatchRule::builder()
        .msg_type(MessageType::Signal)
        .sender(NM_SERVICE)
        .and_then(|b| b.path(NM_PATH))
        .and_then(|b| b.interface("org.freedesktop.DBus.Properties"))
        .and_then(|b| b.member("PropertiesChanged"))
        .map(|b| b.build());
    let stream = match rule {
        Ok(rule) => zbus::MessageStream::for_match_rule(rule, &bus, None).await,
        Err(e) => Err(e),
    };
    let mut stream = match stream {
        Ok(stream) => stream,
        Err(e) => {
            ::log::error!("network: cannot subscribe: {e}");
            return;
        }
    };
    match service.upgrade() {
        Some(service) => service.refresh(),
        None => return,
    }
    while let Some(message) = stream.next().await {
        let Some(service) = service.upgrade() else { return };
        let Ok(message) = message else { continue };
        let Ok((interface, properties, _)) = message.body().deserialize::<(String, HashMap<String, OwnedValue>, Vec<String>)>() else { continue };
        if interface == NM_SERVICE {
            service.on_manager_changed(&properties);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set(items: &[&str]) -> BTreeSet<String> {
        items.iter().map(|s| s.to_string()).collect()
    }

    fn info(connected: bool, existing: bool, in_range: bool) -> NetworkInfo {
        NetworkInfo { connected, existing, in_range, ..Default::default() }
    }

    fn map(entries: &[(&str, NetworkInfo)]) -> NetworkMap {
        entries.iter().map(|(k, v)| (k.to_string(), v.clone())).collect()
    }

    #[test]
    fn network_parse() {
        let profiles = network_parse_profiles("home:802-11-wireless\nWired connection 1:802-3-ethernet\nlab\\:5G:802-11-wireless\n");
        assert_eq!(profiles, set(&["home", "lab:5G"]), "profiles keep Wi-Fi only and unescape colons");
        let networks = network_parse_networks("home:WPA2 WPA3:82:*\ncafe::40: \nlab\\:5G:WPA2:60: \ncafe:WPA2:90: \n:WPA2:30: \n", &profiles);
        assert_eq!(networks.len(), 3, "hidden SSIDs drop and duplicates merge");
        assert!(networks["home"].connected && networks["home"].existing && networks["home"].security == "WPA2/WPA3", "connected saved network");
        assert!(networks["cafe"].security == "--" && networks["cafe"].signal == 40 && !networks["cafe"].existing, "first duplicate wins; open network shows --");
        assert!(networks["lab:5G"].in_range && networks["lab:5G"].existing, "escaped SSID matches its profile");
        let out_of_range = network_parse_networks("", &set(&["office"]));
        assert!(out_of_range.len() == 1 && !out_of_range["office"].in_range, "profiles out of range stay listed");
        assert_eq!(network_visible_count(&out_of_range), 0, "out-of-range profiles are not visible");
        assert!(network_parse_wifi_device("wlan0:wifi:connected\nlo:loopback:unmanaged\n"), "wifi device found");
        assert!(!network_parse_wifi_device("wlan0:wifi:unmanaged\neth0:ethernet:connected\n"), "unmanaged wifi ignored");
        let self_only = network_parse_networks("home:WPA2:80:*\n", &BTreeSet::new());
        assert!(network_scan_collapses(&networks, &self_only), "a self-only rescan is discarded");
        assert!(!network_scan_collapses(&self_only, &networks), "a fuller rescan is kept");
    }

    #[test]
    fn network_more() {
        let none = BTreeSet::new();
        let nets = network_parse_networks("HomeNet:WPA2:80:*\nGuest:WPA2 WPA3:40:\nOpen:--:20:\n", &none);
        assert!(nets.len() == 3 && nets["HomeNet"].connected && nets["HomeNet"].signal == 80 && nets["HomeNet"].security == "WPA2", "scan lines parse");
        assert!(nets["Guest"].security == "WPA2/WPA3" && !nets["Guest"].connected && nets["Open"].security == "--", "security names");
        assert!(nets.values().all(|n| n.in_range), "scanned networks are in range");
        assert!(network_parse_networks("Dup:WPA2:30:\nDup:WPA2:90:*\n", &none)["Dup"].connected, "a connected duplicate marks the network");
        assert!(network_parse_networks("My\\:Network:WPA2:50:\n", &none).contains_key("My:Network"), "escaped colon in an SSID");
        let saved = network_parse_networks("", &set(&["SavedNet"]));
        assert!(saved.len() == 1 && saved["SavedNet"].existing && !saved["SavedNet"].in_range && !saved["SavedNet"].connected, "saved profile out of range");
        assert!(network_parse_networks("garbage:noenoughfields\n", &none).is_empty(), "short lines are dropped");

        let both = network_parse_device_status("wlan0:wifi:connected:MyWifi\neth0:ethernet:connected:Wired connection 1\nlo:loopback:unmanaged:--\n");
        assert!(both.wifi && both.ethernet && both.ethernet_connected && both.ethernet_name == "Wired connection 1", "wifi and ethernet device status");
        let connecting = network_parse_device_status("eth0:ethernet:connecting:--\n");
        assert!(connecting.ethernet && !connecting.ethernet_connected, "ethernet still connecting");
        let unavailable = network_parse_device_status("wlan0:wifi:unavailable:--\n");
        assert!(unavailable.wifi && !unavailable.ethernet, "wifi unavailable still counts as present");
        assert_eq!(network_parse_device_status("eth0:ethernet:connected:Lab\\:Wired\n").ethernet_name, "Lab:Wired", "escaped colon in a connection name");

        assert!(
            connectivity_name(2) == "portal" && connectivity_name(4) == "full" && connectivity_name(3) == "limited" && connectivity_name(1) == "none" && connectivity_name(0) == "unknown",
            "connectivity names"
        );

        let visible = map(&[("A", info(true, false, false)), ("B", info(false, true, true)), ("C", info(false, false, false)), ("D", info(false, true, false))]);
        assert_eq!(network_visible_count(&visible), 3, "stubs are not visible");

        let (a, b, c) = (info(true, false, true), info(false, true, true), info(false, false, true));
        let populated = map(&[("A", a.clone()), ("B", b.clone()), ("C", c.clone())]);
        assert!(network_scan_collapses(&populated, &map(&[("A", a.clone())])), "self-only scan collapses");
        assert!(!network_scan_collapses(&populated, &map(&[("Z", c)])), "neighbour-only scan is kept");
        assert!(!network_scan_collapses(&populated, &map(&[("A", a.clone()), ("B", b)])), "fuller scan is kept");
        assert!(!network_scan_collapses(&map(&[("Only", a.clone())]), &map(&[("A", a)])), "a sparse list never collapses");
    }

    fn msg(summary: &str, body: &str) -> StatusMessage {
        StatusMessage { summary: summary.into(), body: body.into(), click_command: String::new() }
    }

    #[test]
    fn network_status_changes() {
        let wifi = |strength, ssid: &str, portal| NetworkStatus { kind: NetworkKind::Wifi, strength, ssid: ssid.into(), portal };
        let none = NetworkStatus::default();
        let home = wifi(80, "home", false);
        let cafe = wifi(60, "cafe", false);
        let portal = wifi(60, "cafe", true);
        let wired = NetworkStatus { kind: NetworkKind::Ethernet, ..Default::default() };
        assert!(network_changes(&home, &home).is_empty(), "unchanged network sends nothing");
        assert!(network_changes(&home, &wifi(30, "home", false)).is_empty(), "signal change sends nothing");
        assert_eq!(network_changes(&none, &home), vec![msg("Connected", "Connected to home")], "Wi-Fi connect");
        assert_eq!(network_changes(&home, &none), vec![msg("Disconnected", "Disconnected from home")], "Wi-Fi disconnect");
        assert_eq!(network_changes(&home, &cafe), vec![msg("Connected", "Connected to cafe")], "Wi-Fi switch");
        assert_eq!(network_changes(&none, &wired), vec![msg("Connected", "Connected via Ethernet")], "Ethernet up");
        assert_eq!(network_changes(&wired, &none), vec![msg("Disconnected", "Ethernet disconnected")], "Ethernet down");
        assert_eq!(
            network_changes(&cafe, &portal),
            vec![StatusMessage { summary: "Captive Portal".into(), body: "Sign in required for cafe".into(), click_command: network_portal_command() }],
            "captive portal"
        );
        let command = network_portal_command();
        let at = |s: &str| command.find(s).unwrap();
        assert!(
            at("http://httpforever.com") < at("http://captive.apple.com/hotspot-detect.html") && at("http://captive.apple.com/hotspot-detect.html") < at("http://example.com") && at("http://example.com") < at("http://neverssl.net"),
            "portal URLs are tried in order"
        );
        assert!(!command.contains("curl -f") && !command.contains(" -f "), "any HTTP reply counts as reachable");
        assert!(command.ends_with("astralia-open browser http://httpforever.com"), "the primary URL opens when every probe fails");
    }
}
