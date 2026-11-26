use std::cell::{Cell, Ref, RefCell, RefMut};
use std::rc::Rc;
use std::time::{Duration, Instant};

use calloop::EventLoop;

use super::model::{Act, Field, Hooks, Model, Tab, apply_field, field_text};
use super::view::{Art, paint};
use crate::base::animation::set_instant;
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::file::collapse_home;
use crate::config::settings::{BarStyle, Config, PaletteSource, RainMode, VisualizerShape};
use crate::config::settings_config as cfg;
use crate::render::geometry::Rect;
use crate::render::recording_canvas::{OpKind, RecordingCanvas};
use crate::service::settings::ConfigStore;
use crate::service::wallpaper::{WallpaperSlot, wallpaper_column_count, wallpaper_column_override, wallpaper_fill_mode};

struct Snap;

impl Snap {
    fn on() -> Snap {
        set_instant(true);
        Snap
    }
}

impl Drop for Snap {
    fn drop(&mut self) {
        set_instant(false);
    }
}

#[derive(Default)]
struct Store {
    config: RefCell<Config>,
    updates: Cell<i32>,
}

impl ConfigStore for Store {
    fn config(&self) -> Ref<'_, Config> {
        self.config.borrow()
    }

    fn update(&self, edit: &mut dyn FnMut(&mut Config)) {
        edit(&mut self.config.borrow_mut());
        self.updates.set(self.updates.get() + 1);
    }
}

struct Env {
    store: Rc<Store>,
    model: Model,
    closes: i32,
    closed: i32,
}

impl Env {
    fn new() -> Env {
        let store = Rc::new(Store::default());
        let hooks = Hooks {
            store: store.clone(),
            monitors: Box::new(|| vec!["DP-1".to_string(), "HDMI-1".to_string()]),
            focused_monitor: Box::new(|| "HDMI-1".to_string()),
            cpu_fallback: Box::new(|_, _| false),
        };
        Env { model: Model::new(hooks), store, closes: 0, closed: 0 }
    }

    fn config(&self) -> RefMut<'_, Config> {
        self.store.config.borrow_mut()
    }

    fn cfg(&self) -> Ref<'_, Config> {
        self.store.config.borrow()
    }

    /// What the host does after every call: count close requests and finished closes.
    fn settle(&mut self) {
        if self.model.take_close_request() {
            self.closes += 1;
        }
        if self.model.take_closed() {
            self.closed += 1;
        }
    }

    fn click_at(&mut self, x: f64, y: f64) {
        self.model.click(x, y);
        self.settle();
    }

    fn click(&mut self, act: Act, a: i32, b: i32) {
        self.model.begin_frame();
        self.model.add_region(Rect { x: 0.0, y: 0.0, w: 10.0, h: 10.0 }, act, a, b);
        self.click_at(5.0, 5.0);
    }

    fn key(&mut self, kind: KeyKind, text: &str) -> bool {
        let used = self.model.key(&KeyEvent { kind, text: text.to_string(), ..KeyEvent::default() });
        self.settle();
        used
    }

    fn tick(&mut self, at: Instant) {
        self.model.tick(at);
        self.settle();
    }

    fn close(&mut self) {
        self.model.close();
        self.settle();
    }

    fn texts(&mut self, width: f32, height: f32) -> Vec<String> {
        let mut canvas = RecordingCanvas::default();
        self.model.begin_frame();
        paint(&mut canvas, &mut self.model, &art(), width, height);
        canvas.ops.into_iter().filter(|op| op.kind == OpKind::Text).map(|op| op.text).collect()
    }
}

fn art() -> Art {
    Art { user_name: "someone".into(), uptime: "up 3 hours".into(), face: String::new() }
}

fn later(seconds: u64) -> Instant {
    Instant::now() + Duration::from_secs(seconds)
}

fn count(canvas: &RecordingCanvas, kind: OpKind) -> usize {
    canvas.ops.iter().filter(|op| op.kind == kind).count()
}

