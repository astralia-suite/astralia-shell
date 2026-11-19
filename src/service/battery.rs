use std::cell::{Cell, Ref, RefCell};
use std::collections::BTreeMap;
use std::rc::{Rc, Weak};

use futures_lite::StreamExt;
use zbus::message::Type as MessageType;
use zbus::zvariant::OwnedObjectPath;

use crate::base::dbus::{Coalesce, Properties, get_all, value};
use crate::base::signal::Signal;
use crate::service::runtime;

pub const UPOWER_TYPE_BATTERY: u32 = 2;
pub const UPOWER_TYPE_MONITOR: u32 = 4;
pub const UPOWER_STATE_CHARGING: u32 = 1;
pub const UPOWER_STATE_DISCHARGING: u32 = 2;
pub const UPOWER_STATE_FULLY_CHARGED: u32 = 4;
pub const UPOWER_STATE_PENDING_CHARGE: u32 = 5;

const UPOWER_SERVICE: &str = "org.freedesktop.UPower";
const MANAGER_PATH: &str = "/org/freedesktop/UPower";
const DISPLAY_PATH: &str = "/org/freedesktop/UPower/devices/DisplayDevice";
const MANAGER_INTERFACE: &str = "org.freedesktop.UPower";
const DEVICE_INTERFACE: &str = "org.freedesktop.UPower.Device";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BatteryStatus {
    pub present: bool,
    pub percent: i32,
    pub charging: bool,
    pub full: bool,
    pub pending: bool,
    pub seconds_left: i32,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BatteryDevice {
    pub path: String,
    pub native_path: String,
    pub kind: u32,
    pub present: bool,
    pub state: u32,
    pub percent: i32,
    pub time_to_empty_s: i32,
    pub time_to_full_s: i32,
}

impl BatteryDevice {
    pub fn is_battery(&self) -> bool {
        self.kind == UPOWER_TYPE_BATTERY || self.kind == UPOWER_TYPE_MONITOR
    }
}

fn percent(p: &Properties) -> i32 {
    value::<f64>(p, "Percentage").unwrap_or(0.0).round() as i32
}

pub fn battery_parse_status(p: &Properties) -> BatteryStatus {
    if !value::<bool>(p, "IsPresent").unwrap_or(false) {
        return BatteryStatus::default();
    }
    let state = value::<u32>(p, "State").unwrap_or(0);
    let charging = state == UPOWER_STATE_CHARGING;
    BatteryStatus {
        present: true,
        percent: percent(p),
        charging,
        full: state == UPOWER_STATE_FULLY_CHARGED,
        pending: state == UPOWER_STATE_PENDING_CHARGE,
        seconds_left: value::<i64>(p, if charging { "TimeToFull" } else { "TimeToEmpty" }).unwrap_or(0) as i32,
    }
}

pub fn battery_parse_device(path: &str, p: &Properties) -> BatteryDevice {
    BatteryDevice {
        path: path.to_string(),
        native_path: value::<String>(p, "NativePath").unwrap_or_default(),
        kind: value::<u32>(p, "Type").unwrap_or(0),
        present: value::<bool>(p, "IsPresent").unwrap_or(false),
        state: value::<u32>(p, "State").unwrap_or(0),
        percent: percent(p),
        time_to_empty_s: value::<i64>(p, "TimeToEmpty").unwrap_or(0) as i32,
        time_to_full_s: value::<i64>(p, "TimeToFull").unwrap_or(0) as i32,
    }
}

struct Tracked {
    /// `None` until the first read, so a device is never published half-known.
    device: Option<BatteryDevice>,
    refresh: Coalesce,
}

pub struct BatteryService {
    this: Weak<BatteryService>,
    bus: zbus::Connection,
    status: Cell<BatteryStatus>,
    on_battery: Cell<bool>,
    devices: RefCell<Vec<BatteryDevice>>,
    tracked: RefCell<BTreeMap<String, Tracked>>,
    display_refresh: Coalesce,
    manager_refresh: Coalesce,
    pub changed: Signal,
}

impl BatteryService {
    pub fn start(bus: &zbus::Connection) -> Rc<BatteryService> {
        let service = Rc::new_cyclic(|this| BatteryService {
            this: this.clone(),
            bus: bus.clone(),
            status: Cell::default(),
            on_battery: Cell::new(false),
            devices: RefCell::default(),
            tracked: RefCell::default(),
            display_refresh: Coalesce::default(),
            manager_refresh: Coalesce::default(),
            changed: Signal::default(),
        });
        runtime::spawn(listen(bus.clone(), Rc::downgrade(&service)));
        service
    }

    pub fn status(&self) -> BatteryStatus {
        self.status.get()
    }

    pub fn devices(&self) -> Ref<'_, Vec<BatteryDevice>> {
        self.devices.borrow()
    }

    pub fn on_battery(&self) -> bool {
        self.on_battery.get()
    }

    /// Runs `apply` with the properties of `path`, through `coalesce`.
    fn fetch(&self, coalesce: &Coalesce, path: String, interface: &'static str, apply: fn(&BatteryService, &str, &Properties)) {
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        coalesce.request(move || {
            let (weak, bus, path) = (weak.clone(), bus.clone(), path.clone());
            Box::pin(async move {
                match get_all(&bus, UPOWER_SERVICE, &path, interface).await {
                    Ok(p) => {
                        if let Some(service) = weak.upgrade() {
                            apply(&service, &path, &p);
                        }
                    }
                    Err(e) => ::log::error!("upower: {path}: {e}"),
                }
            })
        });
    }

    fn refresh_display(&self) {
        self.fetch(&self.display_refresh, DISPLAY_PATH.into(), DEVICE_INTERFACE, |service, _, p| {
            let next = battery_parse_status(p);
            if service.status.replace(next) != next {
                ::log::info!("battery: {next:?}");
                service.changed.emit(&());
            }
        });
    }

    fn refresh_manager(&self) {
        self.fetch(&self.manager_refresh, MANAGER_PATH.into(), MANAGER_INTERFACE, |service, _, p| {
            let next = value::<bool>(p, "OnBattery").unwrap_or(false);
            if service.on_battery.replace(next) != next {
                ::log::info!("battery: on battery {next}");
                service.changed.emit(&());
            }
        });
    }

    fn refresh_device(&self, path: &str) {
        let Some(coalesce) = self.tracked.borrow().get(path).map(|t| t.refresh.clone()) else { return };
        self.fetch(&coalesce, path.into(), DEVICE_INTERFACE, |service, path, p| {
            let next = battery_parse_device(path, p);
            {
                let mut tracked = service.tracked.borrow_mut();
                let Some(entry) = tracked.get_mut(path) else { return };
                if entry.device.as_ref() == Some(&next) {
                    return;
                }
                entry.device = Some(next);
            }
            service.publish_devices();
        });
    }

    fn add_device(&self, path: &str) {
        if self.tracked.borrow().contains_key(path) {
            return;
        }
        self.tracked.borrow_mut().insert(path.into(), Tracked { device: None, refresh: Coalesce::default() });
        self.refresh_device(path);
    }

    fn remove_device(&self, path: &str) {
        if self.tracked.borrow_mut().remove(path).is_some() {
            self.publish_devices();
        }
    }

    fn publish_devices(&self) {
        let next: Vec<BatteryDevice> = self.tracked.borrow().values().filter_map(|t| t.device.clone()).collect();
        if *self.devices.borrow() == next {
            return;
        }
        ::log::info!("battery: devices {:?}", next.iter().map(|d| (&d.native_path, d.percent)).collect::<Vec<_>>());
        *self.devices.borrow_mut() = next;
        self.changed.emit(&());
    }

    fn on_signal(&self, message: &zbus::Message) {
        let header = message.header();
        let (Some(path), Some(member)) = (header.path(), header.member()) else { return };
        match member.as_str() {
            "PropertiesChanged" if path.as_str() == DISPLAY_PATH => self.refresh_display(),
            "PropertiesChanged" if path.as_str() == MANAGER_PATH => self.refresh_manager(),
            "PropertiesChanged" => self.refresh_device(path.as_str()),
            "DeviceAdded" | "DeviceRemoved" => {
                let Ok(device) = message.body().deserialize::<OwnedObjectPath>() else { return };
                if member.as_str() == "DeviceAdded" {
                    self.add_device(device.as_str());
                } else {
                    self.remove_device(device.as_str());
                }
            }
            _ => {}
        }
    }
}

