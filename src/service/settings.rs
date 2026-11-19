use std::cell::{Ref, RefCell};
use std::io;
use std::os::fd::OwnedFd;
use std::path::Path;
use std::rc::{Rc, Weak};

use async_io::Async;

use crate::base::inotify;
use crate::base::signal::Signal;
use crate::config::file::{read_text_file, write_text_file};
use crate::config::settings::{Config, config_path, parse_config, serialize_config};
use crate::service::runtime;

pub struct SettingsService {
    path: String,
    config: RefCell<Config>,
    pub changed: Signal,
}

impl SettingsService {
    pub fn start() -> Rc<SettingsService> {
        SettingsService::start_with(config_path())
    }

    /// Loads `path` and reloads it whenever it is written, renamed over or removed.
    pub fn start_with(path: String) -> Rc<SettingsService> {
        let config = read_text_file(&path).map(|text| parse_config(&text)).unwrap_or_default();
        let service = Rc::new(SettingsService { path, config: RefCell::new(config), changed: Signal::default() });
        match watch(Path::new(&service.path)) {
            Ok((fd, name)) => runtime::spawn(watch_loop(fd, name, Rc::downgrade(&service))),
            Err(e) => ::log::error!("settings: cannot watch {}: {e}", service.path),
        }
        service
    }

    pub fn config(&self) -> Ref<'_, Config> {
        self.config.borrow()
    }

    /// Applies `edit` to the config as it is on disk now (a change not yet seen by the watch is kept),
    /// writes the result and fires `changed` once. The watch's reload of our own write finds no
    /// difference and fires nothing.
    pub fn update(&self, edit: impl FnOnce(&mut Config)) {
        let base = read_text_file(&self.path).map(|text| parse_config(&text)).unwrap_or_default();
        let mut next = base.clone();
        edit(&mut next);
        if next != base && !write_text_file(&self.path, &serialize_config(&next)) {
            return;
        }
        if next == *self.config.borrow() {
            return;
        }
        *self.config.borrow_mut() = next;
        self.changed.emit(&());
    }

    fn reload(&self) {
        let next = match read_text_file(&self.path) {
            Some(text) => parse_config(&text),
            None => {
                ::log::info!("settings: config removed, using defaults");
                Config::default()
            }
        };
        if next == *self.config.borrow() {
            return;
        }
        ::log::info!("settings: reloaded {}", self.path);
        *self.config.borrow_mut() = next;
        self.changed.emit(&());
    }
}

/// Where a module reads the config and sends its edits.
pub trait ConfigStore {
    fn config(&self) -> Ref<'_, Config>;
    fn update(&self, edit: &mut dyn FnMut(&mut Config));
}

impl ConfigStore for SettingsService {
    fn config(&self) -> Ref<'_, Config> {
        SettingsService::config(self)
    }

    fn update(&self, edit: &mut dyn FnMut(&mut Config)) {
        SettingsService::update(self, |c| edit(c));
    }
}

/// Watches the file's directory, since editors save by rename and a watch on the file is lost then.
fn watch(path: &Path) -> io::Result<(Async<OwnedFd>, Vec<u8>)> {
    let dir = path.parent().ok_or(io::ErrorKind::InvalidInput)?;
    let name = path.file_name().ok_or(io::ErrorKind::InvalidInput)?;
    let _ = std::fs::create_dir_all(dir);
    let fd = inotify::open()?;
    inotify::add_watch(&fd, dir, libc::IN_CLOSE_WRITE | libc::IN_MOVED_TO | libc::IN_MOVED_FROM | libc::IN_DELETE)?;
    Ok((fd, name.as_encoded_bytes().to_vec()))
}