fn home() -> String {
    std::env::var("HOME").unwrap_or_default()
}

/// Runs this thread's loop, so scans finish and their results arrive.
struct Pump(EventLoop<'static, ()>);

impl Pump {
    fn new() -> Pump {
        let event_loop = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        Pump(event_loop)
    }

    fn scanned(&mut self, model: &mut Model, slot: WallpaperSlot) -> bool {
        let deadline = Instant::now() + Duration::from_secs(10);
        while Instant::now() < deadline {
            self.0.dispatch(Some(Duration::from_millis(5)), &mut ()).unwrap();
            model.poll();
            if !model.picker(slot).scanning {
                return true;
            }
        }
        false
    }
}

#[test]
fn settings_fields() {
    let mut config = Config::default();
    apply_field(&mut config, Field::AmbientTimeout, "5", "");
    assert_eq!(config.ambient_timeout, cfg::IDLE_TIMEOUT_MIN as u32, "timeouts clamp to the minimum");
    apply_field(&mut config, Field::AmbientTimeout, "9999", "");
    assert_eq!(config.ambient_timeout, cfg::IDLE_TIMEOUT_MAX as u32, "and to the maximum");
    apply_field(&mut config, Field::ScreensaverTimeout, "200", "DP-1");
    config.monitor_overrides.entry("DP-1".into()).or_default().enabled = true;
    assert!(config.monitor_overrides["DP-1"].screensaver_timeout == 200 && config.screensaver_timeout == 300, "monitor fields write the override");
    apply_field(&mut config, Field::VisualizerFps, "abc", "");
    assert_eq!(config.visualizer.fps, 60, "unparsable text keeps the value");
    apply_field(&mut config, Field::VisualizerThin, "2", "");
    assert_eq!(config.visualizer.particle_thin, 0.95, "floats clamp");
    apply_field(&mut config, Field::WallpaperDir, "/tmp/x", "");
    assert_eq!(config.wallpaper_dir, "/tmp/x", "directories are stored");
    apply_field(&mut config, Field::LockWallpaperDir, "/tmp/lock", "");
    assert!(
        config.lock_wallpaper_dir == "/tmp/lock" && config.wallpaper_dir == "/tmp/x" && field_text(&config, Field::LockWallpaperDir, "") == "/tmp/lock",
        "the lock directory is its own field"
    );
    assert_eq!(field_text(&config, Field::VisualizerThin, ""), "0.95", "floats print without trailing zeros");
    assert_eq!(field_text(&config, Field::VisualizerFps, ""), "60", "integers print plainly");
    assert_eq!(field_text(&config, Field::ScreensaverTimeout, "DP-1"), "200", "monitor values read the override");
}

#[test]
fn settings_tabs() {
    let _snap = Snap::on();
    let tabs = |env: &Env| env.model.tabs().iter().map(|t| t.tab).collect::<Vec<_>>();
    let mut minimal = Env::new();
    assert_eq!(
        tabs(&minimal),
        [Tab::General, Tab::Ambient, Tab::Bar, Tab::Palette, Tab::Displays, Tab::Lock, Tab::Wallpaper],
        "general leads, and the lock screen tab sits between displays and wallpaper"
    );

    minimal.model.open();
    assert!(minimal.model.in_menu(), "the overlay opens on the tile menu");
    minimal.click(Act::Tab, Tab::Displays as i32, 0);
    assert!(!minimal.model.in_menu(), "picking a tile opens its page");
    minimal.click(Act::Menu, 0, 0);
    assert!(minimal.model.in_menu(), "the back key returns to the menu");
    minimal.click(Act::Tab, Tab::Displays as i32, 0);
    assert!(minimal.model.active_tab() == Tab::Displays && minimal.model.tab_alpha() == 1.0, "a tab switch settles at once when instant");
    minimal.close();
    minimal.model.open();
    assert!(minimal.model.in_menu() && minimal.model.active_tab() == Tab::Wallpaper, "reopening starts over");

    let mut keeper = Env::new();
    keeper.model.open();
    keeper.key(KeyKind::Down, "");
    assert!(keeper.model.in_menu(), "arrows do nothing on the menu");
    keeper.click(Act::Tab, Tab::Wallpaper as i32, 0);
    keeper.key(KeyKind::Down, "");
    assert_eq!(keeper.model.active_tab(), Tab::Wallpaper, "down stops at the last tab");
    keeper.key(KeyKind::Up, "");
    assert_eq!(keeper.model.active_tab(), Tab::Lock, "up moves to the previous tab");
    keeper.key(KeyKind::Down, "");
    assert_eq!(keeper.model.active_tab(), Tab::Wallpaper, "down moves to the next tab");
    keeper.click(Act::Tab, Tab::Palette as i32, 0);
    keeper.click(Act::PaletteSource, 1, 0);
    assert_eq!(keeper.cfg().palette_source, PaletteSource::RegionCapture, "the palette source switches to region capture");
    keeper.click(Act::PaletteSource, 0, 0);
    assert_eq!(keeper.cfg().palette_source, PaletteSource::DefaultPalette, "the palette source switches back to the default");
    let shown = keeper.texts(1920.0, 1200.0);
    assert!(
        shown.iter().any(|t| t == "Default") && !shown.iter().any(|t| t == "Region Capture"),
        "the palette page paints and hides Region Capture on the default source"
    );
    keeper.click(Act::PaletteSource, 1, 0);
    let shown = keeper.texts(1920.0, 1200.0);
    assert!(shown.iter().any(|t| t == "Region Capture"), "Region Capture shows on the region source");
    keeper.click(Act::PaletteSource, 0, 0);

    keeper.closes = 0;
    keeper.key(KeyKind::Escape, "");
    assert!(keeper.closes == 0 && keeper.model.in_menu(), "escape on a page returns to the menu");
    keeper.key(KeyKind::Escape, "");
    assert_eq!(keeper.closes, 1, "escape on the menu asks to close");
    keeper.model.begin_frame();
    keeper.model.set_panel(Rect { x: 100.0, y: 100.0, w: 200.0, h: 200.0 });
    keeper.click_at(10.0, 10.0);
    assert_eq!(keeper.closes, 2, "a click outside the card asks to close");
    keeper.click_at(150.0, 150.0);
    assert_eq!(keeper.closes, 2, "a click inside the card does not");
}

#[test]
fn settings_tab_fade() {
    let mut animated = Env::new();
    animated.model.open();
    animated.tick(later(1));
    animated.tick(later(2));
    animated.tick(later(3));
    animated.click(Act::Tab, Tab::Bar as i32, 0);
    assert!(animated.model.active_tab() == Tab::Wallpaper && animated.model.animating(), "a tab switch fades out first");
    animated.tick(later(4));
    animated.tick(later(5));
    assert!(animated.model.active_tab() == Tab::Bar && animated.model.tab_alpha() == 1.0, "then fades the new tab in");
}

#[test]
fn settings_scroll_stages() {
    let mut env = Env::new();
    env.model.open();
    assert!(env.model.is_open() && env.model.sequence().slide() == 1.0 && env.model.animating(), "the closed scroll starts below the screen");
    let mut rising = RecordingCanvas::default();
    paint(&mut rising, &mut env.model, &art(), 1920.0, 1200.0);
    assert!(
        count(&rising, OpKind::Rounded) > 0 && count(&rising, OpKind::Text) == 0 && !env.model.clickable(960.0, 600.0),
        "the closed scroll shows no content"
    );
    env.click_at(5.0, 5.0);
    assert_eq!(env.closes, 0, "clicks are ignored before the scroll is open");

    env.tick(later(1));
    env.tick(later(2));
    assert!(env.model.sequence().settled() && env.model.sequence().grid() == 0.0, "the scroll unrolls before the content");
    let mut unrolled = RecordingCanvas::default();
    paint(&mut unrolled, &mut env.model, &art(), 1920.0, 1200.0);
    assert_eq!(count(&unrolled, OpKind::Text), 0, "the open scroll is still empty");

    env.tick(later(3));
    let mut shown = RecordingCanvas::default();
    paint(&mut shown, &mut env.model, &art(), 1920.0, 1200.0);
    assert!(count(&shown, OpKind::Text) > 0 && count(&shown, OpKind::BeginGroup) == 0, "then the content appears");
    env.click_at(5.0, 5.0);
    assert_eq!(env.closes, 1, "and clicks work");

    env.close();
    assert!(env.model.is_open() && env.closed == 0, "closing keeps the overlay until the scroll has dropped");
    assert!(!env.key(KeyKind::Down, ""), "keys are ignored while closing");
    env.tick(later(4));
    let mut fading = RecordingCanvas::default();
    paint(&mut fading, &mut env.model, &art(), 1920.0, 1200.0);
    assert!(
        count(&fading, OpKind::Text) == 0 && env.model.sequence().scroll() == 1.0 && env.closed == 0,
        "the content goes before the scroll rolls up"
    );
    env.tick(later(5));
    env.tick(later(6));
    assert!(!env.model.is_open() && env.closed == 1, "the overlay closes once, after the drop");
}

#[test]
fn settings_actions() {
    let _snap = Snap::on();
    let mut env = Env::new();
    env.model.open();

    env.click(Act::BarStyle, 1, 0);
    assert_eq!(env.cfg().bar_style, BarStyle::Okinami, "the bar style tile applies");
    env.click(Act::ToggleExpandWidgets, 0, 0);
    assert!(env.cfg().expand_widgets, "expand widgets switches on");
    env.click(Act::ToggleExpandWidgets, 0, 0);
    assert!(!env.cfg().expand_widgets, "expand widgets switches off");
    env.click(Act::ToggleAutohide, 0, 0);
    env.click(Act::BarMonitor, 0, 0);
    assert!(env.model.bar_monitor() == "DP-1" && env.model.displays_monitor().is_empty(), "the bar tab keeps its own selection");
    env.click(Act::ToggleBarOverride, 0, 0);
    let dp = env.cfg().monitor_overrides["DP-1"].clone();
    assert!(dp.bar_override && !dp.enabled && dp.autohide, "the bar override starts from the bar defaults, apart from the display override");
    env.click(Act::ToggleBarDisabled, 0, 0);
    env.click(Act::ToggleExpandWidgets, 0, 0);
    let dp = env.cfg().monitor_overrides["DP-1"].clone();
    assert!(dp.bar_disabled && dp.expand_widgets && !env.cfg().bar_disabled && !env.cfg().expand_widgets, "bar toggles write the override");
    env.click(Act::ToggleBarOverride, 0, 0);
    assert!(!env.cfg().monitor_overrides["DP-1"].bar_override, "the bar override switches off");
    env.click(Act::BarMonitor, -1, 0);
    env.click(Act::ToggleBarDisabled, 0, 0);
    assert!(env.cfg().bar_disabled, "with Default selected the toggle flips the default");
    env.click(Act::ToggleBarDisabled, 0, 0);
    env.click(Act::ToggleAutohide, 0, 0);

    env.click(Act::ToggleOsd, 0, 0);
    assert!(!env.cfg().default_osd, "default toggles flip the default");
    env.click(Act::DisplaysMonitor, 0, 0);
    assert_eq!(env.model.displays_monitor(), "DP-1", "monitor tiles select a monitor");
    env.click(Act::ToggleOverride, 0, 0);
    assert!(env.cfg().monitor_overrides["DP-1"].enabled && !env.cfg().monitor_overrides["DP-1"].osd, "the override starts from the defaults");
    env.click(Act::ToggleNotifications, 0, 0);
    assert!(!env.cfg().monitor_overrides["DP-1"].notifications && env.cfg().default_notifications, "override toggles flip the override");
    env.click(Act::ToggleOverride, 0, 0);
    assert!(!env.cfg().monitor_overrides["DP-1"].enabled, "the override switches off");
    env.click(Act::DisplaysMonitor, -1, 0);
    assert!(env.model.displays_monitor().is_empty(), "the default tile clears the selection");

    env.click(Act::IdleEnable, 0, 1);
    assert!(!env.cfg().idle_enabled, "idle management flips");
    env.click(Act::DisplaysMonitor, 1, 0);
    assert_eq!(env.model.displays_monitor(), "HDMI-1", "idle shares the display selection");
    env.click(Act::ToggleOverride, 0, 1);
    env.click(Act::IdleAmbient, 0, 1);
    assert!(!env.cfg().monitor_overrides["HDMI-1"].ambient_enabled && env.cfg().ambient_enabled, "idle toggles write the override");
    env.config().monitor_overrides.get_mut("HDMI-1").unwrap().ambient_timeout = 20;
    env.click(Act::IdleAmbientReset, 0, 0);
    assert_eq!(env.cfg().monitor_overrides["HDMI-1"].ambient_timeout, env.cfg().ambient_timeout, "reset restores the default timeout");

    env.click(Act::RainMode, 1, 0);
    env.click(Act::RainAsync, 0, 0);
    assert!(env.cfg().rain.mode == RainMode::Stiletto && env.cfg().rain.async_speed, "rain settings apply");
    env.click(Act::VisualizerShape, 1, 0);
    env.config().visualizer.fps = 30;
    env.click(Act::VisualizerReset, Field::VisualizerFps as i32, 0);
    assert!(env.cfg().visualizer.visualizer_shape == VisualizerShape::Sphere && env.cfg().visualizer.fps == 60, "visualizer settings apply and reset");
    env.click(Act::AnimationDisable, 0, 0);
    assert!(env.cfg().animations_disabled, "single toggles flip");
    env.click(Act::WallpaperDefault, 0, 0);
    env.click(Act::WallpaperAnimated, 0, 0);
    assert!(!env.cfg().default_wallpaper && env.cfg().wallpaper_animated_enabled, "wallpaper mode toggles flip");
}

#[test]
fn settings_wallpaper() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let root = tempfile::Builder::new().prefix("astralia_test_settings_").tempdir().unwrap();
    let root_str = root.path().to_string_lossy().into_owned();
    std::fs::create_dir_all(root.path().join("sub")).unwrap();
    for name in ["b.png", "a.jpg", "notes.txt", "sub/c.png"] {
        std::fs::write(root.path().join(name), "x").unwrap();
    }

    let mut env = Env::new();
    env.config().wallpaper_dir = root_str.clone();
    env.model.open();
    env.click(Act::Tab, Tab::Wallpaper as i32, 0);
    assert!(
        env.model.picker(WallpaperSlot::Still).scanning && env.model.picker(WallpaperSlot::Still).region == "HDMI-1",
        "opening scans and picks the focused monitor"
    );
    assert!(pump.scanned(&mut env.model, WallpaperSlot::Still), "the scan finishes");
    let files = env.model.picker(WallpaperSlot::Still).files.clone();
    assert_eq!(files.len(), 3, "images are found one folder deep, other files are skipped");
    assert!(files[0].ends_with("a.jpg") && files[1].ends_with("b.png") && files[2].ends_with("c.png"), "files sort by extension then name");

    env.click(Act::WallpaperPick, 1, 0);
    assert_eq!(wallpaper_column_override(&env.cfg(), "HDMI-1", 0, WallpaperSlot::Still), files[1], "picking sets the column image");
    env.click(Act::WallpaperCount, 1, 0);
    assert_eq!(wallpaper_column_count(&env.cfg(), "HDMI-1", WallpaperSlot::Still), 2, "the stepper adds a column");
    env.click(Act::WallpaperRegion, 0, 1);
    assert!(
        env.model.picker(WallpaperSlot::Still).region == "DP-1" && env.model.picker(WallpaperSlot::Still).column == 1,
        "chips select a region and column"
    );
    env.click(Act::WallpaperFill, 1, 0);
    assert_eq!(wallpaper_fill_mode(&env.cfg(), "DP-1", 1, WallpaperSlot::Still), "fit", "fill mode applies");
    env.click(Act::DropdownToggle, 1, 0);
    assert_eq!(env.model.dropdown_open(), Some(1), "the drop-down opens");
    env.model.begin_frame();
    env.model.add_region(Rect { x: 0.0, y: 0.0, w: 10.0, h: 10.0 }, Act::WallpaperRemove, 0, 0);
    env.model.add_region_front(Rect { x: 0.0, y: 0.0, w: 10.0, h: 10.0 }, Act::CapturePick, 0, 1);
    env.click_at(5.0, 5.0);
    assert!(
        env.cfg().capture_monitor == "DP-1" && env.cfg().capture_column == 1 && env.model.dropdown_open().is_none(),
        "an open list row wins over content beneath and closes the list"
    );
    env.click(Act::CapturePick, -1, 0);
    assert!(env.cfg().capture_monitor.is_empty(), "Auto clears region capture");
    env.click(Act::DropdownToggle, 1, 0);
    env.click(Act::DropdownDismiss, 0, 0);
    assert!(env.model.dropdown_open().is_none(), "dismiss closes the list");
    env.click(Act::WallpaperRegion, 1, 0);
    env.click(Act::WallpaperRemove, 0, 0);
    assert!(wallpaper_column_override(&env.cfg(), "HDMI-1", 0, WallpaperSlot::Still).is_empty(), "remove clears the column");
    env.click(Act::WallpaperCount, -1, 0);
    env.click(Act::WallpaperCount, -1, 0);
    assert_eq!(wallpaper_column_count(&env.cfg(), "HDMI-1", WallpaperSlot::Still), 1, "the stepper stops at one");

    env.click(Act::FieldFocus, Field::WallpaperDir as i32, 0);
    assert!(
        env.model.focused() == Field::WallpaperDir && env.model.field().text == collapse_home(&root_str, &home()),
        "focusing a field loads its text"
    );
    for _ in 0..3 {
        env.key(KeyKind::Backspace, "");
    }
    env.key(KeyKind::Text, "x");
    assert!(env.model.field().text.ends_with('x'), "typing edits the buffer");
    env.key(KeyKind::Escape, "");
    assert!(env.model.focused() == Field::None && env.cfg().wallpaper_dir == root_str, "escape cancels the edit");
    env.click(Act::FieldFocus, Field::WallpaperDir as i32, 0);
    env.model.commit_text("/");
    env.key(KeyKind::Enter, "");
    assert!(env.model.focused() == Field::None && env.cfg().wallpaper_dir == format!("{root_str}/"), "enter commits the edit");

    env.close();
    assert!(env.model.picker(WallpaperSlot::Still).files.is_empty(), "closing drops the scanned files");
}

