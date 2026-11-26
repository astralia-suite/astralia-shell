//! The settings state: the tabs, the page shown and its fade, the focused field, the wallpaper
//! pickers, and every edit sent to the config.

use std::cell::{Cell, Ref, RefCell};
use std::rc::{Rc, Weak};
use std::time::Instant;

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::file::{collapse_home, expand_home};
use crate::config::settings::{
    BarStyle, Config, MonitorOverride, PaletteSource, RainMode, VisualizerParams, VisualizerShape, ambient_timeout_for,
    screensaver_timeout_for,
};
use crate::config::settings_config as cfg;
use crate::config::visualizer_config as vis;
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::scroll_sequence::{ScrollSequence, ScrollTimings};
use crate::render::text_field::{
    TextFieldResult, TextFieldState, TextFieldTypeAnim, text_field_backspace, text_field_handle_key, text_field_idle_toggle,
    text_field_type_anim_clear, text_field_type_anim_settle, text_field_type_anim_sync,
};
use crate::service::runtime;
use crate::service::settings::ConfigStore;
use crate::service::wallpaper::{
    WallpaperSlot, wallpaper_column_count, wallpaper_list_files, wallpaper_set_column, wallpaper_set_column_count,
    wallpaper_set_fill_mode, wallpaper_slot_dir,
};

const OWNER_SEQUENCE: u64 = 30;
const OWNER_TAB_FADE: u64 = 20;
const OWNER_FIELD_ANIM: u64 = 10000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tab {
    Ambient,
    Bar,
    Displays,
    General,
    Lock,
    Wallpaper,
    Palette,
}

impl Tab {
    const ALL: [Tab; 7] = [Tab::Ambient, Tab::Bar, Tab::Displays, Tab::General, Tab::Lock, Tab::Wallpaper, Tab::Palette];

    pub fn from_index(i: i32) -> Option<Tab> {
        usize::try_from(i).ok().and_then(|i| Tab::ALL.get(i).copied())
    }
}

#[derive(Clone, Copy, Debug)]
pub struct TabInfo {
    pub tab: Tab,
    pub label: &'static str,
    pub icon: &'static str,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Field {
    None,
    WallpaperDir,
    WallpaperAnimatedDir,
    LockWallpaperDir,
    AmbientTimeout,
    ScreensaverTimeout,
    VisualizerFps,
    VisualizerThin,
    VisualizerSize,
    VisualizerComplexity,
    VisualizerGlowDirections,
    VisualizerGlowQuality,
}

impl Field {
    const ALL: [Field; 12] = [
        Field::None,
        Field::WallpaperDir,
        Field::WallpaperAnimatedDir,
        Field::LockWallpaperDir,
        Field::AmbientTimeout,
        Field::ScreensaverTimeout,
        Field::VisualizerFps,
        Field::VisualizerThin,
        Field::VisualizerSize,
        Field::VisualizerComplexity,
        Field::VisualizerGlowDirections,
        Field::VisualizerGlowQuality,
    ];

    pub fn from_index(i: i32) -> Field {
        usize::try_from(i).ok().and_then(|i| Field::ALL.get(i).copied()).unwrap_or(Field::None)
    }
}

/// What a click on a region does; `a` and `b` carry its arguments.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Act {
    Close,
    Tab,
    Menu,
    DisplaysMonitor,
    ToggleOverride,
    ToggleOsd,
    ToggleNotifications,
    ToggleAutohide,
    ToggleExpandWidgets,
    IdleEnable,
    IdleAmbient,
    IdleScreensaver,
    IdleAmbientReset,
    IdleScreensaverReset,
    BarStyle,
    RainMode,
    RainAsync,
    VisualizerShape,
    VisualizerReset,
    AnimationDisable,
    WallpaperDefault,
    WallpaperAnimated,
    LockWallpaperMode,
    WallpaperRegion,
    WallpaperPick,
    WallpaperCount,
    WallpaperRemove,
    WallpaperFill,
    WallpaperRescan,
    DropdownToggle,
    DropdownDismiss,
    CapturePick,
    PaletteSource,
    FieldFocus,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Region {
    pub rect: Rect,
    pub act: Act,
    pub a: i32,
    pub b: i32,
}

impl Region {
    fn contains(&self, x: f64, y: f64) -> bool {
        let r = self.rect;
        r.w > 0.0 && x >= f64::from(r.x) && x < f64::from(r.x + r.w) && y >= f64::from(r.y) && y < f64::from(r.y + r.h)
    }
}

