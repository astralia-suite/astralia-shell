use std::cell::{Cell, Ref, RefCell};
use std::collections::HashMap;
use std::rc::{Rc, Weak};
use std::time::{Duration, Instant};

use async_io::Timer;
use zbus::zvariant::OwnedValue;

use crate::base::dbus::{Ownership, own_name};
use crate::base::process::spawn_detached;
use crate::base::signal::Signal;
use crate::service::runtime;

pub const NOTIFICATION_HANG_TIME: Duration = Duration::from_millis(5000);
pub const NOTIFICATION_URGENCY_NORMAL: u8 = 1;
pub const NOTIFICATION_URGENCY_CRITICAL: u8 = 2;

const BUS_NAME: &str = "org.freedesktop.Notifications";
const OBJECT_PATH: &str = "/org/freedesktop/Notifications";
const INTERFACE: &str = "org.freedesktop.Notifications";

const REASON_EXPIRED: u32 = 1;
const REASON_DISMISSED: u32 = 2;
const REASON_CLOSED: u32 = 3;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Notification {
    pub id: u32,
    pub app: String,
    pub summary: String,
    pub body: String,
    pub urgency: u8,
    pub timeout: Duration,
    pub created: Instant,
    pub deadline: Instant,
    pub click_command: String,
}

impl Notification {
    #[cfg(test)]
    pub fn new(id: u32, now: Instant) -> Notification {
        Notification {
            id,
            app: String::new(),
            summary: String::new(),
            body: String::new(),
            urgency: NOTIFICATION_URGENCY_NORMAL,
            timeout: NOTIFICATION_HANG_TIME,
            created: now,
            deadline: now.checked_add(NOTIFICATION_HANG_TIME).unwrap_or(now),
            click_command: String::new(),
        }
    }

    #[cfg(test)]
    pub fn critical(&self) -> bool {
        self.urgency == NOTIFICATION_URGENCY_CRITICAL
    }
}

pub fn notification_timeout(expire_timeout_ms: i32) -> Duration {
    if expire_timeout_ms > 0 { Duration::from_millis(expire_timeout_ms as u64) } else { NOTIFICATION_HANG_TIME }
}

struct Incoming {
    app: String,
    replaces_id: u32,
    summary: String,
    body: String,
    urgency: u8,
    expire_timeout: i32,
}

enum ServerCall {
    Notify(Incoming, async_channel::Sender<u32>),
    Close(u32),
}

/// The exported `org.freedesktop.Notifications`; it runs on zbus's thread and forwards to the loop thread.
struct Server {
    calls: async_channel::Sender<ServerCall>,
}

#[zbus::interface(name = "org.freedesktop.Notifications")]
impl Server {
    #[allow(clippy::too_many_arguments)]
    async fn notify(
        &self,
        app_name: String,
        replaces_id: u32,
        _app_icon: String,
        summary: String,
        body: String,
        _actions: Vec<String>,
        hints: HashMap<String, OwnedValue>,
        expire_timeout: i32,
    ) -> u32 {
        let urgency = hints.get("urgency").and_then(|v| v.try_clone().ok()).and_then(|v| u8::try_from(v).ok()).unwrap_or(NOTIFICATION_URGENCY_NORMAL);
        let incoming = Incoming { app: app_name, replaces_id, summary, body, urgency, expire_timeout };
        let (reply, id) = async_channel::bounded(1);
        if self.calls.send(ServerCall::Notify(incoming, reply)).await.is_err() {
            return 0;
        }
        id.recv().await.unwrap_or(0)
    }

    async fn close_notification(&self, id: u32) {
        let _ = self.calls.send(ServerCall::Close(id)).await;
    }

    fn get_capabilities(&self) -> Vec<String> {
        vec!["body".into()]
    }

    fn get_server_information(&self) -> (String, String, String, String) {
        ("astralia".into(), "astralia".into(), "0.1.0".into(), "1.2".into())
    }
}

pub struct NotificationService {
    this: Weak<NotificationService>,
    bus: RefCell<Option<zbus::Connection>>,
    owns_name: Cell<bool>,
    list: RefCell<Vec<Notification>>,
    next_id: Cell<u32>,
    /// Bumped whenever the earliest deadline may have moved; a timer task runs while its value is current.
    timer_generation: Cell<u64>,
    pub changed: Signal,
}

impl NotificationService {
    fn new() -> Rc<NotificationService> {
        Rc::new_cyclic(|this| NotificationService {
            this: this.clone(),
            bus: RefCell::new(None),
            owns_name: Cell::new(false),
            list: RefCell::default(),
            next_id: Cell::new(1),
            timer_generation: Cell::new(0),
            changed: Signal::default(),
        })
    }

