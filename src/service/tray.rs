use std::cell::{Ref, RefCell};
use std::collections::HashMap;
use std::rc::{Rc, Weak};
use std::sync::{Arc, Mutex};

use futures_lite::StreamExt;
use futures_lite::future;
use zbus::message::Type as MessageType;
use zbus::zvariant::{OwnedObjectPath, OwnedValue, Value};

use crate::base::dbus::{Ownership, Properties, get_all, own_name, value};
use crate::base::signal::Signal;
use crate::service::runtime;

const WATCHER_NAME: &str = "org.kde.StatusNotifierWatcher";
const WATCHER_PATH: &str = "/StatusNotifierWatcher";
const ITEM_INTERFACE: &str = "org.kde.StatusNotifierItem";
const DEFAULT_ITEM_PATH: &str = "/StatusNotifierItem";
const MENU_INTERFACE: &str = "com.canonical.dbusmenu";

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TrayItem {
    pub bus_name: String,
    pub object_path: String,
    pub icon_name: String,
    pub icon_theme_path: String,
    pub menu_path: String,
    pub status: String,
}

impl TrayItem {
    pub fn has_menu(&self) -> bool {
        !self.menu_path.is_empty()
    }

    pub fn key(&self) -> String {
        format!("{}|{}", self.bus_name, self.object_path)
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TrayMenuEntry {
    pub id: i32,
    pub label: String,
    pub enabled: bool,
    pub visible: bool,
    pub separator: bool,
    pub checkbox: bool,
    pub checked: bool,
    pub children: Vec<TrayMenuEntry>,
}

/// One `com.canonical.dbusmenu` node: id, properties, children as variants of nodes.
pub type TrayMenuLayout = (i32, HashMap<String, OwnedValue>, Vec<OwnedValue>);

/// A layout node from a (possibly variant-wrapped) `(ia{sv}av)` value.
pub fn layout_from(value: &Value<'_>) -> Option<TrayMenuLayout> {
    // Children arrive as variants; unwrap every layer down to the node.
    let mut value = value;
    while let Value::Value(inner) = value {
        value = inner.as_ref();
    }
    let Value::Structure(node) = value else { return None };
    let fields = node.fields();
    let Value::I32(id) = fields.first()? else { return None };
    let Value::Dict(props) = fields.get(1)? else { return None };
    let Value::Array(children) = fields.get(2)? else { return None };
    let props = HashMap::<String, OwnedValue>::try_from(props.try_clone().ok()?).ok()?;
    let children = children.iter().filter_map(|c| OwnedValue::try_from(c.try_clone().ok()?).ok()).collect();
    Some((*id, props, children))
}

pub fn tray_parse_menu(node: &TrayMenuLayout) -> TrayMenuEntry {
    let (id, props, children) = node;
    let text = |name| value::<String>(props, name).unwrap_or_default();
    TrayMenuEntry {
        id: *id,
        label: tray_strip_mnemonic(&text("label")),
        enabled: value::<bool>(props, "enabled").unwrap_or(true),
        visible: value::<bool>(props, "visible").unwrap_or(true),
        separator: text("type") == "separator",
        checkbox: !text("toggle-type").is_empty(),
        checked: value::<i32>(props, "toggle-state").unwrap_or(0) == 1,
        children: children.iter().filter_map(|c| layout_from(c)).map(|c| tray_parse_menu(&c)).collect(),
    }
}

/// The item's own themed PNG, else the icon looked up by name.
pub fn tray_item_icon_path(item: &TrayItem) -> String {
    if item.icon_name.is_empty() {
        return String::new();
    }
    if !item.icon_theme_path.is_empty() {
        let candidate = format!("{}/{}.png", item.icon_theme_path, item.icon_name);
        if std::path::Path::new(&candidate).exists() {
            return candidate;
        }
    }
    crate::service::icon::app_icon_path(&item.icon_name)
}

/// Drops `_` mnemonic markers; `__` is a literal underscore.
pub fn tray_strip_mnemonic(label: &str) -> String {
    let mut out = String::new();
    let mut chars = label.chars().peekable();
    while let Some(c) = chars.next() {
        if c != '_' {
            out.push(c);
        } else if chars.peek() == Some(&'_') {
            out.push('_');
            chars.next();
        }
    }
    out
}

fn apply_properties(item: &mut TrayItem, props: &Properties) {
    if let Some(v) = value::<String>(props, "IconName") {
        item.icon_name = v;
    }
    if let Some(v) = value::<String>(props, "IconThemePath") {
        item.icon_theme_path = v;
    }
    if let Some(v) = value::<OwnedObjectPath>(props, "Menu") {
        item.menu_path = v.to_string();
    }
    if let Some(v) = value::<String>(props, "Status") {
        item.status = v;
    }
}

/// The exported watcher; it runs on zbus's thread, reads the registered list from a shared mirror
/// and forwards registrations to the loop thread.
struct Watcher {
    registrations: async_channel::Sender<(String, String)>,
    registered: Arc<Mutex<Vec<String>>>,
}

#[zbus::interface(name = "org.kde.StatusNotifierWatcher")]
impl Watcher {
    async fn register_status_notifier_item(&self, service: String, #[zbus(header)] header: zbus::message::Header<'_>) {
        let sender = header.sender().map(|s| s.to_string()).unwrap_or_default();
        let path = if service.starts_with('/') { service } else { DEFAULT_ITEM_PATH.to_string() };
        let _ = self.registrations.send((sender, path)).await;
    }

    fn register_status_notifier_host(&self, _service: String) {}

    #[zbus(property)]
    fn registered_status_notifier_items(&self) -> Vec<String> {
        self.registered.lock().map(|r| r.clone()).unwrap_or_default()
    }

    #[zbus(property)]
    fn is_status_notifier_host_registered(&self) -> bool {
        true
    }

    #[zbus(property)]
    fn protocol_version(&self) -> i32 {
        0
    }
}

pub struct TrayService {
    this: Weak<TrayService>,
    bus: zbus::Connection,
    owns_name: std::cell::Cell<bool>,
    items: RefCell<Vec<TrayItem>>,
    registered: Arc<Mutex<Vec<String>>>,
    /// Dropping an item's sender ends its signal task.
    watches: RefCell<HashMap<String, async_channel::Sender<()>>>,
    menus: RefCell<HashMap<String, Vec<TrayMenuEntry>>>,
    pub changed: Signal,
}

impl TrayService {
    fn new(bus: &zbus::Connection) -> Rc<TrayService> {
        Rc::new_cyclic(|this| TrayService {
            this: this.clone(),
            bus: bus.clone(),
            owns_name: std::cell::Cell::new(false),
            items: RefCell::default(),
            registered: Arc::default(),
            watches: RefCell::default(),
            menus: RefCell::default(),
            changed: Signal::default(),
        })
    }

    pub fn start(session_bus: &zbus::Connection) -> Rc<TrayService> {
        let service = TrayService::new(session_bus);
        let weak = Rc::downgrade(&service);
        runtime::spawn(async move {
            if let Some(service) = weak.upgrade() {
                service.serve().await;
            }
        });
        service
    }

    #[cfg(test)]
    pub async fn start_on(bus: &zbus::Connection) -> Rc<TrayService> {
        let service = TrayService::new(bus);
        service.serve().await;
        service
    }

    /// Exports the watcher, then asks for its name without queueing or replacing another owner.
    async fn serve(&self) {
        let (registrations, incoming) = async_channel::unbounded();
        let watcher = Watcher { registrations, registered: self.registered.clone() };
        if let Err(e) = self.bus.object_server().at(WATCHER_PATH, watcher).await {
            ::log::error!("tray: cannot export {WATCHER_PATH}: {e}");
            return;
        }
        match own_name(&self.bus, WATCHER_NAME).await {
            Ownership::Owned => ::log::info!("tray: serving {WATCHER_NAME}"),
            ownership => {
                if ownership == Ownership::Taken {
                    ::log::info!("tray: {WATCHER_NAME} is owned by another process");
                }
                let _ = self.bus.object_server().remove::<Watcher, _>(WATCHER_PATH).await;
                return;
            }
        }
        self.owns_name.set(true);
        let weak = self.this.clone();
        runtime::spawn(async move {
            while let Ok((bus_name, path)) = incoming.recv().await {
                let Some(service) = weak.upgrade() else { return };
                service.register_item(&bus_name, &path);
            }
        });
        runtime::spawn(watch_owners(self.bus.clone(), self.this.clone()));
    }

    #[cfg(test)]
    pub fn owns_name(&self) -> bool {
        self.owns_name.get()
    }

    pub fn items(&self) -> Ref<'_, Vec<TrayItem>> {
        self.items.borrow()
    }

    pub fn find(&self, key: &str) -> Option<TrayItem> {
        self.items.borrow().iter().find(|i| i.key() == key).cloned()
    }

    pub fn menu(&self, key: &str) -> Option<Vec<TrayMenuEntry>> {
        self.menus.borrow().get(key).cloned()
    }

    fn publish_registered(&self) {
        let list = self.items.borrow().iter().map(|i| format!("{}{}", i.bus_name, i.object_path)).collect();
        if let Ok(mut registered) = self.registered.lock() {
            *registered = list;
        }
    }

    fn register_item(&self, bus_name: &str, object_path: &str) {
        let item = TrayItem { bus_name: bus_name.into(), object_path: object_path.into(), ..Default::default() };
        let key = item.key();
        if self.find(&key).is_some() {
            return;
        }
        let (stop, stopped) = async_channel::bounded::<()>(1);
        self.watches.borrow_mut().insert(key.clone(), stop);
        runtime::spawn(watch_item(self.bus.clone(), self.this.clone(), item.clone(), stopped));
        self.items.borrow_mut().push(item);
        self.publish_registered();
        ::log::info!("tray: {key} added");
        self.fetch(&key);
        self.changed.emit(&());
    }

    fn fetch(&self, key: &str) {
        let Some(item) = self.find(key) else { return };
        let (weak, bus, key) = (self.this.clone(), self.bus.clone(), key.to_string());
        runtime::spawn(async move {
            let props = match get_all(&bus, &item.bus_name, &item.object_path, ITEM_INTERFACE).await {
                Ok(props) => props,
                Err(e) => {
                    ::log::error!("tray: GetAll failed for {key}: {e}");
                    return;
                }
            };
            let Some(service) = weak.upgrade() else { return };
            let changed = {
                let mut items = service.items.borrow_mut();
                let Some(item) = items.iter_mut().find(|i| i.key() == key) else { return };
                let before = item.clone();
                apply_properties(item, &props);
                *item != before
            };
            if changed {
                service.changed.emit(&());
            }
        });
    }

    fn remove_owner(&self, name: &str) {
        let prefix = format!("{name}|");
        let removed: Vec<String> = {
            let mut items = self.items.borrow_mut();
            let removed = items.iter().filter(|i| i.bus_name == name).map(TrayItem::key).collect();
            items.retain(|i| i.bus_name != name);
            removed
        };
        self.watches.borrow_mut().retain(|k, _| !k.starts_with(&prefix));
        self.menus.borrow_mut().retain(|k, _| !k.starts_with(&prefix));
        if removed.is_empty() {
            return;
        }
        self.publish_registered();
        for key in removed {
            ::log::info!("tray: {key} removed");
        }
        self.changed.emit(&());
    }

    async fn call<R>(bus: &zbus::Connection, dest: &str, path: &str, interface: &str, method: &str, body: &(impl zbus::export::serde::Serialize + zbus::zvariant::DynamicType)) -> zbus::Result<R>
    where
        R: for<'d> zbus::export::serde::Deserialize<'d> + zbus::zvariant::Type,
    {
        let proxy = zbus::Proxy::new(bus, dest.to_owned(), path.to_owned(), interface.to_owned()).await?;
        proxy.call(method, body).await
    }

    pub fn activate(&self, key: &str, secondary: bool) {
        let Some(item) = self.find(key) else { return };
        let bus = self.bus.clone();
        let method = if secondary { "SecondaryActivate" } else { "Activate" };
        runtime::spawn(async move {
            if let Err(e) = TrayService::call::<()>(&bus, &item.bus_name, &item.object_path, ITEM_INTERFACE, method, &(0i32, 0i32)).await {
                ::log::error!("tray: Activate failed: {e}");
            }
        });
    }

    pub fn request_menu(&self, key: &str) {
        let Some(item) = self.find(key).filter(TrayItem::has_menu) else { return };
        let (weak, bus, key) = (self.this.clone(), self.bus.clone(), key.to_string());
        runtime::spawn(async move {
            let _ = TrayService::call::<bool>(&bus, &item.bus_name, &item.menu_path, MENU_INTERFACE, "AboutToShow", &(0i32,)).await;
            let layout = TrayService::call::<(u32, TrayMenuLayout)>(&bus, &item.bus_name, &item.menu_path, MENU_INTERFACE, "GetLayout", &(0i32, -1i32, Vec::<String>::new())).await;
            let (_, layout) = match layout {
                Ok(layout) => layout,
                Err(e) => {
                    ::log::error!("tray: GetLayout failed for {key}: {e}");
                    return;
                }
            };
            let Some(service) = weak.upgrade() else { return };
            if service.find(&key).is_none() {
                return;
            }
            service.menus.borrow_mut().insert(key, tray_parse_menu(&layout).children);
            service.changed.emit(&());
        });
    }

    pub fn menu_clicked(&self, key: &str, id: i32) {
        let Some(item) = self.find(key).filter(TrayItem::has_menu) else { return };
        let bus = self.bus.clone();
        runtime::spawn(async move {
            let body = (id, "clicked", Value::from(0i32), 0u32);
            if let Err(e) = TrayService::call::<()>(&bus, &item.bus_name, &item.menu_path, MENU_INTERFACE, "Event", &body).await {
                ::log::error!("tray: menu Event failed: {e}");
            }
        });
    }
}

/// Re-reads an item on `PropertiesChanged`, `NewIcon`, `NewStatus` and `NewMenu`, until it is removed.
async fn watch_item(bus: zbus::Connection, service: Weak<TrayService>, item: TrayItem, stopped: async_channel::Receiver<()>) {
    let key = item.key();
    let rule = zbus::MatchRule::builder()
        .msg_type(MessageType::Signal)
        .sender(item.bus_name.as_str())
        .and_then(|b| b.path(item.object_path.as_str()))
        .map(|b| b.build());
    let stream = match rule {
        Ok(rule) => zbus::MessageStream::for_match_rule(rule, &bus, None).await,
        Err(e) => Err(e),
    };
    let Ok(mut stream) = stream else { return };
    let signals = async {
        while let Some(Ok(message)) = stream.next().await {
            let member = message.header().member().map(|m| m.to_string()).unwrap_or_default();
            if !matches!(member.as_str(), "PropertiesChanged" | "NewIcon" | "NewStatus" | "NewMenu") {
                continue;
            }
            let Some(service) = service.upgrade() else { return };
            service.fetch(&key);
        }
    };
    future::or(signals, async {
        let _ = stopped.recv().await;
    })
    .await;
}

/// Drops the items of a name that leaves the bus.
async fn watch_owners(bus: zbus::Connection, service: Weak<TrayService>) {
    let stream = async { zbus::fdo::DBusProxy::new(&bus).await?.receive_name_owner_changed().await };
    let mut stream = match stream.await {
        Ok(stream) => stream,
        Err(e) => {
            ::log::error!("tray: cannot watch name owners: {e}");
            return;
        }
    };
    while let Some(signal) = stream.next().await {
        let Ok(args) = signal.args() else { continue };
        if args.new_owner().is_some() {
            continue;
        }
        let Some(service) = service.upgrade() else { return };
        service.remove_owner(args.name().as_str());
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{connect_to, test_bus};
    use calloop::EventLoop;
    use futures_lite::future::block_on;
    use std::time::{Duration, Instant};
    use zbus::zvariant::Value;

    fn node(id: i32, props: &[(&str, Value<'static>)], children: Vec<Value<'static>>) -> Value<'static> {
        let props: HashMap<String, Value<'static>> = props.iter().map(|(k, v)| (k.to_string(), v.try_clone().unwrap())).collect();
        Value::from((id, props, children))
    }

    #[test]
    fn tray_parsing() {
        assert_eq!(tray_strip_mnemonic("_Open __file"), "Open _file", "mnemonics stripped, doubled kept");
        let leaf = node(3, &[("label", Value::from("_Quit")), ("enabled", Value::from(false))], vec![]);
        let separator = node(2, &[("type", Value::from("separator"))], vec![]);
        let toggle = node(4, &[("label", Value::from("Mute")), ("toggle-type", Value::from("checkmark")), ("toggle-state", Value::from(1i32))], vec![]);
        let sub = node(1, &[("label", Value::from("More")), ("children-display", Value::from("submenu"))], vec![Value::Value(Box::new(toggle))]);
        let root = node(0, &[], vec![Value::Value(Box::new(sub)), Value::Value(Box::new(separator)), Value::Value(Box::new(leaf))]);
        let root = layout_from(&root).unwrap();
        let menu = tray_parse_menu(&root);
        assert_eq!(menu.children.len(), 3, "three top-level entries");
        assert!(menu.children[0].children.len() == 1 && menu.children[0].children[0].checkbox && menu.children[0].children[0].checked, "nested checked toggle");
        assert!(menu.children[1].separator, "separator parsed");
        assert!(menu.children[2].label == "Quit" && !menu.children[2].enabled, "disabled leaf label");

        let item = TrayItem { bus_name: ":1.5".into(), object_path: "/StatusNotifierItem".into(), ..Default::default() };
        assert!(item.key() == ":1.5|/StatusNotifierItem" && !item.has_menu());
        assert_eq!(tray_item_icon_path(&item), "", "no icon name, no path");
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("app.png"), b"").unwrap();
        let themed = TrayItem { icon_name: "app".into(), icon_theme_path: dir.path().to_str().unwrap().into(), ..Default::default() };
        assert_eq!(tray_item_icon_path(&themed), dir.path().join("app.png").to_str().unwrap(), "the item's own theme path wins");
        let absolute = dir.path().join("app.png").to_str().unwrap().to_string();
        let looked_up = TrayItem { icon_name: absolute.clone(), ..Default::default() };
        assert_eq!(tray_item_icon_path(&looked_up), absolute, "no theme path falls back to the icon lookup");
        let missing = TrayItem { icon_name: "app-gone".into(), icon_theme_path: dir.path().to_str().unwrap().into(), ..Default::default() };
        assert_eq!(tray_item_icon_path(&missing), crate::service::icon::app_icon_path("app-gone"), "a missing themed file falls back too");
    }

    struct FakeItem;

    #[zbus::interface(name = "org.kde.StatusNotifierItem")]
    impl FakeItem {
        #[zbus(property)]
        fn icon_name(&self) -> String {
            "fake-icon".into()
        }
        #[zbus(property)]
        fn status(&self) -> String {
            "Active".into()
        }
    }

    #[test]
    fn tray_registers_item() {
        let Some(bus) = test_bus() else { return };
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let conn = block_on(connect_to(bus.address()));
        let service = block_on(TrayService::start_on(&conn));
        assert!(service.owns_name());
        let client = block_on(async {
            zbus::connection::Builder::address(bus.address()).unwrap().serve_at("/StatusNotifierItem", FakeItem).unwrap().build().await.unwrap()
        });
        let own_name = client.unique_name().unwrap().to_string();
        block_on(async {
            let watcher = zbus::Proxy::new(&client, WATCHER_NAME, WATCHER_PATH, WATCHER_NAME).await.unwrap();
            watcher.call_method("RegisterStatusNotifierItem", &(own_name.as_str(),)).await.unwrap();
        });
        let mut pump = |done: &dyn Fn() -> bool| {
            let deadline = Instant::now() + Duration::from_secs(2);
            while !done() && Instant::now() < deadline {
                event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
            }
            done()
        };
        let key = format!("{own_name}|/StatusNotifierItem");
        assert!(pump(&|| service.find(&key).is_some_and(|i| i.icon_name == "fake-icon")), "the item is registered and its properties read");
        assert_eq!(service.items().len(), 1);
        drop(client);
        assert!(pump(&|| service.items().is_empty()), "the item goes when its owner leaves the bus");
    }
}