/// Whether the animated wallpaper of a monitor column decodes on the CPU.
pub type CpuFallback = Box<dyn Fn(&str, i32) -> bool>;

/// Where the settings read and write the config, and what they ask the backend.
pub struct Hooks {
    pub store: Rc<dyn ConfigStore>,
    pub monitors: Box<dyn Fn() -> Vec<String>>,
    pub focused_monitor: Box<dyn Fn() -> String>,
    pub cpu_fallback: CpuFallback,
}

/// One wallpaper grid: the scanned folder, and the column it sets.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WallpaperPicker {
    pub dir: String,
    pub scanning: bool,
    pub files: Vec<String>,
    pub generation: u64,
    pub region: String,
    pub column: i32,
    pub scroll: f32,
    pub grid_width: f32,
    pub grid_height: f32,
}

const TABS: [TabInfo; 7] = [
    TabInfo { tab: Tab::General, label: "General", icon: icons::ADJUSTMENTS },
    TabInfo { tab: Tab::Ambient, label: "Ambient", icon: icons::WAVE_SINE },
    TabInfo { tab: Tab::Bar, label: "Bar", icon: icons::LAYOUT_NAVBAR },
    TabInfo { tab: Tab::Palette, label: "Color Palette", icon: icons::PALETTE_ICON },
    TabInfo { tab: Tab::Displays, label: "Displays", icon: icons::DEVICE_DESKTOP },
    TabInfo { tab: Tab::Lock, label: "Lock Screen", icon: icons::LOCK },
    TabInfo { tab: Tab::Wallpaper, label: "Wallpaper", icon: icons::WALLPAPER },
];

pub fn all_tabs() -> &'static [TabInfo] {
    &TABS
}

fn slot_from(value: i32) -> WallpaperSlot {
    match value {
        1 => WallpaperSlot::Animated,
        2 => WallpaperSlot::Lock,
        _ => WallpaperSlot::Still,
    }
}

fn home() -> String {
    std::env::var("HOME").unwrap_or_default()
}

/// `value` with three decimals, trailing zeros and point dropped.
fn trim_float(value: f32) -> String {
    let mut text = format!("{value:.3}");
    while text.contains('.') && (text.ends_with('0') || text.ends_with('.')) {
        text.pop();
    }
    text
}

/// The number at the start of `text` after blanks: a sign, digits and, for floats, a fraction.
fn leading_number(text: &str, fraction: bool) -> &str {
    let text = text.trim_start();
    let bytes = text.as_bytes();
    let mut end = usize::from(matches!(bytes.first(), Some(b'+' | b'-')));
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }
    if fraction && bytes.get(end) == Some(&b'.') {
        end += 1;
        while end < bytes.len() && bytes[end].is_ascii_digit() {
            end += 1;
        }
    }
    &text[..end]
}

fn parse_int(text: &str) -> Option<i32> {
    leading_number(text, false).parse().ok()
}

fn parse_float(text: &str) -> Option<f32> {
    leading_number(text, true).parse().ok()
}

fn enable_defaults(o: &mut MonitorOverride, config: &Config) {
    o.osd = config.default_osd;
    o.notifications = config.default_notifications;
    o.autohide = config.autohide;
    o.ambient_enabled = config.ambient_enabled;
    o.ambient_timeout = config.ambient_timeout;
    o.screensaver_enabled = config.screensaver_enabled;
    o.screensaver_timeout = config.screensaver_timeout;
}

/// The text a field starts editing with.
pub fn field_text(config: &Config, field: Field, monitor: &str) -> String {
    let v = &config.visualizer;
    match field {
        Field::WallpaperDir => collapse_home(&config.wallpaper_dir, &home()),
        Field::WallpaperAnimatedDir => collapse_home(&config.wallpaper_animated_dir, &home()),
        Field::LockWallpaperDir => collapse_home(&config.lock_wallpaper_dir, &home()),
        Field::AmbientTimeout => ambient_timeout_for(config, monitor).to_string(),
        Field::ScreensaverTimeout => screensaver_timeout_for(config, monitor).to_string(),
        Field::VisualizerFps => v.fps.to_string(),
        Field::VisualizerThin => trim_float(v.particle_thin),
        Field::VisualizerSize => v.particle_size.to_string(),
        Field::VisualizerComplexity => v.fractal_complexity.to_string(),
        Field::VisualizerGlowDirections => trim_float(v.glow_directions),
        Field::VisualizerGlowQuality => trim_float(v.glow_quality),
        Field::None => String::new(),
    }
}

