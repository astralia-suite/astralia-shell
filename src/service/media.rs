use std::cell::{Cell, Ref, RefCell};
use std::collections::HashMap;
use std::rc::{Rc, Weak};

use futures_lite::StreamExt;
use futures_lite::stream;
use zbus::message::Type as MessageType;
use zbus::zvariant::OwnedValue;

use crate::base::dbus::{Coalesce, value};
use crate::base::signal::Signal;
use crate::service::runtime;

const PLAYER_PATH: &str = "/org/mpris/MediaPlayer2";
const PLAYER_INTERFACE: &str = "org.mpris.MediaPlayer2.Player";
const NAME_PREFIX: &str = "org.mpris.MediaPlayer2.";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum MediaPlayback {
    #[default]
    Stopped,
    Paused,
    Playing,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MediaTrack {
    pub title: String,
    pub artist: String,
    pub art_url: String,
    pub length_us: i64,
    pub position_us: i64,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MediaStatus {
    pub has_player: bool,
    pub bus_name: String,
    pub playback: MediaPlayback,
    pub track: MediaTrack,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaPlayer {
    pub bus_name: String,
    pub playback: MediaPlayback,
}

pub fn media_parse_playback(status: &str) -> MediaPlayback {
    match status {
        "Playing" => MediaPlayback::Playing,
        "Paused" => MediaPlayback::Paused,
        _ => MediaPlayback::Stopped,
    }
}

pub fn media_format_position(position_us: i64) -> String {
    let seconds = position_us.max(0) / 1_000_000;
    format!("{}:{:02}", seconds / 60, seconds % 60)
}

pub fn media_is_local_art(url: &str) -> bool {
    url.starts_with("file://")
}

/// The first playing player, else the first one.
pub fn media_select_player(players: &[MediaPlayer]) -> Option<usize> {
    players.iter().position(|p| p.playback == MediaPlayback::Playing).or(if players.is_empty() { None } else { Some(0) })
}

fn parse_metadata(metadata: &HashMap<String, OwnedValue>) -> MediaTrack {
    MediaTrack {
        title: value::<String>(metadata, "xesam:title").unwrap_or_default(),
        artist: value::<Vec<String>>(metadata, "xesam:artist").and_then(|a| a.into_iter().next()).unwrap_or_default(),
        art_url: value::<String>(metadata, "mpris:artUrl").unwrap_or_default(),
        length_us: value::<i64>(metadata, "mpris:length").unwrap_or(0),
        position_us: 0,
    }
}

/// The status with the position cleared, for deciding what is worth a log line.
fn without_position(status: &MediaStatus) -> MediaStatus {
    let mut status = status.clone();
    status.track.position_us = 0;
    status
}

async fn player_property(bus: &zbus::Connection, name: &str, property: &str) -> zbus::Result<OwnedValue> {
    let proxy = zbus::fdo::PropertiesProxy::builder(bus).destination(name.to_owned())?.path(PLAYER_PATH)?.build().await?;
    Ok(proxy.get(zbus::names::InterfaceName::try_from(PLAYER_INTERFACE)?, property).await?)
}

pub struct MediaService {
    this: Weak<MediaService>,
    bus: zbus::Connection,
    status: RefCell<MediaStatus>,
    scan: Coalesce,
    position_inflight: Cell<bool>,
    pub changed: Signal,
}

impl MediaService {
    pub fn start(bus: &zbus::Connection) -> Rc<MediaService> {
        let service = Rc::new_cyclic(|this| MediaService {
            this: this.clone(),
            bus: bus.clone(),
            status: RefCell::default(),
            scan: Coalesce::default(),
            position_inflight: Cell::new(false),
            changed: Signal::default(),
        });
        runtime::spawn(listen(bus.clone(), Rc::downgrade(&service)));
        service
    }

    pub fn status(&self) -> Ref<'_, MediaStatus> {
        self.status.borrow()
    }

    /// Lists the players, reads each one's playback, then the chosen one's metadata.
    fn rescan(&self) {
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        self.scan.request(move || {
            let (weak, bus) = (weak.clone(), bus.clone());
            Box::pin(async move {
                let names = async { Ok::<_, zbus::Error>(zbus::fdo::DBusProxy::new(&bus).await?.list_names().await?) };
                let names = match names.await {
                    Ok(names) => names,
                    Err(e) => return ::log::error!("media: cannot list players: {e}"),
                };
                let mut live: Vec<String> = names.into_iter().map(|n| n.to_string()).filter(|n| n.starts_with(NAME_PREFIX)).collect();
                live.sort();
                let mut candidates = Vec::new();
                for name in live {
                    let playback = player_property(&bus, &name, "PlaybackStatus").await.ok().and_then(|v| String::try_from(v).ok());
                    candidates.push(MediaPlayer { playback: media_parse_playback(&playback.unwrap_or_default()), bus_name: name });
                }
                let Some(service) = weak.upgrade() else { return };
                if !candidates.iter().any(|c| c.bus_name == service.status.borrow().bus_name) {
                    service.position_inflight.set(false);
                }
                let Some(pick) = media_select_player(&candidates).map(|i| candidates.swap_remove(i)) else {
                    return service.update(MediaStatus::default());
                };
                drop(service);
                let metadata = player_property(&bus, &pick.bus_name, "Metadata").await.ok().and_then(|v| HashMap::<String, OwnedValue>::try_from(v).ok());
                let Some(service) = weak.upgrade() else { return };
                let mut next = MediaStatus { has_player: true, bus_name: pick.bus_name, playback: pick.playback, track: metadata.as_ref().map(parse_metadata).unwrap_or_default() };
                {
                    let current = service.status.borrow();
                    if current.bus_name == next.bus_name && current.track.title == next.track.title {
                        next.track.position_us = current.track.position_us;
                    }
                }
                service.update(next);
            })
        });
    }

    fn update(&self, next: MediaStatus) {
        if *self.status.borrow() == next {
            return;
        }
        if without_position(&self.status.borrow()) != without_position(&next) {
            ::log::info!("media: {:?}", without_position(&next));
        }
        *self.status.borrow_mut() = next;
        self.changed.emit(&());
    }

    fn call(&self, method: &'static str) {
        let status = self.status.borrow();
        if !status.has_player {
            return;
        }
        let (bus, name) = (self.bus.clone(), status.bus_name.clone());
        runtime::spawn(async move {
            let result = async { zbus::Proxy::new(&bus, name, PLAYER_PATH, PLAYER_INTERFACE).await?.call_method(method, &()).await };
            if let Err(e) = result.await {
                ::log::error!("media: {method} failed: {e}");
            }
        });
    }

    pub fn play_pause(&self) {
        self.call("PlayPause");
    }

    pub fn next(&self) {
        self.call("Next");
    }

    pub fn previous(&self) {
        self.call("Previous");
    }

    /// One position read at a time; readers call this on their own cadence.
    pub fn poll_position(&self) {
        let name = {
            let status = self.status.borrow();
            if !status.has_player || self.position_inflight.get() {
                return;
            }
            status.bus_name.clone()
        };
        self.position_inflight.set(true);
        let (weak, bus) = (self.this.clone(), self.bus.clone());
        runtime::spawn(async move {
            let position = player_property(&bus, &name, "Position").await.ok().and_then(|v| i64::try_from(v).ok());
            let Some(service) = weak.upgrade() else { return };
            service.position_inflight.set(false);
            let Some(position) = position.filter(|_| service.status.borrow().bus_name == name) else { return };
            let mut next = service.status.borrow().clone();
            next.track.position_us = position;
            service.update(next);
        });
    }
}

/// Rescans when an MPRIS name comes or goes, or a player's properties change.
async fn listen(bus: zbus::Connection, service: Weak<MediaService>) {
    let owners = zbus::MatchRule::builder()
        .msg_type(MessageType::Signal)
        .sender("org.freedesktop.DBus")
        .and_then(|b| b.member("NameOwnerChanged"))
        .and_then(|b| b.arg0ns("org.mpris.MediaPlayer2"))
        .map(|b| b.build());
    let properties = zbus::MatchRule::builder()
        .msg_type(MessageType::Signal)
        .interface("org.freedesktop.DBus.Properties")
        .and_then(|b| b.member("PropertiesChanged"))
        .and_then(|b| b.path(PLAYER_PATH))
        .and_then(|b| b.arg(0, PLAYER_INTERFACE))
        .map(|b| b.build());
    let streams = async {
        let owners = zbus::MessageStream::for_match_rule(owners?, &bus, None).await?;
        let properties = zbus::MessageStream::for_match_rule(properties?, &bus, None).await?;
        Ok::<_, zbus::Error>(stream::or(owners, properties))
    };
    let mut signals = match streams.await {
        Ok(signals) => signals,
        Err(e) => {
            ::log::error!("media: cannot subscribe: {e}");
            return;
        }
    };
    match service.upgrade() {
        Some(service) => service.rescan(),
        None => return,
    }
    while signals.next().await.is_some() {
        let Some(service) = service.upgrade() else { return };
        service.rescan();
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

    #[test]
    fn media_rules() {
        assert_eq!(media_parse_playback("Playing"), MediaPlayback::Playing);
        assert_eq!(media_parse_playback("Paused"), MediaPlayback::Paused);
        assert_eq!(media_parse_playback("bogus"), MediaPlayback::Stopped, "unknown is stopped");
        assert_eq!(media_format_position(0), "0:00", "zero position");
        assert_eq!(media_format_position(65_000_000), "1:05", "pads seconds");
        assert_eq!(media_format_position(-5), "0:00", "negative clamps");
        assert!(media_is_local_art("file:///tmp/a.png") && !media_is_local_art("https://x/a.png") && !media_is_local_art(""), "only file URLs are local art");
        let player = |name: &str, playback| MediaPlayer { bus_name: name.into(), playback };
        assert_eq!(media_select_player(&[]), None, "no players");
        assert_eq!(media_select_player(&[player("a", MediaPlayback::Paused), player("b", MediaPlayback::Playing)]), Some(1), "playing wins");
        assert_eq!(media_select_player(&[player("a", MediaPlayback::Paused), player("b", MediaPlayback::Stopped)]), Some(0), "else the first");
    }

    struct FakePlayer {
        status: &'static str,
        title: &'static str,
    }

    #[zbus::interface(name = "org.mpris.MediaPlayer2.Player")]
    impl FakePlayer {
        #[zbus(property)]
        fn playback_status(&self) -> String {
            self.status.into()
        }
        #[zbus(property)]
        fn metadata(&self) -> HashMap<String, OwnedValue> {
            [
                ("xesam:title".to_string(), Value::from(self.title).try_to_owned().unwrap()),
                ("xesam:artist".to_string(), Value::from(vec!["Artist"]).try_to_owned().unwrap()),
                ("mpris:length".to_string(), Value::from(180_000_000i64).try_to_owned().unwrap()),
            ]
            .into()
        }
        #[zbus(property)]
        fn position(&self) -> i64 {
            42_000_000
        }
    }

    fn player(bus: &str, name: &str, status: &'static str, title: &'static str) -> zbus::Connection {
        block_on(async {
            zbus::connection::Builder::address(bus).unwrap().name(name).unwrap().serve_at(PLAYER_PATH, FakePlayer { status, title }).unwrap().build().await.unwrap()
        })
    }

    #[test]
    fn media_picks_playing_player() {
        let Some(bus) = test_bus() else { return };
        let _paused = player(bus.address(), "org.mpris.MediaPlayer2.a", "Paused", "Quiet");
        let _playing = player(bus.address(), "org.mpris.MediaPlayer2.b", "Playing", "Loud");
        let conn = block_on(connect_to(bus.address()));
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let service = MediaService::start(&conn);
        let mut pump = |done: &dyn Fn() -> bool| {
            let deadline = Instant::now() + Duration::from_secs(2);
            while !done() && Instant::now() < deadline {
                event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
            }
            done()
        };
        assert!(pump(&|| service.status().has_player));
        let status = service.status().clone();
        assert!(status.bus_name == "org.mpris.MediaPlayer2.b" && status.playback == MediaPlayback::Playing, "{status:?}");
        assert!(status.track.title == "Loud" && status.track.artist == "Artist" && status.track.length_us == 180_000_000);
        service.poll_position();
        assert!(pump(&|| service.status().track.position_us == 42_000_000), "a position poll updates the track");
        drop(_playing);
        assert!(pump(&|| service.status().bus_name == "org.mpris.MediaPlayer2.a"), "the next player takes over when the playing one leaves");
    }
}