/// Subscribes to every UPower signal, then reads the initial state, so no change falls between.
async fn listen(bus: zbus::Connection, service: Weak<BatteryService>) {
    let rule = zbus::MatchRule::builder().msg_type(MessageType::Signal).sender(UPOWER_SERVICE).map(|b| b.build());
    let stream = match rule {
        Ok(rule) => zbus::MessageStream::for_match_rule(rule, &bus, None).await,
        Err(e) => Err(e),
    };
    let mut stream = match stream {
        Ok(stream) => stream,
        Err(e) => {
            ::log::error!("upower: cannot subscribe: {e}");
            return;
        }
    };
    {
        let Some(service) = service.upgrade() else { return };
        service.refresh_display();
        service.refresh_manager();
    }
    match zbus::Proxy::new(&bus, UPOWER_SERVICE, MANAGER_PATH, MANAGER_INTERFACE).await {
        Ok(manager) => match manager.call::<_, _, Vec<OwnedObjectPath>>("EnumerateDevices", &()).await {
            Ok(paths) => {
                let Some(service) = service.upgrade() else { return };
                for path in paths {
                    service.add_device(path.as_str());
                }
            }
            Err(e) => ::log::error!("upower: EnumerateDevices failed: {e}"),
        },
        Err(e) => ::log::error!("upower: {e}"),
    }
    while let Some(message) = stream.next().await {
        let Some(service) = service.upgrade() else { return };
        if let Ok(message) = message {
            service.on_signal(&message);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{connect_to, test_bus};
    use crate::service::runtime;
    use calloop::EventLoop;
    use futures_lite::future::block_on;
    use std::cell::Cell;
    use std::time::{Duration, Instant};
    use zbus::zvariant::{OwnedObjectPath, Value};

    fn props(entries: &[(&str, Value<'static>)]) -> Properties {
        entries.iter().map(|(k, v)| (k.to_string(), v.try_to_owned().unwrap())).collect()
    }

    #[test]
    fn battery_parse() {
        let charging = props(&[
            ("IsPresent", Value::from(true)),
            ("Percentage", Value::from(79.6)),
            ("State", Value::from(1u32)),
            ("TimeToFull", Value::from(1800i64)),
            ("TimeToEmpty", Value::from(0i64)),
        ]);
        let status = battery_parse_status(&charging);
        assert!(status.present && status.percent == 80 && status.charging && !status.full && !status.pending, "charging display device");
        assert_eq!(status.seconds_left, 1800, "charging reports time to full");
        let discharging = props(&[
            ("IsPresent", Value::from(true)),
            ("Percentage", Value::from(41.0)),
            ("State", Value::from(2u32)),
            ("TimeToFull", Value::from(0i64)),
            ("TimeToEmpty", Value::from(5400i64)),
        ]);
        let status = battery_parse_status(&discharging);
        assert!(!status.charging && status.seconds_left == 5400, "discharging reports time to empty");
        assert_eq!(battery_parse_status(&props(&[("IsPresent", Value::from(false))])), BatteryStatus::default(), "absent battery is the default status");
        assert_eq!(battery_parse_status(&Properties::new()), BatteryStatus::default(), "missing properties give the default status");

        let mouse = props(&[
            ("Type", Value::from(5u32)),
            ("IsPresent", Value::from(true)),
            ("Percentage", Value::from(12.0)),
            ("State", Value::from(2u32)),
            ("NativePath", Value::from("hid-1234")),
        ]);
        let device = battery_parse_device("/dev/mouse", &mouse);
        assert!(device.path == "/dev/mouse" && device.native_path == "hid-1234" && device.percent == 12, "device fields parse");
        assert!(!device.is_battery(), "a mouse is not a laptop battery");
        assert!(battery_parse_device("/b", &props(&[("Type", Value::from(2u32))])).is_battery(), "type 2 is a battery");
        assert!(battery_parse_device("/m", &props(&[("Type", Value::from(4u32))])).is_battery(), "type 4 is a monitor battery");
    }

    struct FakeDevice {
        percentage: f64,
    }

    #[zbus::interface(name = "org.freedesktop.UPower.Device")]
    impl FakeDevice {
        #[zbus(property)]
        fn is_present(&self) -> bool {
            true
        }
        #[zbus(property)]
        fn percentage(&self) -> f64 {
            self.percentage
        }
        #[zbus(property)]
        fn state(&self) -> u32 {
            2
        }
        #[zbus(property)]
        fn time_to_empty(&self) -> i64 {
            3600
        }
        #[zbus(property, name = "Type")]
        fn kind(&self) -> u32 {
            2
        }
    }

    struct FakeManager;

    #[zbus::interface(name = "org.freedesktop.UPower")]
    impl FakeManager {
        fn enumerate_devices(&self) -> Vec<OwnedObjectPath> {
            // The second device has no object, so its properties are never read.
            vec![
                OwnedObjectPath::try_from("/org/freedesktop/UPower/devices/battery_BAT0").unwrap(),
                OwnedObjectPath::try_from("/org/freedesktop/UPower/devices/ghost").unwrap(),
            ]
        }
        #[zbus(property)]
        fn on_battery(&self) -> bool {
            true
        }
    }

    fn pump(event_loop: &mut EventLoop<'static, ()>, done: impl Fn() -> bool) -> bool {
        let deadline = Instant::now() + Duration::from_secs(2);
        while !done() && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
        }
        done()
    }

    #[test]
    fn battery_follows_upower() {
        let Some(bus) = test_bus() else { return };
        let server = block_on(async {
            zbus::connection::Builder::address(bus.address())
                .unwrap()
                .name("org.freedesktop.UPower")
                .unwrap()
                .serve_at("/org/freedesktop/UPower", FakeManager)
                .unwrap()
                .serve_at(DISPLAY_PATH, FakeDevice { percentage: 40.0 })
                .unwrap()
                .serve_at("/org/freedesktop/UPower/devices/battery_BAT0", FakeDevice { percentage: 40.0 })
                .unwrap()
                .build()
                .await
                .unwrap()
        });
        let client = block_on(connect_to(bus.address()));
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let service = BatteryService::start(&client);
        let count = Rc::new(Cell::new(0));
        let c = count.clone();
        service.changed.connect(move |_| c.set(c.get() + 1));
        assert!(pump(&mut event_loop, || service.status().present && service.on_battery() && service.devices().len() == 1));
        let status = service.status();
        assert!(status.percent == 40 && !status.charging && status.seconds_left == 3600, "{status:?}");
        assert!(service.devices()[0].is_battery());
        event_loop.dispatch(Duration::from_millis(100), &mut ()).unwrap();
        assert_eq!(service.devices().len(), 1, "a device whose properties were never read is not published");
        let settled = count.get();

        block_on(async {
            let iface = server.object_server().interface::<_, FakeDevice>(DISPLAY_PATH).await.unwrap();
            iface.get_mut().await.percentage = 41.0;
            iface.get().await.percentage_changed(iface.signal_emitter()).await.unwrap();
        });
        assert!(pump(&mut event_loop, || service.status().percent == 41), "a PropertiesChanged refreshes the status");
        event_loop.dispatch(Duration::from_millis(100), &mut ()).unwrap();
        assert_eq!(count.get(), settled + 1, "one change fires changed once");
    }
}