/// Stores a field's edited text, clamped; text that does not parse keeps the previous value.
pub fn apply_field(config: &mut Config, field: Field, text: &str, monitor: &str) {
    let failed = || ::log::error!("settings: could not parse '{text}' for field {}, keeping the previous value", field as i32);
    let int = |min: i32, max: i32| parse_int(text).map(|v| v.clamp(min, max));
    let float = |min: f32, max: f32| parse_float(text).map(|v| v.clamp(min, max));
    let timeout = || int(cfg::IDLE_TIMEOUT_MIN, cfg::IDLE_TIMEOUT_MAX).map(|v| v as u32);
    match field {
        Field::WallpaperDir => config.wallpaper_dir = expand_home(text, &home()),
        Field::WallpaperAnimatedDir => config.wallpaper_animated_dir = expand_home(text, &home()),
        Field::LockWallpaperDir => config.lock_wallpaper_dir = expand_home(text, &home()),
        Field::AmbientTimeout | Field::ScreensaverTimeout => {
            let Some(value) = timeout() else { return failed() };
            let ambient = field == Field::AmbientTimeout;
            let target = if monitor.is_empty() {
                if ambient { &mut config.ambient_timeout } else { &mut config.screensaver_timeout }
            } else {
                let o = config.monitor_overrides.entry(monitor.to_string()).or_default();
                if ambient { &mut o.ambient_timeout } else { &mut o.screensaver_timeout }
            };
            *target = value;
        }
        Field::VisualizerFps => match int(vis::FPS_MIN, vis::FPS_MAX) {
            Some(v) => config.visualizer.fps = v,
            None => failed(),
        },
        Field::VisualizerThin => match float(vis::PARTICLE_THIN_MIN, vis::PARTICLE_THIN_MAX) {
            Some(v) => config.visualizer.particle_thin = v,
            None => failed(),
        },
        Field::VisualizerSize => config.visualizer.particle_size = vis::PARTICLE_SIZE,
        Field::VisualizerComplexity => match int(vis::COMPLEXITY_MIN, vis::COMPLEXITY_MAX) {
            Some(v) => config.visualizer.fractal_complexity = v,
            None => failed(),
        },
        Field::VisualizerGlowDirections => match float(vis::GLOW_DIRECTIONS_MIN, vis::GLOW_DIRECTIONS_MAX) {
            Some(v) => config.visualizer.glow_directions = v,
            None => failed(),
        },
        Field::VisualizerGlowQuality => match float(vis::GLOW_QUALITY_MIN, vis::GLOW_QUALITY_MAX) {
            Some(v) => config.visualizer.glow_quality = v,
            None => failed(),
        },
        Field::None => {}
    }
}

/// Where finished scans wait for the loop; `wake` tells the host to poll.
#[derive(Default)]
struct Inbox {
    scanned: RefCell<Vec<(WallpaperSlot, u64, Vec<String>)>>,
    wake: RefCell<Option<Rc<dyn Fn()>>>,
}

fn deliver(inbox: &Weak<Inbox>, slot: WallpaperSlot, generation: u64, found: Vec<String>) {
    let Some(inbox) = inbox.upgrade() else { return };
    inbox.scanned.borrow_mut().push((slot, generation, found));
    let wake = inbox.wake.borrow().clone();
    if let Some(wake) = wake {
        wake();
    }
}

pub struct Model {
    hooks: Hooks,
    inbox: Rc<Inbox>,
    animations: AnimationManager,
    sequence: ScrollSequence,
    tabs: Vec<TabInfo>,
    regions: Vec<Region>,
    dropdown_open: Option<i32>,
    panel: Rect,
    open: bool,
    active: Tab,
    pending: Tab,
    menu: bool,
    pending_menu: bool,
    tab_alpha: Value,
    /// Set when the fade-out of a tab switch ends; the switch then takes effect.
    tab_swap: Rc<Cell<bool>>,
    focused: Field,
    field: TextFieldState,
    field_anim: TextFieldTypeAnim,
    monitors: Vec<String>,
    displays_monitor: String,
    static_picker: WallpaperPicker,
    animated_picker: WallpaperPicker,
    lock_picker: WallpaperPicker,
    /// Set when the close sequence ends.
    sequence_done: Rc<Cell<bool>>,
    close_request: bool,
    closed: bool,
}

