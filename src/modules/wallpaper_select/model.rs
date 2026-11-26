//! The picker's state: the files of the folder, the selection and its scroller, and the appear tween.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::{Component, Path, PathBuf};
use std::rc::{Rc, Weak};
use std::time::Instant;

use crate::base::animation::{AnimationManager, Easing, Value, instant, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::wallpaper_select_config as cfg;
use crate::render::geometry::Rect;
use crate::service::runtime;
use crate::service::settings::ConfigStore;
use crate::service::wallpaper::{
    wallpaper_column_count_animated, wallpaper_column_override_animated, wallpaper_column_path_animated, wallpaper_list_files,
    wallpaper_set_column_animated,
};

const OWNER_APPEAR: u64 = 1;
const OWNER_STEP: u64 = 2;

/// The config the picker reads and edits.
pub type Hooks = Rc<dyn ConfigStore>;

/// A card as drawn, for clicks.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WallpaperCard {
    pub rect: Rect,
    pub index: usize,
}

/// `path` with `.` and `..` resolved by name only.
fn lexically_normal(path: &str) -> PathBuf {
    let mut out = PathBuf::new();
    for part in Path::new(path).components() {
        match part {
            Component::CurDir => {}
            Component::ParentDir if matches!(out.components().next_back(), Some(Component::Normal(_))) => {
                out.pop();
            }
            other => out.push(other),
        }
    }
    out
}

fn same_file(a: &str, b: &str) -> bool {
    lexically_normal(a) == lexically_normal(b)
}

/// Where a finished scan waits for the loop, with the generation it started under; `wake` tells the host to poll.
#[derive(Default)]
struct Inbox {
    scanned: RefCell<Option<(u64, Vec<String>)>>,
    wake: RefCell<Option<Rc<dyn Fn()>>>,
}

pub struct Model {
    hooks: Hooks,
    animated: bool,
    /// The time of the frame being drawn, from the last `tick`.
    now: Option<Instant>,
    animations: AnimationManager,
    inbox: Rc<Inbox>,
    cards: Vec<WallpaperCard>,
    files: Vec<String>,
    monitor: String,
    current: String,
    seen: HashMap<String, Instant>,
    directory: String,
    column: i32,
    selected: usize,
    position: Value,
    appear: Value,
    wheel: f32,
    generation: u64,
    open: bool,
    closing: bool,
    scanning: bool,
    fading: bool,
    /// Set by the close tween's end; `tick` and `close` turn it into the close.
    faded_out: Rc<Cell<bool>>,
    close_request: bool,
    closed: bool,
}

impl Model {
    pub fn new(hooks: Hooks) -> Model {
        Model {
            hooks,
            animated: false,
            now: None,
            animations: AnimationManager::default(),
            inbox: Rc::default(),
            cards: Vec::new(),
            files: Vec::new(),
            monitor: String::new(),
            current: String::new(),
            seen: HashMap::new(),
            directory: String::new(),
            column: 0,
            selected: 0,
            position: value(0.0),
            appear: value(0.0),
            wheel: 0.0,
            generation: 0,
            open: false,
            closing: false,
            scanning: false,
            fading: false,
            faded_out: Rc::default(),
            close_request: false,
            closed: false,
        }
    }

    /// Called when a scan lands, so the host polls.
    pub fn set_wake(&mut self, wake: Rc<dyn Fn()>) {
        *self.inbox.wake.borrow_mut() = Some(wake);
    }

    fn wants_animated(&self) -> bool {
        self.hooks.config().wallpaper_animated_enabled
    }

    /// The column of `monitor` under a point of a `width` by `height` monitor; 0 off the monitor.
    pub fn column_at(&self, monitor: &str, x: f64, y: f64, width: f64, height: f64) -> i32 {
        if x < 0.0 || y < 0.0 || x >= width || y >= height {
            return 0;
        }
        let columns = wallpaper_column_count_animated(&self.hooks.config(), monitor, self.wants_animated());
        ((x * f64::from(columns) / width) as i32).min(columns - 1)
    }

