use std::cell::Cell;
use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::rc::Rc;

use zbus::zvariant::OwnedValue;

pub type Properties = HashMap<String, OwnedValue>;
pub type LocalBoxFuture<T> = Pin<Box<dyn Future<Output = T>>>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BusKind {
    System,
    Session,
}

pub async fn connect(kind: BusKind) -> zbus::Result<zbus::Connection> {
    match kind {
        BusKind::System => zbus::Connection::system().await,
        BusKind::Session => zbus::Connection::session().await,
    }
}

/// `org.freedesktop.DBus.Properties.GetAll`, awaited, never blocking the caller's thread.
pub async fn get_all(conn: &zbus::Connection, dest: &str, path: &str, interface: &str) -> zbus::Result<Properties> {
    let proxy = zbus::fdo::PropertiesProxy::builder(conn)
        .destination(dest.to_owned())?
        .path(path.to_owned())?
        .cache_properties(zbus::proxy::CacheProperties::No)
        .build()
        .await?;
    let interface = zbus::names::InterfaceName::try_from(interface.to_owned())?;
    Ok(proxy.get_all(interface).await?)
}

/// The property converted to `T`; `None` when missing or of another type.
pub fn value<T: TryFrom<OwnedValue>>(p: &Properties, name: &str) -> Option<T> {
    T::try_from(p.get(name)?.try_clone().ok()?).ok()
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Ownership {
    Owned,
    /// Another process holds the name; we neither queue for it nor replace it.
    Taken,
    Failed,
}

/// Requests `name` without queueing or replacing; a refusal because of another owner is `Taken`.
pub async fn own_name(conn: &zbus::Connection, name: &str) -> Ownership {
    use zbus::fdo::{RequestNameFlags, RequestNameReply};
    match conn.request_name_with_flags(name, RequestNameFlags::DoNotQueue.into()).await {
        Ok(RequestNameReply::PrimaryOwner | RequestNameReply::AlreadyOwner) => Ownership::Owned,
        Ok(RequestNameReply::Exists | RequestNameReply::InQueue) | Err(zbus::Error::NameTaken) => Ownership::Taken,
        Err(e) => {
            ::log::error!("dbus: cannot own {name}: {e}");
            Ownership::Failed
        }
    }
}

/// One refresh in flight; requests during it fold into exactly one more run after it.
#[derive(Clone, Default)]
pub struct Coalesce {
    in_flight: Rc<Cell<bool>>,
    repeat: Rc<Cell<bool>>,
}

impl Coalesce {
    pub fn request(&self, run: impl Fn() -> LocalBoxFuture<()> + 'static) {
        if self.in_flight.replace(true) {
            self.repeat.set(true);
            return;
        }
        let (in_flight, repeat) = (self.in_flight.clone(), self.repeat.clone());
        crate::service::runtime::spawn(async move {
            loop {
                run().await;
                if !repeat.replace(false) {
                    break;
                }
            }
            in_flight.set(false);
        });
    }
}

/// A private `dbus-daemon`, killed on drop.
#[cfg(test)]
pub struct TestBus {
    child: std::process::Child,
    address: String,
}

#[cfg(test)]
impl TestBus {
    pub fn address(&self) -> &str {
        &self.address
    }
}

#[cfg(test)]
impl Drop for TestBus {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

/// `None` when `dbus-daemon` is not installed.
#[cfg(test)]
pub fn test_bus() -> Option<TestBus> {
    use std::io::BufRead;
    let mut child = std::process::Command::new("dbus-daemon")
        .args(["--session", "--print-address", "--nofork"])
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .spawn()
        .ok()?;
    let mut address = String::new();
    std::io::BufReader::new(child.stdout.take()?).read_line(&mut address).ok()?;
    Some(TestBus { child, address: address.trim().to_string() })
}

#[cfg(test)]
pub async fn connect_to(address: &str) -> zbus::Connection {
    zbus::connection::Builder::address(address).unwrap().build().await.unwrap()
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_lite::future::block_on;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    struct Sample;

    #[zbus::interface(name = "org.astralia.Test1")]
    impl Sample {
        #[zbus(property)]
        fn label(&self) -> String {
            "hello".into()
        }

        #[zbus(property)]
        fn count(&self) -> u32 {
            7
        }
    }

    fn served(bus: &TestBus) -> (zbus::Connection, zbus::Connection) {
        block_on(async {
            let server = zbus::connection::Builder::address(bus.address())
                .unwrap()
                .name("org.astralia.Test")
                .unwrap()
                .serve_at("/test", Sample)
                .unwrap()
                .build()
                .await
                .unwrap();
            (server, connect_to(bus.address()).await)
        })
    }

    #[test]
    fn get_all_reads_properties() {
        let Some(bus) = test_bus() else { return };
        let (_server, client) = served(&bus);
        let props = block_on(get_all(&client, "org.astralia.Test", "/test", "org.astralia.Test1")).unwrap();
        assert_eq!(value::<String>(&props, "Label").as_deref(), Some("hello"));
        assert_eq!(value::<u32>(&props, "Count"), Some(7));
    }

    #[test]
    fn value_wrong_type_is_none() {
        let Some(bus) = test_bus() else { return };
        let (_server, client) = served(&bus);
        let props = block_on(get_all(&client, "org.astralia.Test", "/test", "org.astralia.Test1")).unwrap();
        assert_eq!(value::<String>(&props, "Count"), None);
        assert_eq!(value::<u32>(&props, "Missing"), None);
    }

    #[test]
    fn own_name_tells_taken_from_failed() {
        let Some(bus) = test_bus() else { return };
        let (a, b) = block_on(async { (connect_to(bus.address()).await, connect_to(bus.address()).await) });
        assert_eq!(block_on(own_name(&a, "org.astralia.Owned")), Ownership::Owned);
        assert_eq!(block_on(own_name(&b, "org.astralia.Owned")), Ownership::Taken, "another owner reads as taken, not as a failure");
    }

    #[test]
    fn coalesce_runs_at_most_twice() {
        let mut event_loop: calloop::EventLoop<'static, ()> = calloop::EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let coalesce = Coalesce::default();
        let runs = Rc::new(Cell::new(0));
        for _ in 0..10 {
            let runs = runs.clone();
            coalesce.request(move || {
                let runs = runs.clone();
                Box::pin(async move {
                    async_io::Timer::after(Duration::from_millis(30)).await;
                    runs.set(runs.get() + 1);
                })
            });
        }
        let deadline = Instant::now() + Duration::from_secs(1);
        while Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
        }
        assert_eq!(runs.get(), 2);
        let r = runs.clone();
        coalesce.request(move || {
            let r = r.clone();
            Box::pin(async move { r.set(r.get() + 1) })
        });
        event_loop.dispatch(Duration::from_millis(50), &mut ()).unwrap();
        assert_eq!(runs.get(), 3, "a request after the burst runs again");
    }
}