impl Model {
    pub fn new(hooks: Hooks) -> Model {
        let tabs = all_tabs().to_vec();
        Model {
            hooks,
            inbox: Rc::default(),
            animations: AnimationManager::default(),
            sequence: ScrollSequence::new(OWNER_SEQUENCE, ScrollTimings::default()),
            tabs,
            regions: Vec::new(),
            dropdown_open: None,
            panel: Rect::default(),
            open: false,
            active: Tab::Wallpaper,
            pending: Tab::Wallpaper,
            menu: true,
            pending_menu: true,
            tab_alpha: value(1.0),
            tab_swap: Rc::default(),
            focused: Field::None,
            field: TextFieldState::default(),
            field_anim: TextFieldTypeAnim::default(),
            monitors: Vec::new(),
            displays_monitor: String::new(),
            static_picker: WallpaperPicker::default(),
            animated_picker: WallpaperPicker::default(),
            lock_picker: WallpaperPicker::default(),
            sequence_done: Rc::default(),
            close_request: false,
            closed: false,
        }
    }

    /// Called when a scan lands, so the host polls.
    pub fn set_wake(&mut self, wake: Rc<dyn Fn()>) {
        *self.inbox.wake.borrow_mut() = Some(wake);
    }

    pub fn open(&mut self) {
        self.open = true;
        self.dropdown_open = None;
        self.active = Tab::Wallpaper;
        self.pending = Tab::Wallpaper;
        self.menu = true;
        self.pending_menu = true;
        self.tab_swap.set(false);
        self.animations.cancel_owner(OWNER_TAB_FADE);
        self.tab_alpha.set(1.0);
        self.sequence_done.set(false);
        self.sequence.open(&mut self.animations);
        self.sync();
    }

    /// Commits the focused field and plays the close; the model closes when the scroll has dropped.
    pub fn close(&mut self) {
        if !self.open || self.sequence.closing() {
            return;
        }
        self.commit_field();
        let done = self.sequence_done.clone();
        self.sequence.close(&mut self.animations, Some(Box::new(move || done.set(true))));
        self.settle();
    }

    /// Closes at once, without the sequence.
    pub fn fast_close(&mut self) {
        if !self.open {
            return;
        }
        self.commit_field();
        for owner in [OWNER_SEQUENCE, OWNER_SEQUENCE + 1, OWNER_SEQUENCE + 2, OWNER_TAB_FADE] {
            self.animations.cancel_owner(owner);
        }
        self.sequence_done.set(true);
        self.settle();
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.settle();
    }

    /// Runs what tween ends asked for: a tab switch, or the end of the close.
    fn settle(&mut self) {
        if self.tab_swap.replace(false) {
            self.menu = self.pending_menu;
            self.active = self.pending;
            self.sync();
        }
        if !self.sequence_done.replace(false) {
            return;
        }
        self.open = false;
        for slot in [WallpaperSlot::Still, WallpaperSlot::Animated, WallpaperSlot::Lock] {
            let picker = self.picker_mut(slot);
            picker.files.clear();
            picker.dir.clear();
            picker.generation += 1;
            picker.scanning = false;
        }
        self.closed = true;
    }

    /// True once after the settings asked to close; the host closes them.
    pub fn take_close_request(&mut self) -> bool {
        std::mem::take(&mut self.close_request)
    }

    /// True once after the settings closed; the host gives the input back.
    pub fn take_closed(&mut self) -> bool {
        std::mem::take(&mut self.closed)
    }

    /// Clears the regions of the last frame; the view adds this frame's.
    pub fn begin_frame(&mut self) {
        self.regions.clear();
    }

    pub fn add_region(&mut self, rect: Rect, act: Act, a: i32, b: i32) {
        self.regions.push(Region { rect, act, a, b });
    }

    /// A region that wins over every one added before it.
    pub fn add_region_front(&mut self, rect: Rect, act: Act, a: i32, b: i32) {
        self.regions.insert(0, Region { rect, act, a, b });
    }

    pub fn dropdown_open(&self) -> Option<i32> {
        self.dropdown_open
    }

    pub fn set_panel(&mut self, panel: Rect) {
        self.panel = panel;
    }

    /// The slot the wallpaper tab edits now.
    fn desktop_slot(&self) -> WallpaperSlot {
        if self.config().wallpaper_animated_enabled { WallpaperSlot::Animated } else { WallpaperSlot::Still }
    }