#[test]
fn settings_lock_wallpaper() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let root = tempfile::Builder::new().prefix("astralia_test_settings_lock_").tempdir().unwrap();
    let root_str = root.path().to_string_lossy().into_owned();
    for name in ["b.png", "a.jpg"] {
        std::fs::write(root.path().join(name), "x").unwrap();
    }

    let lock_flag = WallpaperSlot::Lock as i32;
    let mut env = Env::new();
    env.config().lock_wallpaper_dir = root_str.clone();
    env.model.open();
    env.click(Act::Tab, Tab::Lock as i32, 0);
    let picker = env.model.picker(WallpaperSlot::Lock);
    assert!(!picker.scanning && picker.dir.is_empty() && picker.files.is_empty(), "Auto mode loads nothing for the lock screen");

    env.click(Act::LockWallpaperMode, 1, 0);
    let picker = env.model.picker(WallpaperSlot::Lock);
    assert!(env.cfg().lock_wallpaper_custom && picker.scanning && picker.region == "HDMI-1", "Custom mode scans the lock directory");
    assert!(pump.scanned(&mut env.model, WallpaperSlot::Lock), "the lock scan finishes");
    let files = env.model.picker(WallpaperSlot::Lock).files.clone();
    assert!(files.len() == 2 && files[0].ends_with("a.jpg") && files[1].ends_with("b.png"), "the lock picker lists its own directory");
    let still = env.model.picker(WallpaperSlot::Still);
    assert!(!still.scanning && still.files.is_empty(), "the desktop picker is not touched");

    env.click(Act::WallpaperPick, 1, lock_flag);
    assert!(
        wallpaper_column_override(&env.cfg(), "HDMI-1", 0, WallpaperSlot::Lock) == files[1]
            && wallpaper_column_override(&env.cfg(), "HDMI-1", 0, WallpaperSlot::Still).is_empty(),
        "picking sets only the lock column"
    );
    env.click(Act::WallpaperCount, 1, lock_flag);
    assert!(
        wallpaper_column_count(&env.cfg(), "HDMI-1", WallpaperSlot::Lock) == 2 && wallpaper_column_count(&env.cfg(), "HDMI-1", WallpaperSlot::Still) == 1,
        "the stepper changes only the lock count"
    );
    env.click(Act::WallpaperRegion, 0, 1 | (lock_flag << 8));
    let (lock, still) = (env.model.picker(WallpaperSlot::Lock), env.model.picker(WallpaperSlot::Still));
    assert!(lock.region == "DP-1" && lock.column == 1 && still.region == "HDMI-1" && still.column == 0, "chips move only the lock selection");
    env.click(Act::WallpaperFill, 1, lock_flag);
    assert!(
        wallpaper_fill_mode(&env.cfg(), "DP-1", 1, WallpaperSlot::Lock) == "fit" && wallpaper_fill_mode(&env.cfg(), "DP-1", 1, WallpaperSlot::Still) == "crop",
        "fill mode applies to the lock slot only"
    );
    env.click(Act::WallpaperRegion, 1, lock_flag << 8);
    env.click(Act::WallpaperRemove, 0, lock_flag);
    assert!(wallpaper_column_override(&env.cfg(), "HDMI-1", 0, WallpaperSlot::Lock).is_empty(), "remove clears the lock column");

    env.click(Act::FieldFocus, Field::LockWallpaperDir as i32, 0);
    assert!(
        env.model.focused() == Field::LockWallpaperDir && env.model.field().text == collapse_home(&root_str, &home()),
        "the lock directory field loads its text"
    );
    env.model.commit_text("/");
    env.key(KeyKind::Enter, "");
    assert_eq!(env.cfg().lock_wallpaper_dir, format!("{root_str}/"), "enter commits the lock directory");

    env.click(Act::LockWallpaperMode, 0, 0);
    assert!(!env.cfg().lock_wallpaper_custom, "Auto mode switches back");

    env.close();
    let picker = env.model.picker(WallpaperSlot::Lock);
    assert!(picker.files.is_empty() && picker.dir.is_empty() && !picker.scanning, "closing clears the lock picker");
}