    pub fn open(&mut self, monitor: &str, column: i32) {
        if self.open {
            return;
        }
        self.monitor = monitor.to_string();
        self.column = column;
        self.animated = self.wants_animated();
        {
            let config = self.hooks.config();
            self.current = if self.animated {
                wallpaper_column_override_animated(&config, monitor, column, true)
            } else {
                wallpaper_column_path_animated(&config, monitor, column, false)
            };
            self.directory = if self.animated { config.wallpaper_animated_dir.clone() } else { config.wallpaper_dir.clone() };
        }
        self.files.clear();
        if !self.current.is_empty() {
            self.files.push(self.current.clone());
        }
        self.selected = 0;
        self.position.set(0.0);
        self.wheel = 0.0;
        self.appear.set(0.0);
        self.open = true;
        self.closing = false;
        self.faded_out.set(false);
        self.seen.clear();
        self.animations.animate(0.0, 1.0, cfg::APPEAR_MS, Easing::EaseOutCubic, writer(&self.appear), None, OWNER_APPEAR);
        self.scan();
    }

    pub fn tick(&mut self, now: Instant) {
        self.now = Some(now);
        self.animations.tick(now);
        self.finish_if_faded();
    }

    /// Clears the cards of the last frame; the view adds this frame's.
    pub fn begin_frame(&mut self) {
        self.cards.clear();
        self.fading = false;
    }

    pub fn add_card(&mut self, rect: Rect, index: usize) {
        self.cards.push(WallpaperCard { rect, index });
    }

    /// How far a card's image has faded in: 0 while it loads, then up to 1 over `FADE_IN_MS`.
    pub fn fade(&mut self, path: &str, ready: bool) -> f32 {
        if instant() {
            return 1.0;
        }
        if !ready {
            return 0.0;
        }
        let now = self.now.unwrap_or_else(Instant::now);
        let at = *self.seen.entry(path.to_string()).or_insert(now);
        let t = now.saturating_duration_since(at).as_secs_f32() * 1000.0 / cfg::FADE_IN_MS;
        if t >= 1.0 {
            return 1.0;
        }
        self.fading = true;
        t
    }

    /// Fades the cards out; the picker closes when they are gone.
    pub fn close(&mut self) {
        if !self.open || self.closing {
            return;
        }
        self.closing = true;
        self.generation += 1;
        self.animations.cancel_owner(OWNER_STEP);
        let faded_out = self.faded_out.clone();
        self.animations.animate(
            self.appear.get(),
            0.0,
            cfg::APPEAR_MS,
            Easing::EaseInCubic,
            writer(&self.appear),
            Some(Box::new(move |_| faded_out.set(true))),
            OWNER_APPEAR,
        );
        self.finish_if_faded();
    }

    /// Closes at once, without the fade.
    pub fn fast_close(&mut self) {
        if !self.open {
            return;
        }
        self.generation += 1;
        self.animations.cancel_owner(OWNER_STEP);
        self.animations.cancel_owner(OWNER_APPEAR);
        self.appear.set(0.0);
        self.faded_out.set(true);
        self.finish_if_faded();
    }

    fn finish_if_faded(&mut self) {
        if !self.faded_out.replace(false) {
            return;
        }
        self.open = false;
        self.closing = false;
        self.scanning = false;
        self.files.clear();
        self.cards.clear();
        self.seen.clear();
        self.closed = true;
    }

    /// True once after the picker asked to close; the host closes it.
    pub fn take_close_request(&mut self) -> bool {
        std::mem::take(&mut self.close_request)
    }

    /// True once after the picker closed; the host gives the input back.
    pub fn take_closed(&mut self) -> bool {
        std::mem::take(&mut self.closed)
    }

    fn scan(&mut self) {
        self.scanning = true;
        self.generation += 1;
        let generation = self.generation;
        let directory = self.directory.clone();
        let animated = self.animated;
        let inbox = Rc::downgrade(&self.inbox);
        runtime::spawn(async move {
            let found = blocking::unblock(move || wallpaper_list_files(&directory, animated, cfg::MAX_IMAGES)).await;
            deliver(&inbox, generation, found);
        });
    }

    /// Applies a scan that finished since the last call; true when one arrived.
    pub fn poll(&mut self) -> bool {
        let Some((generation, mut found)) = self.inbox.scanned.borrow_mut().take() else { return false };
        if generation != self.generation {
            return true;
        }
        let at = match found.iter().position(|p| same_file(p, &self.current)) {
            Some(i) => Some(i),
            None if !self.current.is_empty() => {
                found.insert(0, self.current.clone());
                Some(0)
            }
            None => None,
        };
        self.files = found;
        self.selected = at.unwrap_or(0);
        self.position.set(self.selected as f32);
        self.animations.cancel_owner(OWNER_STEP);
        self.scanning = false;
        true
    }