    /// Local posts work without a bus; with one, the service also serves `org.freedesktop.Notifications`.
    pub fn start(session_bus: Option<&zbus::Connection>) -> Rc<NotificationService> {
        let service = NotificationService::new();
        if let Some(bus) = session_bus {
            let (weak, bus) = (Rc::downgrade(&service), bus.clone());
            runtime::spawn(async move {
                if let Some(service) = weak.upgrade() {
                    service.serve(bus).await;
                }
            });
        }
        service
    }

    #[cfg(test)]
    pub async fn start_on(bus: &zbus::Connection) -> Rc<NotificationService> {
        let service = NotificationService::new();
        service.serve(bus.clone()).await;
        service
    }

    /// Exports the server, then asks for the name without queueing or replacing another owner.
    async fn serve(&self, bus: zbus::Connection) {
        let (calls, incoming) = async_channel::unbounded();
        if let Err(e) = bus.object_server().at(OBJECT_PATH, Server { calls }).await {
            ::log::error!("notifications: cannot export {OBJECT_PATH}: {e}");
            return;
        }
        match own_name(&bus, BUS_NAME).await {
            Ownership::Owned => {}
            ownership => {
                if ownership == Ownership::Taken {
                    ::log::info!("notifications: {BUS_NAME} is owned by another process");
                }
                let _ = bus.object_server().remove::<Server, _>(OBJECT_PATH).await;
                return;
            }
        }
        ::log::info!("notifications: serving {BUS_NAME}");
        self.owns_name.set(true);
        *self.bus.borrow_mut() = Some(bus);
        let weak = self.this.clone();
        runtime::spawn(async move {
            while let Ok(call) = incoming.recv().await {
                let Some(service) = weak.upgrade() else { return };
                match call {
                    ServerCall::Notify(incoming, reply) => {
                        let _ = reply.try_send(service.notify(incoming, String::new()));
                    }
                    ServerCall::Close(id) => service.close(id, REASON_CLOSED),
                }
            }
        });
    }

    #[cfg(test)]
    pub fn owns_name(&self) -> bool {
        self.owns_name.get()
    }