async fn watch_loop(fd: Async<OwnedFd>, name: Vec<u8>, service: Weak<SettingsService>) {
    while fd.readable().await.is_ok() {
        if !inotify::drain(&fd).contains(&name) {
            continue;
        }
        let Some(service) = service.upgrade() else { return };
        service.reload();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::settings::{BarStyle, RainMode};
    use crate::service::runtime;
    use calloop::EventLoop;
    use std::cell::Cell;
    use std::time::{Duration, Instant};

    fn pump_until(event_loop: &mut EventLoop<'static, ()>, done: impl Fn() -> bool) -> bool {
        let deadline = Instant::now() + Duration::from_secs(2);
        while !done() && Instant::now() < deadline {
            event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
        }
        done()
    }

    #[test]
    fn settings_reload_on_rename() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("astralia/config.json");
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(&path, r#"{"bar": {"autohideEnabled": true}}"#).unwrap();
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let service = SettingsService::start_with(path.to_str().unwrap().to_string());
        assert!(service.config().autohide);
        let emitted = Rc::new(Cell::new(0));
        let e = emitted.clone();
        service.changed.connect(move |_| e.set(e.get() + 1));

        // An unrelated file in the directory is ignored.
        std::fs::write(dir.path().join("astralia/other.json"), "{}").unwrap();
        let fresh = dir.path().join("astralia/config.json.new");
        std::fs::write(&fresh, r#"{"bar": {"autohideEnabled": false}}"#).unwrap();
        std::fs::rename(&fresh, &path).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 1), "a rename over the file is reloaded");
        assert!(!service.config().autohide);

        // Rewriting the same content is no change.
        std::fs::write(&path, r#"{"bar": {"autohideEnabled": false}}"#).unwrap();
        event_loop.dispatch(Duration::from_millis(100), &mut ()).unwrap();
        assert_eq!(emitted.get(), 1, "an identical reload emits nothing");

        std::fs::write(&path, r#"{"bar": {"autohideEnabled": true}}"#).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 2));
        std::fs::remove_file(&path).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 3), "a removed config reverts to the defaults");
        assert_eq!(*service.config(), Config::default());
    }

    fn start_in(dir: &tempfile::TempDir) -> (EventLoop<'static, ()>, String, Rc<SettingsService>, Rc<Cell<i32>>) {
        let path = dir.path().join("astralia/config.json").to_str().unwrap().to_string();
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let service = SettingsService::start_with(path.clone());
        let emitted = Rc::new(Cell::new(0));
        let e = emitted.clone();
        service.changed.connect(move |_| e.set(e.get() + 1));
        (event_loop, path, service, emitted)
    }

    #[test]
    fn settings_service_writes() {
        let dir = tempfile::tempdir().unwrap();
        let (mut event_loop, path, service, emitted) = start_in(&dir);
        assert!(!Path::new(&path).exists(), "a missing config is not written");
        assert_eq!(*service.config(), Config::default(), "a missing config yields the defaults");

        service.update(|cfg| cfg.autohide = true);
        assert!(emitted.get() == 1 && service.config().autohide && Path::new(&path).exists(), "update stores, writes and emits");
        service.update(|cfg| cfg.autohide = true);
        assert_eq!(emitted.get(), 1, "an unchanged update emits nothing");
        event_loop.dispatch(Duration::from_millis(50), &mut ()).unwrap();
        assert_eq!(emitted.get(), 1, "the service's own write is not reloaded as a change");

        let mut external = service.config().clone();
        external.rain.mode = RainMode::Stiletto;
        std::fs::write(&path, serialize_config(&external)).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 2), "an external edit is reloaded");
        assert_eq!(service.config().rain.mode, RainMode::Stiletto, "the external value is applied");

        std::fs::remove_file(&path).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 3), "a removed config reverts to the defaults");
        assert!(*service.config() == Config::default() && !Path::new(&path).exists(), "and is not rewritten");
    }

    #[test]
    fn config_watch_in_place_write() {
        let dir = tempfile::tempdir().unwrap();
        let (mut event_loop, path, service, emitted) = start_in(&dir);
        std::fs::write(&path, r#"{"displays":{"idle":{"ambientTimeoutSeconds":300}}}"#).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 1));
        std::fs::write(&path, r#"{"displays":{"idle":{"ambientTimeoutSeconds":400}}}"#).unwrap();
        assert!(pump_until(&mut event_loop, || emitted.get() == 2), "an in-place write is seen");
        assert_eq!(service.config().ambient_timeout, 400);
    }

    #[test]
    fn settings_update_rereads() {
        let dir = tempfile::tempdir().unwrap();
        let (mut event_loop, path, service, emitted) = start_in(&dir);
        service.update(|cfg| cfg.autohide = true);
        assert_eq!(emitted.get(), 1);

        // Changed on disk, and the watch has not run yet.
        let mut disk = service.config().clone();
        disk.bar_style = BarStyle::Pop;
        std::fs::write(&path, serialize_config(&disk)).unwrap();
        service.update(|cfg| cfg.default_osd = false);
        assert_eq!(emitted.get(), 2, "one change for the edit");
        let cfg = service.config().clone();
        assert!(cfg.bar_style == BarStyle::Pop && !cfg.default_osd && cfg.autohide, "the disk change is kept and the edit added");
        let saved = parse_config(&read_text_file(&path).unwrap());
        assert_eq!(saved, cfg, "the file holds both");

        event_loop.dispatch(Duration::from_millis(100), &mut ()).unwrap();
        assert_eq!(emitted.get(), 2, "the reloads that follow do not fire again");
    }
}