    pub fn select(&mut self, index: i64) {
        if !self.open || self.closing || self.files.is_empty() {
            return;
        }
        let target = index.clamp(0, self.files.len() as i64 - 1) as usize;
        if target == self.selected {
            return;
        }
        self.selected = target;
        self.animations.animate(self.position.get(), target as f32, cfg::STEP_MS, Easing::EaseOutCubic, writer(&self.position), None, OWNER_STEP);
    }

    pub fn step(&mut self, delta: i64) {
        self.select(self.selected as i64 + delta);
    }

    /// Wheel motion in notches; whole notches step, the rest carries over.
    pub fn scroll(&mut self, notches: f32) {
        if !self.open || self.closing {
            return;
        }
        self.wheel += notches;
        while self.wheel >= cfg::WHEEL_NOTCH {
            self.wheel -= cfg::WHEEL_NOTCH;
            self.step(1);
        }
        while self.wheel <= -cfg::WHEEL_NOTCH {
            self.wheel += cfg::WHEEL_NOTCH;
            self.step(-1);
        }
    }

    /// Sets the selected file on the opened column, when it changed, and asks to close.
    pub fn apply(&mut self) {
        if !self.open || self.closing || self.files.is_empty() {
            return;
        }
        let path = self.files[self.selected].clone();
        if !same_file(&path, &self.current) {
            let (monitor, column, animated) = (self.monitor.clone(), self.column, self.animated);
            self.hooks.update(&mut |config| wallpaper_set_column_animated(config, &monitor, column, &path, animated));
        }
        self.close_request = true;
    }

    fn card_at(&self, x: f64, y: f64) -> Option<&WallpaperCard> {
        self.cards.iter().rev().find(|c| {
            let r = c.rect;
            x >= f64::from(r.x) && x < f64::from(r.x + r.w) && y >= f64::from(r.y) && y < f64::from(r.y + r.h)
        })
    }

    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.open && !self.closing && self.card_at(x, y).is_some()
    }

    /// A side card selects it, the selected card applies, anywhere else asks to close.
    pub fn click(&mut self, x: f64, y: f64) {
        if !self.open || self.closing {
            return;
        }
        match self.card_at(x, y).map(|c| c.index) {
            None => self.close_request = true,
            Some(index) if index == self.selected => self.apply(),
            Some(index) => self.select(index as i64),
        }
    }

    /// True when the key was used.
    pub fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.open || self.closing {
            return false;
        }
        match event.kind {
            KeyKind::Left => self.step(-1),
            KeyKind::Right => self.step(1),
            KeyKind::Home => self.select(0),
            KeyKind::End => self.select(self.files.len() as i64 - 1),
            KeyKind::Enter => self.apply(),
            KeyKind::Escape => self.close_request = true,
            _ => return false,
        }
        true
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    #[cfg(test)]
    pub fn closing(&self) -> bool {
        self.closing
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active() || self.fading
    }

    pub fn scanning(&self) -> bool {
        self.scanning
    }

    pub fn appear(&self) -> f32 {
        self.appear.get()
    }

    pub fn position(&self) -> f32 {
        self.position.get()
    }

    pub fn selected(&self) -> usize {
        self.selected
    }

    pub fn directory(&self) -> &str {
        &self.directory
    }

    pub fn column(&self) -> i32 {
        self.column
    }

    pub fn animated(&self) -> bool {
        self.animated
    }

    pub fn column_count(&self) -> i32 {
        wallpaper_column_count_animated(&self.hooks.config(), &self.monitor, self.animated)
    }

    pub fn files(&self) -> &[String] {
        &self.files
    }
}

fn deliver(inbox: &Weak<Inbox>, generation: u64, found: Vec<String>) {
    let Some(inbox) = inbox.upgrade() else { return };
    *inbox.scanned.borrow_mut() = Some((generation, found));
    let wake = inbox.wake.borrow().clone();
    if let Some(wake) = wake {
        wake();
    }
}