    pub fn list(&self) -> Ref<'_, Vec<Notification>> {
        self.list.borrow()
    }

    #[cfg(test)]
    pub fn dismiss(&self, id: u32) {
        self.close(id, REASON_DISMISSED);
    }

    pub fn click(&self, id: u32) {
        let command = self.list.borrow().iter().find(|n| n.id == id).map(|n| n.click_command.clone());
        let Some(command) = command.filter(|c| !c.is_empty()) else { return };
        spawn_detached(&command);
        self.close(id, REASON_DISMISSED);
    }

    pub fn post(&self, app: &str, summary: &str, body: &str, expire_timeout: i32, click_command: String) -> u32 {
        let incoming = Incoming { app: app.into(), replaces_id: 0, summary: summary.into(), body: body.into(), urgency: NOTIFICATION_URGENCY_NORMAL, expire_timeout };
        self.notify(incoming, click_command)
    }

    fn notify(&self, incoming: Incoming, click_command: String) -> u32 {
        let timeout = notification_timeout(incoming.expire_timeout);
        let now = Instant::now();
        let mut entry = Notification {
            id: 0,
            app: incoming.app,
            summary: incoming.summary,
            body: incoming.body,
            urgency: incoming.urgency,
            timeout,
            created: now,
            deadline: now.checked_add(timeout).unwrap_or(now),
            click_command,
        };
        let replaces = incoming.replaces_id;
        let replaced = replaces != 0 && self.list.borrow().iter().any(|n| n.id == replaces);
        if replaced {
            entry.id = replaces;
        } else {
            entry.id = self.next_id.get();
            self.next_id.set(entry.id.wrapping_add(1).max(1));
        }
        ::log::info!("notifications: {} {}: {}", entry.id, entry.app, entry.summary);
        let id = entry.id;
        {
            let mut list = self.list.borrow_mut();
            match list.iter_mut().find(|n| n.id == id) {
                Some(slot) => *slot = entry,
                None => list.push(entry),
            }
        }
        self.reschedule();
        self.changed.emit(&());
        id
    }

    fn close(&self, id: u32, reason: u32) {
        {
            let mut list = self.list.borrow_mut();
            let Some(at) = list.iter().position(|n| n.id == id) else { return };
            list.remove(at);
        }
        ::log::info!("notifications: {id} closed");
        if let Some(bus) = self.bus.borrow().clone() {
            runtime::spawn(async move {
                if let Err(e) = bus.emit_signal(None::<()>, OBJECT_PATH, INTERFACE, "NotificationClosed", &(id, reason)).await {
                    ::log::error!("notifications: cannot emit NotificationClosed: {e}");
                }
            });
        }
        self.reschedule();
        self.changed.emit(&());
    }

    /// One timer at the earliest deadline, whatever the list order.
    fn reschedule(&self) {
        let generation = self.timer_generation.get() + 1;
        self.timer_generation.set(generation);
        let Some(earliest) = self.list.borrow().iter().map(|n| n.deadline).min() else { return };
        let weak = self.this.clone();
        runtime::spawn(async move {
            Timer::at(earliest).await;
            if let Some(service) = weak.upgrade().filter(|s| s.timer_generation.get() == generation) {
                service.expire();
            }
        });
    }

    fn expire(&self) {
        let now = Instant::now();
        let expired: Vec<u32> = self.list.borrow().iter().filter(|n| n.deadline <= now).map(|n| n.id).collect();
        for id in expired {
            self.close(id, REASON_EXPIRED);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{connect_to, test_bus};
    use calloop::EventLoop;
    use futures_lite::future::block_on;
    use std::sync::mpsc;
    use std::time::Instant;

    fn pump(event_loop: &mut EventLoop<'static, ()>, done: impl Fn() -> bool) -> bool {
        let deadline = Instant::now() + Duration::from_secs(2);
        while !done() && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
        }
        done()
    }

    #[test]
    fn notification_rules() {
        assert_eq!(notification_timeout(-1), NOTIFICATION_HANG_TIME, "default expiry uses the hang time");
        assert_eq!(notification_timeout(0), NOTIFICATION_HANG_TIME, "zero expiry uses the hang time");
        assert_eq!(notification_timeout(1500), Duration::from_millis(1500), "an explicit expiry is honored");
        let now = Instant::now();
        let critical = Notification { urgency: NOTIFICATION_URGENCY_CRITICAL, ..Notification::new(1, now) };
        assert!(critical.critical() && !Notification::new(2, now).critical(), "urgency 2 is critical");
    }

    #[test]
    fn notification_expires_and_replaces() {
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let service = NotificationService::start(None);
        let long = service.post("a", "long", "", 5000, String::new());
        let short = service.post("b", "short", "", 50, String::new());
        assert_eq!((long, short), (1, 2));
        assert!(pump(&mut event_loop, || service.list().len() == 1), "the earliest deadline expires first, whatever the list order");
        assert_eq!(service.list()[0].id, long);
        service.dismiss(long);
        assert!(service.list().is_empty());
    }

    #[test]
    fn notification_served_over_dbus() {
        let Some(bus) = test_bus() else { return };
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let conn = block_on(connect_to(bus.address()));
        let service = block_on(NotificationService::start_on(&conn));
        assert!(service.owns_name());
        let (id_tx, id_rx) = mpsc::channel();
        let (go_tx, go_rx) = mpsc::channel::<()>();
        let address = bus.address().to_string();
        let client = std::thread::spawn(move || {
            block_on(async {
                let client = connect_to(&address).await;
                let rule = zbus::MatchRule::builder().msg_type(zbus::message::Type::Signal).member("NotificationClosed").unwrap().build();
                let mut closed = zbus::MessageStream::for_match_rule(rule, &client, None).await.unwrap();
                let proxy = zbus::Proxy::new(&client, BUS_NAME, OBJECT_PATH, INTERFACE).await.unwrap();
                let hints: HashMap<&str, zbus::zvariant::Value> = [("urgency", zbus::zvariant::Value::from(2u8))].into();
                let id: u32 = proxy.call("Notify", &("app", 0u32, "", "hello", "body", Vec::<&str>::new(), hints, -1i32)).await.unwrap();
                id_tx.send(id).unwrap();
                go_rx.recv().unwrap();
                proxy.call_method("CloseNotification", &(id,)).await.unwrap();
                let message = futures_lite::StreamExt::next(&mut closed).await.unwrap().unwrap();
                message.body().deserialize::<(u32, u32)>().unwrap()
            })
        });
        assert!(pump(&mut event_loop, || !service.list().is_empty()));
        assert_eq!(id_rx.recv_timeout(Duration::from_secs(2)).unwrap(), 1, "Notify returns the new id");
        let first = service.list()[0].clone();
        assert!(first.app == "app" && first.summary == "hello" && first.critical(), "a client Notify appears in list()");
        go_tx.send(()).unwrap();
        assert!(pump(&mut event_loop, || service.list().is_empty()), "CloseNotification removes it");
        let deadline = Instant::now() + Duration::from_secs(2);
        while !client.is_finished() && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
        }
        assert_eq!(client.join().unwrap(), (1, 3), "NotificationClosed carries the id and reason closed");
    }

    #[test]
    fn notification_name_taken_is_logged() {
        let Some(bus) = test_bus() else { return };
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        // first owner takes the name, the second service must start without it and not panic
        let (a, b) = block_on(async { (connect_to(bus.address()).await, connect_to(bus.address()).await) });
        block_on(a.request_name("org.freedesktop.Notifications")).unwrap();
        let svc = block_on(NotificationService::start_on(&b));
        assert!(!svc.owns_name());
        assert_eq!(svc.post("t", "s", "b", -1, String::new()), 1); // local posts still work
    }
}
