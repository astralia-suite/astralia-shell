use std::cell::{Cell, Ref, RefCell};
use std::collections::BTreeSet;
use std::rc::{Rc, Weak};

use futures_lite::StreamExt;
use zbus::message::Type as MessageType;
use zbus::zvariant::{ObjectPath, OwnedObjectPath};

use crate::base::dbus::{Coalesce, Properties, value};
use crate::base::signal::Signal;
use crate::service::network::StatusMessage;
use crate::service::runtime;

const BLUEZ: &str = "org.bluez";
const ADAPTER_INTERFACE: &str = "org.bluez.Adapter1";
const DEVICE_INTERFACE: &str = "org.bluez.Device1";
const BATTERY_INTERFACE: &str = "org.bluez.Battery1";
const AGENT_MANAGER_INTERFACE: &str = "org.bluez.AgentManager1";
const AGENT_PATH: &str = "/org/astralia_shell/BluetoothAgent";
const AGENT_CAPABILITY: &str = "KeyboardDisplay";
const ALREADY_EXISTS_ERROR: &str = "org.bluez.Error.AlreadyExists";
const PASSKEY_DIGITS: usize = 6;
const PIN_MAX_CHARS: usize = 16;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BluetoothDeviceKind {
    #[default]
    Unknown,
    Headset,
    Headphones,
    Speaker,
    Mouse,
    Keyboard,
    Phone,
    Computer,
    Gamepad,
    Watch,
    Tv,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BluetoothStatus {
    pub present: bool,
    pub powered: bool,
    pub connected: bool,
    pub device: String,
    pub scanning: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BluetoothDevice {
    pub path: String,
    pub name: String,
    pub paired: bool,
    pub trusted: bool,
    pub connected: bool,
    pub connecting: bool,
    pub battery: i32,
    pub address: String,
    pub kind: BluetoothDeviceKind,
}

impl Default for BluetoothDevice {
    fn default() -> Self {
        BluetoothDevice {
            path: String::new(),
            name: String::new(),
            paired: false,
            trusted: false,
            connected: false,
            connecting: false,
            battery: -1,
            address: String::new(),
            kind: BluetoothDeviceKind::Unknown,
        }
    }
}

pub type BluetoothObjects = zbus::fdo::ManagedObjects;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BluetoothSnapshot {
    pub status: BluetoothStatus,
    pub devices: Vec<BluetoothDevice>,
    pub adapter_path: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AgentRequestKind {
    Confirm,
    DisplayPasskey,
    DisplayPin,
    InputPasskey,
    InputPin,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BluetoothAgentRequest {
    pub kind: AgentRequestKind,
    pub path: String,
    pub name: String,
    pub code: String,
    pub entered: u32,
}

/// Only the device being paired, or for services a known one, may use the agent.
pub fn bluetooth_agent_allows(request_path: &str, pairing_path: &str, known: bool) -> bool {
    known || (!pairing_path.is_empty() && request_path == pairing_path)
}

pub fn bluetooth_passkey_text(passkey: u32) -> String {
    format!("{:06}", passkey % 1_000_000)
}

pub fn bluetooth_classify_icon(name: &str) -> BluetoothDeviceKind {
    match name {
        "audio-headset" => BluetoothDeviceKind::Headset,
        "audio-headphones" => BluetoothDeviceKind::Headphones,
        "audio-card" | "audio-speakers" => BluetoothDeviceKind::Speaker,
        "input-mouse" => BluetoothDeviceKind::Mouse,
        "input-keyboard" => BluetoothDeviceKind::Keyboard,
        "input-gaming" => BluetoothDeviceKind::Gamepad,
        "phone" => BluetoothDeviceKind::Phone,
        "computer" => BluetoothDeviceKind::Computer,
        "video-display" => BluetoothDeviceKind::Tv,
        _ => BluetoothDeviceKind::Unknown,
    }
}

pub fn bluetooth_classify_class(class_of_device: u32) -> BluetoothDeviceKind {
    let major = (class_of_device >> 8) & 0x1F;
    let minor = (class_of_device >> 2) & 0x3F;
    match major {
        0x01 => BluetoothDeviceKind::Computer,
        0x02 => BluetoothDeviceKind::Phone,
        0x04 => match minor {
            0x01 | 0x02 => BluetoothDeviceKind::Headset,
            0x05 | 0x07 => BluetoothDeviceKind::Speaker,
            0x0A | 0x0B => BluetoothDeviceKind::Tv,
            _ => BluetoothDeviceKind::Headphones,
        },
        0x05 => match minor & 0x0F {
            0x01 => BluetoothDeviceKind::Keyboard,
            0x02 => BluetoothDeviceKind::Mouse,
            _ => BluetoothDeviceKind::Unknown,
        },
        0x07 => BluetoothDeviceKind::Watch,
        0x08 => BluetoothDeviceKind::Gamepad,
        _ => BluetoothDeviceKind::Unknown,
    }
}

pub fn bluetooth_is_connected(d: &BluetoothDevice) -> bool {
    d.connected
}

pub fn bluetooth_is_paired(d: &BluetoothDevice) -> bool {
    !d.connected && (d.paired || d.trusted)
}

pub fn bluetooth_is_nearby(d: &BluetoothDevice) -> bool {
    !d.connected && !d.paired && !d.trusted
}

fn flag(p: &Properties, name: &str) -> bool {
    value::<bool>(p, name).unwrap_or(false)
}

/// The first adapter and every device, in object path order.
pub fn bluetooth_parse(objects: &BluetoothObjects, busy: &BTreeSet<String>) -> BluetoothSnapshot {
    let mut snapshot = BluetoothSnapshot::default();
    let mut paths: Vec<&OwnedObjectPath> = objects.keys().collect();
    paths.sort_by(|a, b| a.as_str().cmp(b.as_str()));
    for path in paths {
        let interfaces = &objects[path];
        let find = |name: &str| interfaces.iter().find(|(k, _)| k.as_str() == name).map(|(_, v)| v);
        if let Some(adapter) = find(ADAPTER_INTERFACE).filter(|_| !snapshot.status.present) {
            snapshot.status.present = true;
            snapshot.status.powered = flag(adapter, "Powered");
            snapshot.status.scanning = flag(adapter, "Discovering");
            snapshot.adapter_path = path.to_string();
        }
        let Some(props) = find(DEVICE_INTERFACE) else { continue };
        let text = |name| value::<String>(props, name).unwrap_or_default();
        let address = text("Address");
        let name = [text("Alias"), text("Name")].into_iter().find(|n| !n.is_empty());
        let name = name.unwrap_or_else(|| if address.is_empty() { "Unknown Device".into() } else { address.clone() });
        let connected = flag(props, "Connected");
        let mut kind = bluetooth_classify_icon(&text("Icon"));
        if kind == BluetoothDeviceKind::Unknown {
            kind = bluetooth_classify_class(value::<u32>(props, "Class").unwrap_or(0));
        }
        let battery = find(BATTERY_INTERFACE).filter(|_| connected).and_then(|b| value::<u8>(b, "Percentage")).map_or(-1, i32::from);
        let device = BluetoothDevice {
            path: path.to_string(),
            name,
            paired: flag(props, "Paired"),
            trusted: flag(props, "Trusted"),
            connected,
            connecting: !connected && busy.contains(path.as_str()),
            battery,
            address,
            kind,
        };
        if device.connected && !snapshot.status.connected {
            snapshot.status.connected = true;
            snapshot.status.device = device.name.clone();
        }
        snapshot.devices.push(device);
    }
    snapshot
}

pub fn bluetooth_changes(prev: &[BluetoothDevice], next: &[BluetoothDevice]) -> Vec<StatusMessage> {
    let connected = |devices: &[BluetoothDevice], path: &str| devices.iter().any(|d| d.path == path && d.connected);
    let dropped = prev.iter().filter(|d| d.connected && !connected(next, &d.path)).map(|d| StatusMessage::new("Disconnected", format!("Disconnected from {}", d.name)));
    let joined = next.iter().filter(|d| d.connected && !connected(prev, &d.path)).map(|d| StatusMessage::new("Connected", format!("Connected to {}", d.name)));
    dropped.chain(joined).collect()
}

pub mod rfkill {
    use std::io::Write;

    const RFKILL_TYPE_BLUETOOTH: u8 = 2;
    const RFKILL_OP_CHANGE_ALL: u8 = 3;

    /// `fscanf("%u")`: the first unsigned number.
    pub fn read_sysfs_uint(path: &str) -> Option<u32> {
        std::fs::read_to_string(path).ok()?.split_whitespace().next()?.parse().ok()
    }

    /// `fscanf("%31s")`: the first word, at most 31 bytes.
    pub fn read_sysfs_string(path: &str) -> Option<String> {
        let text = std::fs::read_to_string(path).ok()?;
        let word = text.split_whitespace().next()?;
        Some(word.chars().take(31).collect())
    }

    struct Entry {
        soft: bool,
        hard: bool,
    }

    fn bluetooth_entries() -> Vec<Entry> {
        let Ok(dir) = std::fs::read_dir("/sys/class/rfkill") else { return Vec::new() };
        dir.flatten()
            .filter(|e| e.file_name().to_string_lossy().starts_with("rfkill"))
            .map(|e| e.path().to_string_lossy().into_owned())
            .filter(|base| read_sysfs_uint(&format!("{base}/index")).is_some() && read_sysfs_string(&format!("{base}/type")).as_deref() == Some("bluetooth"))
            .map(|base| Entry {
                soft: read_sysfs_uint(&format!("{base}/soft")).unwrap_or(0) != 0,
                hard: read_sysfs_uint(&format!("{base}/hard")).unwrap_or(0) != 0,
            })
            .collect()
    }

    pub fn bluetooth_hard_blocked() -> bool {
        bluetooth_entries().iter().any(|e| e.hard)
    }

    /// Sets the soft block on every Bluetooth radio; false when a radio is hard blocked or the write fails.
    pub fn set_bluetooth_soft_blocked(blocked: bool) -> bool {
        let entries = bluetooth_entries();
        if entries.is_empty() || entries.iter().any(|e| e.hard) {
            return false;
        }
        if entries.iter().all(|e| e.soft == blocked) {
            return true;
        }
        // struct rfkill_event { u32 idx; u8 type; u8 op; u8 soft; u8 hard; }
        let event = [0, 0, 0, 0, RFKILL_TYPE_BLUETOOTH, RFKILL_OP_CHANGE_ALL, u8::from(blocked), 0];
        let written = std::fs::OpenOptions::new().write(true).open("/dev/rfkill").and_then(|mut f| f.write_all(&event));
        if let Err(e) = written {
            ::log::error!("rfkill: write /dev/rfkill: {e}");
            return false;
        }
        true
    }
}

#[derive(Debug, zbus::DBusError)]
#[zbus(prefix = "org.bluez.Error")]
enum AgentError {
    #[zbus(error)]
    ZBus(zbus::Error),
    Rejected(String),
}

/// What BlueZ asks the agent; the loop thread decides and answers through `reply`.
enum Ask {
    Authorization,
    Service,
    Display(AgentRequestKind, String, u32),
    Request(AgentRequestKind, String),
}

type AgentReply = async_channel::Sender<Result<String, String>>;

/// Runs after a device call with whether it succeeded.
type DeviceDone = Box<dyn FnOnce(&BluetoothService, bool)>;

enum AgentCall {
    Drop,
    Ask { device: String, ask: Ask, reply: AgentReply },
}

/// The exported `org.bluez.Agent1`; it runs on zbus's thread and forwards each call to the loop thread.
struct Agent {
    calls: async_channel::Sender<AgentCall>,
}

impl Agent {
    async fn ask(&self, device: OwnedObjectPath, ask: Ask) -> Result<String, AgentError> {
        let (reply, answer) = async_channel::bounded(1);
        let call = AgentCall::Ask { device: device.to_string(), ask, reply };
        if self.calls.send(call).await.is_err() {
            return Err(AgentError::Rejected("Rejected".into()));
        }
        // A reply dropped unanswered (BlueZ cancelled, or the service went away) is a rejection.
        answer.recv().await.unwrap_or_else(|_| Err("Rejected".into())).map_err(AgentError::Rejected)
    }

    async fn drop_request(&self) {
        let _ = self.calls.send(AgentCall::Drop).await;
    }
}

#[zbus::interface(name = "org.bluez.Agent1")]
impl Agent {
    async fn release(&self) {
        self.drop_request().await;
    }

    async fn cancel(&self) {
        self.drop_request().await;
    }

    async fn request_authorization(&self, device: OwnedObjectPath) -> Result<(), AgentError> {
        self.ask(device, Ask::Authorization).await.map(drop)
    }

    async fn authorize_service(&self, device: OwnedObjectPath, _uuid: String) -> Result<(), AgentError> {
        self.ask(device, Ask::Service).await.map(drop)
    }

    async fn display_passkey(&self, device: OwnedObjectPath, passkey: u32, entered: u16) -> Result<(), AgentError> {
        self.ask(device, Ask::Display(AgentRequestKind::DisplayPasskey, bluetooth_passkey_text(passkey), u32::from(entered))).await.map(drop)
    }

    async fn display_pin_code(&self, device: OwnedObjectPath, pin: String) -> Result<(), AgentError> {
        self.ask(device, Ask::Display(AgentRequestKind::DisplayPin, pin, 0)).await.map(drop)
    }

    async fn request_confirmation(&self, device: OwnedObjectPath, passkey: u32) -> Result<(), AgentError> {
        self.ask(device, Ask::Request(AgentRequestKind::Confirm, bluetooth_passkey_text(passkey))).await.map(drop)
    }

    async fn request_passkey(&self, device: OwnedObjectPath) -> Result<u32, AgentError> {
        let text = self.ask(device, Ask::Request(AgentRequestKind::InputPasskey, String::new())).await?;
        text.parse().map_err(|_| AgentError::Rejected("Rejected".into()))
    }

    async fn request_pin_code(&self, device: OwnedObjectPath) -> Result<String, AgentError> {
        self.ask(device, Ask::Request(AgentRequestKind::InputPin, String::new())).await
    }
}

pub struct BluetoothService {
    this: Weak<BluetoothService>,
    bus: zbus::Connection,
    status: RefCell<BluetoothStatus>,
    devices: RefCell<Vec<BluetoothDevice>>,
    busy: RefCell<BTreeSet<String>>,
    adapter_path: RefCell<String>,
    initialized: Cell<bool>,
    refresh: Coalesce,
    agent_exported: Cell<bool>,
    agent_registered: Cell<bool>,
    pairing_path: RefCell<String>,
    agent_request: RefCell<Option<BluetoothAgentRequest>>,
    agent_reply: RefCell<Option<AgentReply>>,
    pub changed: Signal,
    pub messages: Signal<StatusMessage>,
}

impl BluetoothService {
    pub fn start(bus: &zbus::Connection) -> Rc<BluetoothService> {
        let service = Rc::new_cyclic(|this| BluetoothService {
            this: this.clone(),
            bus: bus.clone(),
            status: RefCell::default(),
            devices: RefCell::default(),
            busy: RefCell::default(),
            adapter_path: RefCell::default(),
            initialized: Cell::new(false),
            refresh: Coalesce::default(),
            agent_exported: Cell::new(false),
            agent_registered: Cell::new(false),
            pairing_path: RefCell::default(),
            agent_request: RefCell::new(None),
            agent_reply: RefCell::new(None),
            changed: Signal::default(),
            messages: Signal::default(),
        });
        runtime::spawn(listen(bus.clone(), Rc::downgrade(&service)));
        runtime::spawn(watch_owner(bus.clone(), Rc::downgrade(&service)));
        service
    }

    pub fn status(&self) -> Ref<'_, BluetoothStatus> {
        self.status.borrow()
    }

    pub fn devices(&self) -> Ref<'_, Vec<BluetoothDevice>> {
        self.devices.borrow()
    }

    pub fn agent_request(&self) -> Option<BluetoothAgentRequest> {
        self.agent_request.borrow().clone()
    }

    fn emit_changed(&self) {
        self.changed.emit(&());
    }

    fn request_refresh(&self) {
        let weak = self.this.clone();
        let bus = self.bus.clone();
        self.refresh.request(move || {
            let (weak, bus) = (weak.clone(), bus.clone());
            Box::pin(async move {
                let objects = async {
                    zbus::fdo::ObjectManagerProxy::builder(&bus).destination(BLUEZ)?.path("/")?.build().await?.get_managed_objects().await.map_err(zbus::Error::from)
                };
                match objects.await {
                    Ok(objects) => {
                        if let Some(service) = weak.upgrade() {
                            service.apply(&objects);
                        }
                    }
                    Err(e) => ::log::error!("bluetooth: GetManagedObjects failed: {e}"),
                }
            })
        });
    }

    fn apply(&self, objects: &BluetoothObjects) {
        let next = bluetooth_parse(objects, &self.busy.borrow());
        *self.adapter_path.borrow_mut() = next.adapter_path.clone();
        if next.status.present {
            self.register_agent();
        }
        let first = !self.initialized.replace(true);
        if *self.status.borrow() == next.status && *self.devices.borrow() == next.devices {
            return;
        }
        if *self.status.borrow() != next.status {
            ::log::info!("bluetooth: {:?}", next.status);
        }
        *self.status.borrow_mut() = next.status;
        let prev = self.devices.replace(next.devices);
        if !first {
            let messages = bluetooth_changes(&prev, &self.devices.borrow());
            for message in messages {
                ::log::info!("bluetooth: {}: {}", message.summary, message.body);
                self.messages.emit(&message);
            }
        }
        self.emit_changed();
    }

    async fn call(bus: &zbus::Connection, path: &str, interface: &str, method: &str, body: &(impl zbus::export::serde::Serialize + zbus::zvariant::DynamicType)) -> zbus::Result<()> {
        let proxy = zbus::Proxy::new(bus, BLUEZ, path.to_owned(), interface.to_owned()).await?;
        proxy.call_method(method, body).await.map(drop)
    }

    fn call_device(&self, path: &str, method: &'static str, busy: bool, done: Option<DeviceDone>) {
        if path.is_empty() {
            return;
        }
        if busy {
            self.busy.borrow_mut().insert(path.into());
            for known in self.devices.borrow_mut().iter_mut().filter(|d| d.path == path && !d.connected) {
                known.connecting = true;
            }
            self.emit_changed();
        }
        let (weak, bus, path) = (self.this.clone(), self.bus.clone(), path.to_string());
        runtime::spawn(async move {
            let result = BluetoothService::call(&bus, &path, DEVICE_INTERFACE, method, &()).await;
            if let Err(e) = &result {
                ::log::error!("bluetooth: {method} failed: {e}");
            }
            let Some(service) = weak.upgrade() else { return };
            if let Some(done) = done {
                done(&service, result.is_ok());
            }
            service.busy.borrow_mut().remove(&path);
            service.request_refresh();
        });
    }

    fn call_adapter(&self, method: &'static str) {
        let adapter = self.adapter_path.borrow().clone();
        if adapter.is_empty() {
            return;
        }
        let bus = self.bus.clone();
        runtime::spawn(async move {
            if let Err(e) = BluetoothService::call(&bus, &adapter, ADAPTER_INTERFACE, method, &()).await {
                ::log::error!("bluetooth: {method} failed: {e}");
            }
        });
    }

    fn set_property(&self, path: String, interface: &'static str, name: &'static str, value: bool) {
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        runtime::spawn(async move {
            let result = async {
                let proxy = zbus::fdo::PropertiesProxy::builder(&bus).destination(BLUEZ)?.path(path)?.build().await?;
                proxy.set(zbus::names::InterfaceName::try_from(interface)?, name, zbus::zvariant::Value::from(value)).await?;
                Ok::<(), zbus::Error>(())
            };
            if let Err(e) = result.await {
                ::log::error!("bluetooth: cannot set {name}: {e}");
            }
            if let Some(service) = weak.upgrade() {
                service.request_refresh();
            }
        });
    }

    pub fn set_powered(&self, powered: bool) {
        let adapter = self.adapter_path.borrow().clone();
        if adapter.is_empty() {
            return;
        }
        if powered {
            if rfkill::bluetooth_hard_blocked() {
                ::log::error!("bluetooth: set_powered: rfkill hard block is active");
                return;
            }
            if !rfkill::set_bluetooth_soft_blocked(false) {
                ::log::error!("bluetooth: rfkill unblock failed, trying Powered anyway");
            }
        }
        self.set_property(adapter, ADAPTER_INTERFACE, "Powered", powered);
    }

    pub fn start_discovery(&self) {
        if self.status.borrow().powered {
            self.call_adapter("StartDiscovery");
        }
    }

    pub fn stop_discovery(&self) {
        if self.status.borrow().powered {
            self.call_adapter("StopDiscovery");
        }
    }

    pub fn connect(&self, path: &str) {
        self.call_device(path, "Connect", true, None);
    }

    pub fn disconnect(&self, path: &str) {
        self.call_device(path, "Disconnect", false, None);
    }

    pub fn pair(&self, path: &str) {
        if path.is_empty() || !self.pairing_path.borrow().is_empty() {
            return;
        }
        *self.pairing_path.borrow_mut() = path.into();
        let target = path.to_string();
        self.call_device(
            path,
            "Pair",
            true,
            Some(Box::new(move |service, ok| {
                service.pairing_path.borrow_mut().clear();
                service.agent_reply.borrow_mut().take();
                let had_request = service.agent_request.borrow_mut().take().is_some();
                if ok {
                    service.set_property(target, DEVICE_INTERFACE, "Trusted", true);
                }
                if had_request {
                    service.emit_changed();
                }
            })),
        );
    }

    pub fn forget(&self, path: &str) {
        let adapter = self.adapter_path.borrow().clone();
        if adapter.is_empty() {
            return;
        }
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        let Ok(device) = OwnedObjectPath::try_from(path) else { return };
        runtime::spawn(async move {
            if let Err(e) = BluetoothService::call(&bus, &adapter, ADAPTER_INTERFACE, "RemoveDevice", &(device,)).await {
                ::log::error!("bluetooth: RemoveDevice failed: {e}");
            }
            if let Some(service) = weak.upgrade() {
                service.request_refresh();
            }
        });
    }

    pub fn cancel_pairing(&self) {
        if self.agent_request.borrow().is_some() {
            self.finish_agent(false, "");
        }
        let pairing = self.pairing_path.borrow().clone();
        if !pairing.is_empty() {
            self.call_device(&pairing, "CancelPairing", false, None);
        }
    }

    pub fn agent_answer(&self, accept: bool) {
        let Some(kind) = self.agent_request.borrow().as_ref().map(|r| r.kind) else { return };
        if !accept {
            self.cancel_pairing();
        } else if kind == AgentRequestKind::Confirm {
            self.finish_agent(true, "");
        }
    }

    pub fn agent_input(&self, text: &str) {
        let Some(kind) = self.agent_request.borrow().as_ref().map(|r| r.kind) else { return };
        let digits = !text.is_empty() && text.len() <= PASSKEY_DIGITS && text.bytes().all(|c| c.is_ascii_digit());
        if (kind == AgentRequestKind::InputPasskey && digits) || (kind == AgentRequestKind::InputPin && !text.is_empty() && text.len() <= PIN_MAX_CHARS) {
            self.finish_agent(true, text);
        }
    }

    /// Shows `request` as if the agent had received it, with nobody waiting for the answer.
    #[cfg(test)]
    pub fn hold_request(&self, request: BluetoothAgentRequest) {
        self.hold(request, None);
    }

    fn hold(&self, request: BluetoothAgentRequest, reply: Option<AgentReply>) {
        if self.agent_reply.borrow().is_some() {
            self.finish_agent(false, "");
        }
        *self.agent_request.borrow_mut() = Some(request);
        *self.agent_reply.borrow_mut() = reply;
        self.emit_changed();
    }

    fn finish_agent(&self, accept: bool, text: &str) {
        let reply = self.agent_reply.borrow_mut().take();
        self.agent_request.borrow_mut().take();
        if let Some(reply) = reply {
            let _ = reply.try_send(if accept { Ok(text.to_string()) } else { Err("Rejected".into()) });
        }
        self.emit_changed();
    }

    fn device_name(&self, path: &str) -> String {
        self.devices.borrow().iter().find(|d| d.path == path).map_or_else(|| path.to_string(), |d| d.name.clone())
    }

    fn device_known(&self, path: &str) -> bool {
        self.devices.borrow().iter().any(|d| d.path == path && (d.paired || d.trusted))
    }

    fn on_agent(&self, call: AgentCall) {
        let AgentCall::Ask { device, ask, reply } = call else {
            self.agent_reply.borrow_mut().take();
            if self.agent_request.borrow_mut().take().is_some() {
                self.emit_changed();
            }
            return;
        };
        let pairing = self.pairing_path.borrow().clone();
        let allowed = bluetooth_agent_allows(&device, &pairing, false);
        let reject = |why: &str| {
            let _ = reply.try_send(Err(why.into()));
        };
        match ask {
            Ask::Authorization if !allowed => reject("Not pairing this device"),
            Ask::Service if !bluetooth_agent_allows(&device, &pairing, self.device_known(&device)) => reject("Unknown device"),
            Ask::Authorization | Ask::Service => {
                let _ = reply.try_send(Ok(String::new()));
            }
            Ask::Display(..) | Ask::Request(..) if !allowed => reject("Not pairing this device"),
            Ask::Display(kind, code, entered) => {
                let name = self.device_name(&device);
                self.hold(BluetoothAgentRequest { kind, path: device, name, code, entered }, None);
                let _ = reply.try_send(Ok(String::new()));
            }
            Ask::Request(kind, code) => {
                let name = self.device_name(&device);
                self.hold(BluetoothAgentRequest { kind, path: device, name, code, entered: 0 }, Some(reply));
            }
        }
    }

    fn register_agent(&self) {
        if !self.agent_exported.get() || self.agent_registered.replace(true) {
            return;
        }
        let bus = self.bus.clone();
        runtime::spawn(async move {
            let Ok(agent) = ObjectPath::try_from(AGENT_PATH) else { return };
            match BluetoothService::call(&bus, "/org/bluez", AGENT_MANAGER_INTERFACE, "RegisterAgent", &(&agent, AGENT_CAPABILITY)).await {
                Err(zbus::Error::MethodError(name, _, _)) if name.as_str() == ALREADY_EXISTS_ERROR => {}
                Err(e) => {
                    ::log::error!("bluetooth: RegisterAgent failed: {e}");
                    return;
                }
                Ok(()) => {}
            }
            if let Err(e) = BluetoothService::call(&bus, "/org/bluez", AGENT_MANAGER_INTERFACE, "RequestDefaultAgent", &(&agent,)).await {
                ::log::error!("bluetooth: RequestDefaultAgent failed: {e}");
            }
        });
    }
}

/// Exports the agent, subscribes to BlueZ's signals, then reads the first state.
async fn listen(bus: zbus::Connection, service: Weak<BluetoothService>) {
    let (calls, incoming) = async_channel::unbounded();
    match bus.object_server().at(AGENT_PATH, Agent { calls }).await {
        Ok(_) => {
            if let Some(service) = service.upgrade() {
                service.agent_exported.set(true);
            }
            let weak = service.clone();
            runtime::spawn(async move {
                while let Ok(call) = incoming.recv().await {
                    let Some(service) = weak.upgrade() else { return };
                    service.on_agent(call);
                }
            });
        }
        Err(e) => ::log::error!("bluetooth: cannot export agent: {e}"),
    }
    let rule = zbus::MatchRule::builder().msg_type(MessageType::Signal).sender(BLUEZ).map(|b| b.build());
    let stream = match rule {
        Ok(rule) => zbus::MessageStream::for_match_rule(rule, &bus, None).await,
        Err(e) => Err(e),
    };
    let mut stream = match stream {
        Ok(stream) => stream,
        Err(e) => {
            ::log::error!("bluetooth: cannot subscribe: {e}");
            return;
        }
    };
    match service.upgrade() {
        Some(service) => service.request_refresh(),
        None => return,
    }
    while stream.next().await.is_some() {
        let Some(service) = service.upgrade() else { return };
        service.request_refresh();
    }
}

/// BlueZ restarting drops our agent registration.
async fn watch_owner(bus: zbus::Connection, service: Weak<BluetoothService>) {
    let stream = async { zbus::fdo::DBusProxy::new(&bus).await?.receive_name_owner_changed_with_args(&[(0, BLUEZ)]).await };
    let mut stream = match stream.await {
        Ok(stream) => stream,
        Err(e) => {
            ::log::error!("bluetooth: cannot watch {BLUEZ}: {e}");
            return;
        }
    };
    while stream.next().await.is_some() {
        let Some(service) = service.upgrade() else { return };
        service.agent_registered.set(false);
        service.request_refresh();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{connect_to, test_bus};
    use crate::service::network::StatusMessage;
    use calloop::EventLoop;
    use futures_lite::future::block_on;
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::time::{Duration, Instant};
    use zbus::names::OwnedInterfaceName;
    use zbus::zvariant::{OwnedObjectPath, Value};

    fn props(entries: &[(&str, Value<'static>)]) -> Properties {
        entries.iter().map(|(k, v)| (k.to_string(), v.try_to_owned().unwrap())).collect()
    }

    fn iface(name: &str) -> OwnedInterfaceName {
        OwnedInterfaceName::try_from(name).unwrap()
    }

    fn path(p: &str) -> OwnedObjectPath {
        OwnedObjectPath::try_from(p).unwrap()
    }

    #[test]
    fn bluetooth_cases() {
        assert_eq!(bluetooth_classify_icon("audio-headset"), BluetoothDeviceKind::Headset);
        assert_eq!(bluetooth_classify_icon("audio-headphones"), BluetoothDeviceKind::Headphones);
        assert_eq!(bluetooth_classify_icon("input-mouse"), BluetoothDeviceKind::Mouse);
        assert_eq!(bluetooth_classify_icon("phone"), BluetoothDeviceKind::Phone);
        assert_eq!(bluetooth_classify_icon("something-unrecognized"), BluetoothDeviceKind::Unknown);
        assert_eq!(bluetooth_classify_class(0x0404), BluetoothDeviceKind::Headset);
        assert_eq!(bluetooth_classify_class(0x0508), BluetoothDeviceKind::Mouse);
        assert_eq!(bluetooth_classify_class(0x0504), BluetoothDeviceKind::Keyboard);

        let connected = BluetoothDevice { connected: true, paired: true, ..Default::default() };
        assert!(bluetooth_is_connected(&connected) && !bluetooth_is_paired(&connected) && !bluetooth_is_nearby(&connected), "connected bucket");
        let paired = BluetoothDevice { paired: true, ..Default::default() };
        assert!(!bluetooth_is_connected(&paired) && bluetooth_is_paired(&paired) && !bluetooth_is_nearby(&paired), "paired bucket");
        assert!(bluetooth_is_paired(&BluetoothDevice { trusted: true, ..Default::default() }), "trusted counts as paired");
        let nearby = BluetoothDevice::default();
        assert!(bluetooth_is_nearby(&nearby) && !bluetooth_is_paired(&nearby), "nearby bucket");

        let mut objects = BluetoothObjects::new();
        objects.insert(path("/org/bluez/hci0"), [(iface("org.bluez.Adapter1"), props(&[("Powered", Value::from(true)), ("Discovering", Value::from(true))]))].into());
        objects.insert(
            path("/org/bluez/hci0/dev_BB"),
            [(iface("org.bluez.Device1"), props(&[("Address", Value::from("CC:DD")), ("Class", Value::from(0x0508u32))]))].into(),
        );
        objects.insert(
            path("/org/bluez/hci0/dev_AA"),
            [
                (
                    iface("org.bluez.Device1"),
                    props(&[
                        ("Address", Value::from("AA:BB")),
                        ("Alias", Value::from("Buds")),
                        ("Paired", Value::from(true)),
                        ("Connected", Value::from(true)),
                        ("Icon", Value::from("audio-headset")),
                    ]),
                ),
                (iface("org.bluez.Battery1"), props(&[("Percentage", Value::from(77u8))])),
            ]
            .into(),
        );
        let snapshot = bluetooth_parse(&objects, &["/org/bluez/hci0/dev_BB".to_string()].into());
        assert!(snapshot.status.present && snapshot.status.powered && snapshot.status.scanning, "adapter state parses");
        assert!(snapshot.status.connected && snapshot.status.device == "Buds", "connected device names the status");
        assert_eq!(snapshot.adapter_path, "/org/bluez/hci0", "adapter path is kept");
        assert_eq!(snapshot.devices.len(), 2, "both devices parse");
        assert!(snapshot.devices[0].battery == 77 && snapshot.devices[0].kind == BluetoothDeviceKind::Headset, "battery and icon kind");
        assert!(
            snapshot.devices[1].name == "CC:DD" && snapshot.devices[1].connecting && snapshot.devices[1].kind == BluetoothDeviceKind::Mouse,
            "address names an unnamed device; busy device is connecting; class gives the kind"
        );
        assert_eq!(bluetooth_parse(&BluetoothObjects::new(), &BTreeSet::new()).status, BluetoothStatus::default(), "no adapter gives the default status");

        assert!(bluetooth_passkey_text(0) == "000000" && bluetooth_passkey_text(42) == "000042" && bluetooth_passkey_text(999999) == "999999");
        assert!(bluetooth_agent_allows("/dev_A", "/dev_A", false), "the pairing device is accepted");
        assert!(!bluetooth_agent_allows("/dev_B", "/dev_A", false), "another device is rejected");
        assert!(bluetooth_agent_allows("/dev_B", "/dev_A", true), "a known device is accepted for services");
        assert!(!bluetooth_agent_allows("/dev_A", "", false) && !bluetooth_agent_allows("", "", false), "no pairing device rejects pairing requests");

        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("rfkill");
        let file = file.to_str().unwrap();
        assert!(rfkill::read_sysfs_uint(file).is_none() && rfkill::read_sysfs_string(file).is_none(), "missing sysfs file reads as nothing");
        std::fs::write(file, "1\n").unwrap();
        assert_eq!(rfkill::read_sysfs_uint(file), Some(1), "sysfs unsigned reads");
        std::fs::write(file, "bluetooth\n").unwrap();
        assert_eq!(rfkill::read_sysfs_string(file).as_deref(), Some("bluetooth"), "sysfs string reads");
    }

    fn msg(summary: &str, body: &str) -> StatusMessage {
        StatusMessage::new(summary, body.into())
    }

    #[test]
    fn bluetooth_status_changes() {
        let device = |path: &str, name: &str, connected| BluetoothDevice { path: path.into(), name: name.into(), connected, ..Default::default() };
        let (buds_off, buds_on, kbd_on) = (device("/dev_F8", "Buds", false), device("/dev_F8", "Buds", true), device("/dev_D1", "Kbd", true));
        let idle = vec![buds_off];
        let buds = vec![buds_on.clone()];
        assert!(bluetooth_changes(&idle, &idle).is_empty(), "unchanged Bluetooth sends nothing");
        assert_eq!(bluetooth_changes(&idle, &buds), vec![msg("Connected", "Connected to Buds")]);
        assert_eq!(bluetooth_changes(&buds, &idle), vec![msg("Disconnected", "Disconnected from Buds")]);
        assert_eq!(bluetooth_changes(&buds, &[kbd_on.clone(), buds_on.clone()]), vec![msg("Connected", "Connected to Kbd")], "second device connect names only that device");
        assert_eq!(bluetooth_changes(&[kbd_on, buds_on], &buds), vec![msg("Disconnected", "Disconnected from Kbd")], "second device drop does not re-announce the first");
    }

    struct FakeAdapter;

    #[zbus::interface(name = "org.bluez.Adapter1")]
    impl FakeAdapter {
        #[zbus(property)]
        fn powered(&self) -> bool {
            true
        }
    }

    struct FakeDevice;

    #[zbus::interface(name = "org.bluez.Device1")]
    impl FakeDevice {
        async fn pair(&self) {
            async_io::Timer::after(Duration::from_secs(5)).await;
        }
        #[zbus(property)]
        fn alias(&self) -> String {
            "Phone".into()
        }
    }

    struct FakeAgentManager;

    #[zbus::interface(name = "org.bluez.AgentManager1")]
    impl FakeAgentManager {
        fn register_agent(&self, _agent: OwnedObjectPath, _capability: String) {}
        fn request_default_agent(&self, _agent: OwnedObjectPath) {}
    }

    #[test]
    fn agent_reply_waits_for_answer() {
        let Some(bus) = test_bus() else { return };
        const DEVICE: &str = "/org/bluez/hci0/dev_AA";
        let _bluez = block_on(async {
            zbus::connection::Builder::address(bus.address())
                .unwrap()
                .name("org.bluez")
                .unwrap()
                .serve_at("/", zbus::fdo::ObjectManager)
                .unwrap()
                .serve_at("/org/bluez", FakeAgentManager)
                .unwrap()
                .serve_at("/org/bluez/hci0", FakeAdapter)
                .unwrap()
                .serve_at(DEVICE, FakeDevice)
                .unwrap()
                .build()
                .await
                .unwrap()
        });
        let conn = block_on(connect_to(bus.address()));
        let service_name = conn.unique_name().unwrap().to_string();
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let service = BluetoothService::start(&conn);
        let mut pump = |done: &dyn Fn() -> bool| {
            let deadline = Instant::now() + Duration::from_secs(2);
            while !done() && Instant::now() < deadline {
                event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
            }
            done()
        };
        assert!(pump(&|| service.devices().len() == 1), "the fake adapter and device are listed");
        service.pair(DEVICE);

        let finished = Arc::new(AtomicBool::new(false));
        let done = finished.clone();
        let address = bus.address().to_string();
        let caller = std::thread::spawn(move || {
            let result = block_on(async {
                let client = connect_to(&address).await;
                let proxy = zbus::Proxy::new(&client, service_name, AGENT_PATH, "org.bluez.Agent1").await.unwrap();
                proxy.call_method("RequestConfirmation", &(OwnedObjectPath::try_from(DEVICE).unwrap(), 123456u32)).await
            });
            done.store(true, Ordering::SeqCst);
            result.map(|_| ())
        });
        assert!(pump(&|| service.agent_request().is_some()), "the request is held for the user");
        let request = service.agent_request().unwrap();
        assert!(request.kind == AgentRequestKind::Confirm && request.code == "123456" && request.name == "Phone");
        assert!(!pump(&|| finished.load(Ordering::SeqCst)), "the call stays pending until answered");
        service.agent_answer(true);
        assert!(pump(&|| finished.load(Ordering::SeqCst)), "answering replies");
        assert!(caller.join().unwrap().is_ok(), "an accepted confirmation succeeds");
        assert!(service.agent_request().is_none());
    }
}