    /// Re-reads the monitors, keeps each picker on a monitor and column that exist, and scans the
    /// shown picker's folder when it changed.
    pub fn sync(&mut self) {
        self.monitors = (self.hooks.monitors)().into_iter().filter(|n| !n.is_empty()).collect();
        self.monitors.sort();
        let known = |monitors: &[String], name: &str| monitors.iter().any(|m| m == name);
        if !self.displays_monitor.is_empty() && !known(&self.monitors, &self.displays_monitor) {
            self.displays_monitor.clear();
        }
        let focused = (self.hooks.focused_monitor)();
        let store = self.hooks.store.clone();
        let current = store.config();
        for slot in [WallpaperSlot::Still, WallpaperSlot::Animated, WallpaperSlot::Lock] {
            let monitors = self.monitors.clone();
            let picker = self.picker_mut(slot);
            if monitors.len() == 1 {
                picker.region = monitors[0].clone();
            } else if picker.region.is_empty() || !known(&monitors, &picker.region) {
                picker.region = if known(&monitors, &focused) { focused.clone() } else { monitors.first().cloned().unwrap_or_default() };
            }
            let count = if picker.region.is_empty() { 1 } else { wallpaper_column_count(&current, &picker.region, slot) };
            picker.column = picker.column.clamp(0, count - 1);
        }
        if !self.open || self.sequence.closing() || self.menu {
            return;
        }
        let slot = match self.active {
            Tab::Wallpaper => Some(if current.wallpaper_animated_enabled { WallpaperSlot::Animated } else { WallpaperSlot::Still }),
            Tab::Lock if current.lock_wallpaper_custom => Some(WallpaperSlot::Lock),
            _ => None,
        };
        if let Some(slot) = slot {
            let dir = wallpaper_slot_dir(&current, slot).to_string();
            drop(current);
            if self.picker(slot).dir != dir {
                self.scan(slot, dir);
            }
        }
    }

    pub fn rescan(&mut self, slot: WallpaperSlot) {
        let dir = wallpaper_slot_dir(&self.config(), slot).to_string();
        self.scan(slot, dir);
    }

    fn scan(&mut self, slot: WallpaperSlot, dir: String) {
        let picker = self.picker_mut(slot);
        picker.dir.clone_from(&dir);
        picker.scanning = true;
        picker.scroll = 0.0;
        picker.generation += 1;
        let generation = picker.generation;
        let inbox = Rc::downgrade(&self.inbox);
        runtime::spawn(async move {
            let found = blocking::unblock(move || wallpaper_list_files(&dir, slot == WallpaperSlot::Animated, cfg::MAX_IMAGES)).await;
            deliver(&inbox, slot, generation, found);
        });
    }

    /// Applies the scans finished since the last call; true when any arrived.
    pub fn poll(&mut self) -> bool {
        let scanned = std::mem::take(&mut *self.inbox.scanned.borrow_mut());
        let any = !scanned.is_empty();
        for (slot, generation, found) in scanned {
            let picker = self.picker_mut(slot);
            if generation == picker.generation {
                picker.files = found;
                picker.scanning = false;
            }
        }
        any
    }

    fn select_tab(&mut self, tab: Tab) {
        self.go(false, tab);
    }

    fn show_menu(&mut self) {
        self.go(true, self.active);
    }

    /// Fades the page out, switches, and fades the new one in.
    fn go(&mut self, menu: bool, tab: Tab) {
        if if menu { self.pending_menu } else { !self.pending_menu && tab == self.pending } {
            return;
        }
        self.pending_menu = menu;
        self.pending = tab;
        self.dropdown_open = None;
        let (alpha, swap) = (self.tab_alpha.clone(), self.tab_swap.clone());
        let fade_in = move |am: &mut AnimationManager| {
            swap.set(true);
            am.animate(0.0, 1.0, cfg::TAB_FADE_MS, Easing::EaseOutCubic, writer(&alpha), None, OWNER_TAB_FADE);
        };
        self.animations.animate(self.tab_alpha.get(), 0.0, cfg::TAB_FADE_MS, Easing::EaseOutCubic, writer(&self.tab_alpha), Some(Box::new(fade_in)), OWNER_TAB_FADE);
        self.sync();
        self.settle();
    }

    fn focus_field(&mut self, field: Field) {
        if field == self.focused {
            return;
        }
        self.commit_field();
        self.focused = field;
        let text = field_text(&self.config(), field, &self.displays_monitor);
        self.field.text = text;
        self.field.preedit.clear();
        self.field.cursor_idle_visible = true;
        text_field_type_anim_settle(&mut self.field_anim, &mut self.animations, OWNER_FIELD_ANIM, &self.field.text);
    }