#[test]
fn settings_lock_view() {
    let _snap = Snap::on();
    let mut env = Env::new();
    env.model.open();
    env.click(Act::Tab, Tab::Lock as i32, 0);
    let has = |texts: &[String], wanted: &str| texts.iter().any(|t| t == wanted);

    let automatic = env.texts(1920.0, 1200.0);
    assert!(has(&automatic, "Auto") && has(&automatic, "Custom") && !has(&automatic, "Display"), "Auto mode shows only the selector");
    env.click(Act::LockWallpaperMode, 1, 0);
    let custom = env.texts(1920.0, 1200.0);
    assert!(has(&custom, "Display") && has(&custom, "Dir") && has(&custom, "Crop"), "Custom mode shows the column controls");
}

#[test]
fn settings_bar_view() {
    let _snap = Snap::on();
    let mut env = Env::new();
    env.model.open();
    env.click(Act::Tab, Tab::Bar as i32, 0);
    let has = |texts: &[String], wanted: &str| texts.iter().any(|t| t == wanted);

    let default = env.texts(1920.0, 1200.0);
    assert!(
        has(&default, "Display") && has(&default, "Disable Bar") && has(&default, "Autohide") && has(&default, "Expand Widgets") && has(&default, "Okinami"),
        "Default shows every bar setting"
    );
    assert!(!has(&default, "Override default settings"), "Default has no override switch");
    env.click(Act::BarMonitor, 0, 0);
    let plain = env.texts(1920.0, 1200.0);
    assert!(has(&plain, "Override default settings") && !has(&plain, "Disable Bar"), "a monitor without the override stops at the switch");
    env.click(Act::ToggleBarOverride, 0, 0);
    assert!(has(&env.texts(1920.0, 1200.0), "Autohide"), "an overridden monitor shows its settings");
    env.click(Act::ToggleBarDisabled, 0, 0);
    let disabled = env.texts(1920.0, 1200.0);
    assert!(has(&disabled, "Disable Bar") && !has(&disabled, "Autohide") && !has(&disabled, "Okinami"), "a disabled bar hides the rest");

    env.click(Act::Tab, Tab::Displays as i32, 0);
    assert!(!has(&env.texts(1920.0, 1200.0), "Bar Autohide"), "autohide left the Displays tab");
}

