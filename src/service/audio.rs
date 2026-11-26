use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::{Rc, Weak};
use std::thread::JoinHandle;

use pipewire as pw;
use pw::spa::param::{ParamInfoFlags, ParamType};
use pw::spa::pod::deserialize::PodDeserializer;
use pw::spa::pod::serialize::PodSerializer;
use pw::spa::pod::{Object, Pod, Property, Value, ValueArray};
use pw::spa::sys as spa;
use pw::types::ObjectType;

use crate::base::signal::Signal;
use crate::service::runtime;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AudioKind {
    Sink,
    Source,
    Nodes,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AudioLevel {
    pub percent: i32,
    pub muted: bool,
    pub present: bool,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum AudioNodeKind {
    #[default]
    Sink,
    Source,
    Playback,
    Capture,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AudioNode {
    pub id: u32,
    pub kind: AudioNodeKind,
    pub label: String,
    pub percent: i32,
    pub muted: bool,
}

/// Mean of the cube roots of the channel volumes, as a percentage.
pub fn audio_percent(channel_volumes: &[f32]) -> i32 {
    if channel_volumes.is_empty() {
        return 0;
    }
    let total: f64 = channel_volumes.iter().map(|v| f64::from(*v).cbrt()).sum();
    (total / channel_volumes.len() as f64 * 100.0).round() as i32
}

/// Everything the loop thread reads, sent whole after each PipeWire change.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct Snapshot {
    sink_id: u32,
    source_id: u32,
    nodes: Vec<AudioNode>,
}

impl Snapshot {
    fn level(&self, id: u32) -> AudioLevel {
        self.nodes.iter().find(|n| n.id == id && id != 0).map_or_else(AudioLevel::default, |n| AudioLevel { percent: n.percent, muted: n.muted, present: true })
    }
}

enum Command {
    Volume(u32, i32),
    Mute(u32, bool),
    Default(u32),
    Quit,
}

pub struct AudioService {
    snapshot: RefCell<Snapshot>,
    commands: pw::channel::Sender<Command>,
    thread: RefCell<Option<JoinHandle<()>>>,
    pub changed: Signal<AudioKind>,
}

impl AudioService {
    /// `None`, with a log line, when PipeWire cannot be reached.
    pub fn start() -> Option<Rc<AudioService>> {
        AudioService::start_with_remote(None)
    }

    pub fn start_with_remote(remote: Option<&str>) -> Option<Rc<AudioService>> {
        let (updates, incoming) = async_channel::unbounded();
        let (commands, command_rx) = pw::channel::channel();
        let (ready_tx, ready) = std::sync::mpsc::channel();
        let remote = remote.map(str::to_string);
        let thread = std::thread::Builder::new()
            .name("pipewire".into())
            .spawn(move || pipewire_thread(remote, updates, command_rx, ready_tx))
            .inspect_err(|e| ::log::error!("audio: cannot start the PipeWire thread: {e}"))
            .ok()?;
        if let Err(e) = ready.recv().unwrap_or(Err("the PipeWire thread exited".into())) {
            ::log::info!("audio: cannot connect to PipeWire: {e}");
            let _ = thread.join();
            return None;
        }
        let service = Rc::new(AudioService { snapshot: RefCell::default(), commands, thread: RefCell::new(Some(thread)), changed: Signal::default() });
        let weak = Rc::downgrade(&service);
        runtime::spawn(async move {
            while let Ok(next) = incoming.recv().await {
                let Some(service) = weak.upgrade() else { return };
                service.apply(next);
            }
        });
        Some(service)
    }

    fn apply(&self, next: Snapshot) {
        let prev = self.snapshot.replace(next);
        let next = self.snapshot.borrow();
        let sink = (prev.level(prev.sink_id), next.level(next.sink_id));
        let source = (prev.level(prev.source_id), next.level(next.source_id));
        let nodes_changed = prev.nodes != next.nodes;
        drop(next);
        if sink.0 != sink.1 {
            ::log::info!("audio: sink {:?}", sink.1);
            self.changed.emit(&AudioKind::Sink);
        }
        if source.0 != source.1 {
            ::log::info!("audio: source {:?}", source.1);
            self.changed.emit(&AudioKind::Source);
        }
        if nodes_changed {
            self.changed.emit(&AudioKind::Nodes);
        }
    }

    pub fn sink(&self) -> AudioLevel {
        let s = self.snapshot.borrow();
        s.level(s.sink_id)
    }

    pub fn source(&self) -> AudioLevel {
        let s = self.snapshot.borrow();
        s.level(s.source_id)
    }

    pub fn sink_id(&self) -> u32 {
        self.snapshot.borrow().sink_id
    }

    pub fn source_id(&self) -> u32 {
        self.snapshot.borrow().source_id
    }

    /// The nodes of one kind, by id.
    pub fn nodes(&self, kind: AudioNodeKind) -> Vec<AudioNode> {
        self.snapshot.borrow().nodes.iter().filter(|n| n.kind == kind).cloned().collect()
    }

    pub fn node(&self, id: u32) -> Option<AudioNode> {
        self.snapshot.borrow().nodes.iter().find(|n| n.id == id).cloned()
    }

    fn send(&self, command: Command) {
        if self.commands.send(command).is_err() {
            ::log::error!("audio: the PipeWire thread is gone");
        }
    }

    pub fn set_volume(&self, id: u32, percent: i32) {
        self.send(Command::Volume(id, percent));
    }

    pub fn set_mute(&self, id: u32, muted: bool) {
        self.send(Command::Mute(id, muted));
    }

    pub fn set_default(&self, id: u32) {
        self.send(Command::Default(id));
    }
}

impl Drop for AudioService {
    fn drop(&mut self) {
        let _ = self.commands.send(Command::Quit);
        if let Some(thread) = self.thread.borrow_mut().take() {
            let _ = thread.join();
        }
    }
}

struct PwNode {
    proxy: pw::node::Node,
    _listener: pw::node::NodeListener,
    name: String,
    label: String,
    kind: AudioNodeKind,
    channels: u32,
    percent: i32,
    muted: bool,
    device_id: u32,
    card_profile_device: i32,
}

impl PwNode {
    fn is_device(&self) -> bool {
        matches!(self.kind, AudioNodeKind::Sink | AudioNodeKind::Source)
    }
}

struct PwDevice {
    proxy: pw::device::Device,
    _listener: pw::device::DeviceListener,
    route_index: HashMap<i32, i32>,
}

struct State {
    nodes: HashMap<u32, PwNode>,
    devices: HashMap<u32, PwDevice>,
    metadata: Option<(pw::metadata::Metadata, pw::metadata::MetadataListener)>,
    sink_name: String,
    source_name: String,
    sink_id: u32,
    source_id: u32,
    updates: async_channel::Sender<Snapshot>,
}

impl State {
    fn publish(&self) {
        let mut nodes: Vec<AudioNode> = self
            .nodes
            .iter()
            .map(|(id, n)| AudioNode { id: *id, kind: n.kind, label: n.label.clone(), percent: n.percent, muted: n.muted })
            .collect();
        nodes.sort_by_key(|n| n.id);
        let _ = self.updates.try_send(Snapshot { sink_id: self.sink_id, source_id: self.source_id, nodes });
    }
}

type Shared = Rc<RefCell<State>>;

fn pod_bytes(value: &Value) -> Option<Vec<u8>> {
    PodSerializer::serialize(std::io::Cursor::new(Vec::new()), value).ok().map(|(cursor, _)| cursor.into_inner())
}

fn object_properties(pod: &Pod) -> Vec<Property> {
    match PodDeserializer::deserialize_any_from(pod.as_bytes()) {
        Ok((_, Value::Object(object))) => object.properties,
        _ => Vec::new(),
    }
}

/// `{ "name": "<value>" }` from the default-node metadata.
fn json_name(value: Option<&str>) -> String {
    let Some(json) = value else { return String::new() };
    let Some(key) = json.find("\"name\"") else { return String::new() };
    let Some(colon) = json[key..].find(':').map(|c| key + c) else { return String::new() };
    let Some(start) = json[colon + 1..].find('"').map(|s| colon + 1 + s) else { return String::new() };
    let Some(end) = json[start + 1..].find('"').map(|e| start + 1 + e) else { return String::new() };
    json[start + 1..end].to_string()
}

fn node_param(state: &Weak<RefCell<State>>, id: u32, param: &Pod) {
    let Some(state) = state.upgrade() else { return };
    let mut state = state.borrow_mut();
    let Some(node) = state.nodes.get_mut(&id) else { return };
    let (mut percent, mut muted) = (node.percent, node.muted);
    for property in object_properties(param) {
        match (property.key, property.value) {
            (spa::SPA_PROP_channelVolumes, Value::ValueArray(ValueArray::Float(volumes))) if !volumes.is_empty() => {
                percent = audio_percent(&volumes);
                node.channels = volumes.len() as u32;
            }
            (spa::SPA_PROP_mute, Value::Bool(value)) => muted = value,
            _ => {}
        }
    }
    if (percent, muted) == (node.percent, node.muted) {
        return;
    }
    (node.percent, node.muted) = (percent, muted);
    state.publish();
}

fn device_param(state: &Weak<RefCell<State>>, id: u32, param: &Pod) {
    let Some(state) = state.upgrade() else { return };
    let (mut device, mut index) = (None, None);
    for property in object_properties(param) {
        match (property.key, property.value) {
            (spa::SPA_PARAM_ROUTE_device, Value::Int(v)) => device = Some(v),
            (spa::SPA_PARAM_ROUTE_index, Value::Int(v)) => index = Some(v),
            _ => {}
        }
    }
    if let (Some(device), Some(index), Some(entry)) = (device, index, state.borrow_mut().devices.get_mut(&id)) {
        entry.route_index.insert(device, index);
    }
}

fn readwrite(params: &[pw::spa::param::ParamInfo], kind: ParamType) -> bool {
    params.iter().any(|p| p.id() == kind && p.flags().contains(ParamInfoFlags::READWRITE))
}

fn add_node(shared: &Shared, registry: &pw::registry::RegistryRc, obj: &pw::registry::GlobalObject<&pw::spa::utils::dict::DictRef>) {
    let props = obj.props;
    let get = |key: &str| props.and_then(|p| p.get(key)).unwrap_or("");
    let kind = match get("media.class") {
        "Audio/Sink" => AudioNodeKind::Sink,
        "Audio/Source" => AudioNodeKind::Source,
        "Stream/Output/Audio" => AudioNodeKind::Playback,
        "Stream/Input/Audio" => AudioNodeKind::Capture,
        _ => return,
    };
    let Ok(proxy) = registry.bind::<pw::node::Node, _>(obj) else { return };
    let first = |keys: &[&str]| keys.iter().map(|k| get(k)).find(|v| !v.is_empty()).unwrap_or("Unknown").to_string();
    let label = if matches!(kind, AudioNodeKind::Sink | AudioNodeKind::Source) {
        first(&["node.description", "node.name"])
    } else {
        first(&["application.name", "node.description", "media.name", "node.name"])
    };
    let id = obj.id;
    let weak = Rc::downgrade(shared);
    let (info_state, param_state) = (weak.clone(), weak);
    let listener = proxy
        .add_listener_local()
        .info(move |info| {
            let Some(state) = info_state.upgrade() else { return };
            let mut state = state.borrow_mut();
            let Some(node) = state.nodes.get_mut(&id) else { return };
            if info.change_mask().contains(pw::node::NodeChangeMask::PROPS)
                && let Some(device) = info.props().and_then(|p| p.get("card.profile.device")).and_then(|v| v.parse().ok())
            {
                node.card_profile_device = device;
            }
            if info.change_mask().contains(pw::node::NodeChangeMask::PARAMS) && readwrite(info.params(), ParamType::Props) {
                node.proxy.enum_params(0, Some(ParamType::Props), 0, u32::MAX);
            }
        })
        .param(move |_, kind, index, _, param| {
            if let (ParamType::Props, 0, Some(param)) = (kind, index, param) {
                node_param(&param_state, id, param);
            }
        })
        .register();
    // A listener alone delivers no live parameter updates.
    proxy.subscribe_params(&[ParamType::Props]);
    let node = PwNode {
        proxy,
        _listener: listener,
        name: get("node.name").to_string(),
        label,
        kind,
        channels: 2,
        percent: 0,
        muted: false,
        device_id: get("device.id").parse().unwrap_or(0),
        card_profile_device: get("card.profile.device").parse().unwrap_or(-1),
    };
    let mut state = shared.borrow_mut();
    if kind == AudioNodeKind::Sink && node.name == state.sink_name {
        state.sink_id = id;
    }
    if kind == AudioNodeKind::Source && node.name == state.source_name {
        state.source_id = id;
    }
    state.nodes.insert(id, node);
    state.publish();
}

fn add_device(shared: &Shared, registry: &pw::registry::RegistryRc, obj: &pw::registry::GlobalObject<&pw::spa::utils::dict::DictRef>) {
    let Ok(proxy) = registry.bind::<pw::device::Device, _>(obj) else { return };
    let id = obj.id;
    let weak = Rc::downgrade(shared);
    let (info_state, param_state) = (weak.clone(), weak);
    let listener = proxy
        .add_listener_local()
        .info(move |info| {
            if !info.change_mask().contains(pw::device::DeviceChangeMask::PARAMS) || !readwrite(info.params(), ParamType::Route) {
                return;
            }
            if let Some(state) = info_state.upgrade()
                && let Some(device) = state.borrow().devices.get(&id)
            {
                device.proxy.enum_params(0, Some(ParamType::Route), 0, u32::MAX);
            }
        })
        .param(move |_, kind, _, _, param| {
            if let (ParamType::Route, Some(param)) = (kind, param) {
                device_param(&param_state, id, param);
            }
        })
        .register();
    shared.borrow_mut().devices.insert(id, PwDevice { proxy, _listener: listener, route_index: HashMap::new() });
}

fn add_metadata(shared: &Shared, registry: &pw::registry::RegistryRc, obj: &pw::registry::GlobalObject<&pw::spa::utils::dict::DictRef>) {
    if shared.borrow().metadata.is_some() || obj.props.and_then(|p| p.get("metadata.name")) != Some("default") {
        return;
    }
    let Ok(proxy) = registry.bind::<pw::metadata::Metadata, _>(obj) else { return };
    let weak = Rc::downgrade(shared);
    let listener = proxy
        .add_listener_local()
        .property(move |_, key, _, value| {
            let sink = key == Some("default.audio.sink");
            if !sink && key != Some("default.audio.source") {
                return 0;
            }
            let Some(state) = weak.upgrade() else { return 0 };
            let mut state = state.borrow_mut();
            let name = json_name(value);
            let wanted = if sink { AudioNodeKind::Sink } else { AudioNodeKind::Source };
            let resolved = state.nodes.iter().find(|(_, n)| n.is_device() && n.name == name && n.kind == wanted).map_or(0, |(id, _)| *id);
            if sink {
                (state.sink_name, state.sink_id) = (name, resolved);
            } else {
                (state.source_name, state.source_id) = (name, resolved);
            }
            state.publish();
            0
        })
        .register();
    shared.borrow_mut().metadata = Some((proxy, listener));
}

fn remove_global(shared: &Shared, id: u32) {
    let mut state = shared.borrow_mut();
    state.devices.remove(&id);
    if state.nodes.remove(&id).is_none() {
        return;
    }
    if state.sink_id == id {
        state.sink_id = 0;
    }
    if state.source_id == id {
        state.source_id = 0;
    }
    state.publish();
}

/// Writes through the parent device's route when there is one, so the change is saved and shown by
/// other mixers; else on the node itself.
fn set_props(state: &State, node: &PwNode, props: Object) {
    if node.card_profile_device >= 0
        && let Some(device) = state.devices.get(&node.device_id)
        && let Some(index) = device.route_index.get(&node.card_profile_device)
    {
        let route = Value::Object(Object {
            type_: spa::SPA_TYPE_OBJECT_ParamRoute,
            id: spa::SPA_PARAM_Route,
            properties: vec![
                Property::new(spa::SPA_PARAM_ROUTE_device, Value::Int(node.card_profile_device)),
                Property::new(spa::SPA_PARAM_ROUTE_index, Value::Int(*index)),
                Property::new(spa::SPA_PARAM_ROUTE_props, Value::Object(props)),
                Property::new(spa::SPA_PARAM_ROUTE_save, Value::Bool(true)),
            ],
        });
        if let Some(bytes) = pod_bytes(&route)
            && let Some(pod) = Pod::from_bytes(&bytes)
        {
            device.proxy.set_param(ParamType::Route, 0, pod);
        }
        return;
    }
    if let Some(bytes) = pod_bytes(&Value::Object(props))
        && let Some(pod) = Pod::from_bytes(&bytes)
    {
        node.proxy.set_param(ParamType::Props, 0, pod);
    }
}

fn props_object(property: Property) -> Object {
    Object { type_: spa::SPA_TYPE_OBJECT_Props, id: spa::SPA_PARAM_Props, properties: vec![property] }
}

fn run_command(shared: &Shared, command: Command) {
    let state = shared.borrow();
    match command {
        Command::Volume(id, percent) => {
            let Some(node) = state.nodes.get(&id) else { return };
            let level = percent as f32 / 100.0;
            let volumes = vec![level * level * level; node.channels as usize];
            set_props(&state, node, props_object(Property::new(spa::SPA_PROP_channelVolumes, Value::ValueArray(ValueArray::Float(volumes)))));
        }
        Command::Mute(id, muted) => {
            let Some(node) = state.nodes.get(&id) else { return };
            set_props(&state, node, props_object(Property::new(spa::SPA_PROP_mute, Value::Bool(muted))));
        }
        Command::Default(id) => {
            let (Some(node), Some((metadata, _))) = (state.nodes.get(&id).filter(|n| n.is_device()), &state.metadata) else { return };
            let key = if node.kind == AudioNodeKind::Sink { "default.configured.audio.sink" } else { "default.configured.audio.source" };
            metadata.set_property(0, key, Some("Spa:String:JSON"), Some(&format!("{{ \"name\": \"{}\" }}", node.name)));
        }
        Command::Quit => {}
    }
}

/// Owns every PipeWire object; reports readiness, then runs until `Quit`.
fn pipewire_thread(
    remote: Option<String>,
    updates: async_channel::Sender<Snapshot>,
    commands: pw::channel::Receiver<Command>,
    ready: std::sync::mpsc::Sender<Result<(), String>>,
) {
    pw::init();
    let connected = (|| {
        let main_loop = pw::main_loop::MainLoopRc::new(None)?;
        let context = pw::context::ContextRc::new(&main_loop, None)?;
        let props = remote.map(|remote| pw::properties::properties! { "remote.name" => remote });
        let core = context.connect_rc(props)?;
        let registry = core.get_registry_rc()?;
        Ok::<_, pw::Error>((main_loop, context, core, registry))
    })();
    let (main_loop, _context, _core, registry) = match connected {
        Ok(parts) => parts,
        Err(e) => {
            let _ = ready.send(Err(e.to_string()));
            return;
        }
    };
    let shared: Shared = Rc::new(RefCell::new(State {
        nodes: HashMap::new(),
        devices: HashMap::new(),
        metadata: None,
        sink_name: String::new(),
        source_name: String::new(),
        sink_id: 0,
        source_id: 0,
        updates,
    }));
    let (global_state, remove_state, registry_weak) = (Rc::downgrade(&shared), Rc::downgrade(&shared), registry.downgrade());
    let _registry_listener = registry
        .add_listener_local()
        .global(move |obj| {
            let (Some(shared), Some(registry)) = (global_state.upgrade(), registry_weak.upgrade()) else { return };
            match obj.type_ {
                ObjectType::Node => add_node(&shared, &registry, obj),
                ObjectType::Device => add_device(&shared, &registry, obj),
                ObjectType::Metadata => add_metadata(&shared, &registry, obj),
                _ => {}
            }
        })
        .global_remove(move |id| {
            if let Some(shared) = remove_state.upgrade() {
                remove_global(&shared, id);
            }
        })
        .register();
    let (command_state, loop_weak) = (Rc::downgrade(&shared), main_loop.downgrade());
    let _commands = commands.attach(main_loop.loop_(), move |command| {
        if matches!(command, Command::Quit) {
            if let Some(main_loop) = loop_weak.upgrade() {
                main_loop.quit();
            }
            return;
        }
        if let Some(shared) = command_state.upgrade() {
            run_command(&shared, command);
        }
    });
    let _ = ready.send(Ok(()));
    main_loop.run();
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{Duration, Instant};

    #[test]
    fn audio_percent_cases() {
        assert_eq!(audio_percent(&[0.064, 0.064]), 40, "audio equal channels");
        assert_eq!(audio_percent(&[0.0, 1.0]), 50, "audio uneven channels average");
        assert_eq!(audio_percent(&[1.0]), 100, "audio full volume");
        assert_eq!(audio_percent(&[]), 0, "audio no channels");
    }

    #[test]
    fn audio_start_without_pipewire() {
        let started = Instant::now();
        assert!(AudioService::start_with_remote(Some("/nonexistent/astralia-test")).is_none(), "an unreachable PipeWire is no service");
        assert!(started.elapsed() < Duration::from_secs(2), "the failed start returns promptly, its thread joined");
    }
}