    fn unfocus(&mut self) {
        self.focused = Field::None;
        self.field.preedit.clear();
        text_field_type_anim_clear(&mut self.field_anim, &mut self.animations, OWNER_FIELD_ANIM);
    }

    fn commit_field(&mut self) {
        if self.focused == Field::None {
            return;
        }
        let (field, text, monitor) = (self.focused, self.field.text.clone(), self.displays_monitor.clone());
        self.unfocus();
        self.edit(|config| apply_field(config, field, &text, &monitor));
    }

    fn edit(&mut self, mut apply: impl FnMut(&mut Config)) {
        self.hooks.store.update(&mut apply);
        self.sync();
    }

    /// True when the key was used.
    pub fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.open || self.sequence.closing() {
            return false;
        }
        if self.focused == Field::None {
            match event.kind {
                KeyKind::Escape if !self.pending_menu => self.show_menu(),
                KeyKind::Escape => self.close_request = true,
                KeyKind::Up | KeyKind::Down if !self.pending_menu => {
                    let Some(index) = self.tabs.iter().position(|t| t.tab == self.pending) else { return false };
                    let index = if event.kind == KeyKind::Down { (index + 1).min(self.tabs.len() - 1) } else { index.saturating_sub(1) };
                    self.select_tab(self.tabs[index].tab);
                }
                _ => return false,
            }
            return true;
        }
        match text_field_handle_key(&mut self.field, event) {
            TextFieldResult::Changed => {
                text_field_type_anim_sync(&mut self.field_anim, &mut self.animations, OWNER_FIELD_ANIM, &self.field.text);
            }
            TextFieldResult::Committed => self.commit_field(),
            TextFieldResult::Cancelled => self.unfocus(),
            TextFieldResult::None => return false,
        }
        true
    }

    pub fn commit_text(&mut self, text: &str) {
        self.field.text.push_str(text);
        self.field.preedit.clear();
        text_field_type_anim_sync(&mut self.field_anim, &mut self.animations, OWNER_FIELD_ANIM, &self.field.text);
    }

    pub fn delete_before(&mut self, count: u32) {
        for _ in 0..count {
            text_field_backspace(&mut self.field.text);
        }
        text_field_type_anim_sync(&mut self.field_anim, &mut self.animations, OWNER_FIELD_ANIM, &self.field.text);
    }

    pub fn set_preedit(&mut self, text: &str) {
        self.field.preedit = text.to_string();
        self.field.cursor_idle_visible = true;
    }

    /// Blinks the caret of a focused field; true when it changed.
    pub fn toggle_caret(&mut self) -> bool {
        self.focused != Field::None && text_field_idle_toggle(&mut self.field)
    }

    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.regions.iter().any(|r| r.contains(x, y))
    }

    /// Runs the first region under the point; outside the card asks to close.
    pub fn click(&mut self, x: f64, y: f64) {
        if !self.open || self.sequence.closing() || !self.sequence.settled() {
            return;
        }
        if let Some(region) = self.regions.iter().find(|r| r.contains(x, y)).copied() {
            if region.act != Act::DropdownToggle {
                self.dropdown_open = None;
            }
            self.dispatch(region);
            return;
        }
        let p = self.panel;
        let inside = x >= f64::from(p.x) && x < f64::from(p.x + p.w) && y >= f64::from(p.y) && y < f64::from(p.y + p.h);
        if !inside {
            self.close_request = true;
        }
    }

    /// Scrolls the wallpaper grid of the page shown by `pixels`.
    pub fn scroll(&mut self, pixels: f32) {
        if !self.open || self.sequence.closing() || !self.sequence.settled() {
            return;
        }
        if self.menu || (self.active != Tab::Wallpaper && self.active != Tab::Lock) {
            return;
        }
        let slot = if self.active == Tab::Wallpaper { self.desktop_slot() } else { WallpaperSlot::Lock };
        let picker = self.picker_mut(slot);
        let inset_h = picker.grid_height - cfg::GRID_INSET * 2.0;
        let columns = cfg::THUMB_COLUMNS as usize;
        let rows = picker.files.len().div_ceil(columns);
        let content = if rows == 0 { 0.0 } else { rows as f32 * (cfg::THUMB_SIZE + cfg::THUMB_GAP) - cfg::THUMB_GAP };
        picker.scroll = (picker.scroll + pixels).clamp(0.0, (content - inset_h).max(0.0));
    }

    fn monitor(&self, index: i32) -> String {
        usize::try_from(index).ok().and_then(|i| self.monitors.get(i)).cloned().unwrap_or_default()
    }

    fn dispatch(&mut self, region: Region) {
        match region.act {
            Act::Close => {
                self.close_request = true;
                return;
            }
            Act::Tab => {
                self.commit_field();
                if let Some(tab) = Tab::from_index(region.a) {
                    self.select_tab(tab);
                }
                return;
            }
            Act::Menu => {
                self.commit_field();
                self.show_menu();
                return;
            }
            Act::FieldFocus => {
                self.focus_field(Field::from_index(region.a));
                return;
            }
            Act::DisplaysMonitor => {
                self.displays_monitor = self.monitor(region.a);
                return;
            }
            _ => {}
        }
        self.commit_field();
        let target = self.displays_monitor.clone();
        let flip = |act: Act, config: &mut Config, o: Option<&mut MonitorOverride>| {
            let value = match (act, o) {
                (Act::ToggleOsd, Some(o)) => &mut o.osd,
                (Act::ToggleOsd, None) => &mut config.default_osd,
                (Act::ToggleNotifications, Some(o)) => &mut o.notifications,
                (Act::ToggleNotifications, None) => &mut config.default_notifications,
                (Act::ToggleAutohide, Some(o)) => &mut o.autohide,
                (Act::ToggleAutohide, None) => &mut config.autohide,
                (Act::IdleAmbient, Some(o)) => &mut o.ambient_enabled,
                (Act::IdleAmbient, None) => &mut config.ambient_enabled,
                (_, Some(o)) => &mut o.screensaver_enabled,
                (_, None) => &mut config.screensaver_enabled,
            };
            *value = !*value;
        };
        match region.act {
            Act::ToggleOverride => self.toggle_override(&target, |o| &mut o.enabled, enable_defaults),
            Act::ToggleOsd | Act::ToggleNotifications | Act::ToggleAutohide | Act::IdleAmbient | Act::IdleScreensaver => self.edit(|config| {
                if target.is_empty() {
                    flip(region.act, config, None);
                } else {
                    let mut o = config.monitor_overrides.remove(&target).unwrap_or_default();
                    flip(region.act, config, Some(&mut o));
                    config.monitor_overrides.insert(target.clone(), o);
                }
            }),
            Act::IdleEnable => self.edit(|config| config.idle_enabled = !config.idle_enabled),
            Act::ToggleExpandWidgets => self.edit(|config| config.expand_widgets = !config.expand_widgets),
            Act::IdleAmbientReset => self.edit(|config| {
                let timeout = config.ambient_timeout;
                config.monitor_overrides.entry(target.clone()).or_default().ambient_timeout = timeout;
            }),
            Act::IdleScreensaverReset => self.edit(|config| {
                let timeout = config.screensaver_timeout;
                config.monitor_overrides.entry(target.clone()).or_default().screensaver_timeout = timeout;
            }),
            Act::BarStyle => {
                if let Some(&style) = usize::try_from(region.a).ok().and_then(|i| BarStyle::ALL.get(i)) {
                    self.edit(|config| config.bar_style = style);
                }
            }
            Act::RainMode => self.edit(|config| config.rain.mode = if region.a == 1 { RainMode::Stiletto } else { RainMode::Matrix }),
            Act::RainAsync => self.edit(|config| config.rain.async_speed = !config.rain.async_speed),
            Act::VisualizerShape => {
                self.edit(|config| config.visualizer.visualizer_shape = if region.a == 1 { VisualizerShape::Sphere } else { VisualizerShape::Bar })
            }
            Act::VisualizerReset => self.edit(|config| {
                let defaults = VisualizerParams::default();
                let v = &mut config.visualizer;
                match Field::from_index(region.a) {
                    Field::VisualizerFps => v.fps = defaults.fps,
                    Field::VisualizerThin => v.particle_thin = defaults.particle_thin,
                    Field::VisualizerSize => v.particle_size = defaults.particle_size,
                    Field::VisualizerComplexity => v.fractal_complexity = defaults.fractal_complexity,
                    Field::VisualizerGlowDirections => v.glow_directions = defaults.glow_directions,
                    Field::VisualizerGlowQuality => v.glow_quality = defaults.glow_quality,
                    _ => {}
                }
            }),
            Act::AnimationDisable => self.edit(|config| config.animations_disabled = !config.animations_disabled),
            _ => self.dispatch_wallpaper(region),
        }
    }

    /// Flips the override switch picked by `switch` on `target`; turning it on first copies the
    /// defaults it covers through `copy`.
    fn toggle_override(&mut self, target: &str, switch: fn(&mut MonitorOverride) -> &mut bool, copy: fn(&mut MonitorOverride, &Config)) {
        self.edit(|config| {
            let defaults = config.clone();
            let o = config.monitor_overrides.entry(target.to_string()).or_default();
            if !*switch(o) {
                copy(o, &defaults);
            }
            let on = switch(o);
            *on = !*on;
        });
    }

    fn dispatch_wallpaper(&mut self, region: Region) {
        let slot = slot_from(region.b);
        let (picker_region, picker_column) = (self.picker(slot).region.clone(), self.picker(slot).column);
        match region.act {
            Act::WallpaperDefault => self.edit(|config| config.default_wallpaper = !config.default_wallpaper),
            Act::WallpaperAnimated => self.edit(|config| config.wallpaper_animated_enabled = !config.wallpaper_animated_enabled),
            Act::LockWallpaperMode => self.edit(|config| config.lock_wallpaper_custom = region.a == 1),
            Act::WallpaperRegion => {
                let monitor = self.monitor(region.a);
                if !monitor.is_empty() {
                    let target = self.picker_mut(slot_from(region.b >> 8));
                    target.region = monitor;
                    target.column = region.b & 0xff;
                }
            }
            Act::WallpaperPick => {
                if let Some(path) = usize::try_from(region.a).ok().and_then(|i| self.picker(slot).files.get(i)).cloned() {
                    self.edit(|config| wallpaper_set_column(config, &picker_region, picker_column, &path, slot));
                }
            }
            Act::WallpaperRemove => self.edit(|config| wallpaper_set_column(config, &picker_region, picker_column, "", slot)),
            Act::WallpaperFill => self.edit(|config| wallpaper_set_fill_mode(config, &picker_region, picker_column, if region.a == 1 { "fit" } else { "crop" }, slot)),
            Act::WallpaperCount => self.edit(|config| {
                let count = (wallpaper_column_count(config, &picker_region, slot) + region.a).clamp(1, cfg::MAX_COLUMNS);
                wallpaper_set_column_count(config, &picker_region, count, slot);
            }),
            Act::WallpaperRescan => self.rescan(slot),
            Act::DropdownToggle => self.dropdown_open = if self.dropdown_open == Some(region.a) { None } else { Some(region.a) },
            Act::DropdownDismiss => self.dropdown_open = None,
            Act::CapturePick => {
                let monitor = self.monitor(region.a);
                self.edit(|config| {
                    config.capture_column = if monitor.is_empty() { 0 } else { region.b };
                    config.capture_monitor.clone_from(&monitor);
                });
            }
            Act::PaletteSource => {
                self.edit(|config| config.palette_source = if region.a == 1 { PaletteSource::RegionCapture } else { PaletteSource::DefaultPalette })
            }
            _ => {}
        }
    }

    pub fn tabs(&self) -> &[TabInfo] {
        &self.tabs
    }

    pub fn active_tab(&self) -> Tab {
        self.active
    }

    pub fn in_menu(&self) -> bool {
        self.menu
    }

    pub fn tab_alpha(&self) -> f32 {
        self.tab_alpha.get()
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    pub fn sequence(&self) -> &ScrollSequence {
        &self.sequence
    }

    pub fn focused(&self) -> Field {
        self.focused
    }

    pub fn field(&self) -> &TextFieldState {
        &self.field
    }

    pub fn field_anim(&self) -> &TextFieldTypeAnim {
        &self.field_anim
    }

    pub fn config(&self) -> Ref<'_, Config> {
        self.hooks.store.config()
    }

    pub fn monitors(&self) -> &[String] {
        &self.monitors
    }

    pub fn displays_monitor(&self) -> &str {
        &self.displays_monitor
    }

    pub fn picker(&self, slot: WallpaperSlot) -> &WallpaperPicker {
        match slot {
            WallpaperSlot::Still => &self.static_picker,
            WallpaperSlot::Animated => &self.animated_picker,
            WallpaperSlot::Lock => &self.lock_picker,
        }
    }

    pub fn picker_mut(&mut self, slot: WallpaperSlot) -> &mut WallpaperPicker {
        match slot {
            WallpaperSlot::Still => &mut self.static_picker,
            WallpaperSlot::Animated => &mut self.animated_picker,
            WallpaperSlot::Lock => &mut self.lock_picker,
        }
    }

    pub fn cpu_fallback(&self, monitor: &str, column: i32) -> bool {
        (self.hooks.cpu_fallback)(monitor, column)
    }
}