#[test]
fn settings_view() {
    let _snap = Snap::on();
    let mut env = Env::new();
    env.model.open();
    let mut canvas = RecordingCanvas::default();
    let frame = paint(&mut canvas, &mut env.model, &art(), 1920.0, 1200.0);
    assert!(frame.panel.w == cfg::CARD_MAX_WIDTH && frame.panel.h == cfg::CARD_MAX_HEIGHT, "a large output caps the card");
    assert_eq!(frame.panel.x, (1920.0 - cfg::CARD_MAX_WIDTH) / 2.0, "the card is centered");

    let mut small = RecordingCanvas::default();
    let narrow = paint(&mut small, &mut env.model, &art(), 600.0, 400.0);
    assert!(narrow.panel.w == 520.0 && narrow.panel.h == 320.0, "a small output leaves a margin");
    let name = canvas.ops.iter().any(|op| op.kind == OpKind::Text && op.text == "someone");
    assert!(name, "the menu shows the user name");
    let collapsed_name = small.ops.iter().any(|op| op.kind == OpKind::Text && op.text == "someone");
    assert!(collapsed_name, "also on a small output");

    paint(&mut canvas, &mut env.model, &art(), 1920.0, 1200.0);
    let close_x = frame.panel.x + frame.panel.w - cfg::CARD_PADDING - cfg::ACTION_BUTTON_SIZE / 2.0;
    let close_y = frame.panel.y + cfg::CARD_PADDING + cfg::HEADER_HEIGHT / 2.0;
    assert!(env.model.clickable(f64::from(close_x), f64::from(close_y)), "the close button is clickable");
    env.click_at(f64::from(close_x), f64::from(close_y));
    assert_eq!(env.closes, 1, "and closes");

    let tile_x = frame.panel.x + cfg::CARD_PADDING + 20.0;
    let mut found_tab = false;
    let mut y = frame.panel.y;
    while y < frame.panel.y + frame.panel.h {
        env.model.begin_frame();
        paint(&mut canvas, &mut env.model, &art(), 1920.0, 1200.0);
        if env.model.clickable(f64::from(tile_x), f64::from(y)) && y > frame.panel.y + 150.0 {
            env.click_at(f64::from(tile_x), f64::from(y));
            found_tab = !env.model.in_menu();
            break;
        }
        y += 4.0;
    }
    assert!(found_tab, "menu tiles are clickable");
}
