use std::cell::{Cell, Ref, RefCell};
use std::path::PathBuf;
use std::rc::Rc;
use std::time::{Duration, Instant};

use calloop::EventLoop;

use super::model::Model;
use super::view::paint;
use crate::base::animation::set_instant;
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::settings::Config;
use crate::config::wallpaper_select_config as cfg;
use crate::render::geometry::Rect;
use crate::render::recording_canvas::{OpKind, RecordingCanvas};
use crate::service::settings::ConfigStore;
use crate::service::wallpaper::wallpaper_column_override_animated;

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
    closed: i32,
}

impl Env {
    fn new() -> Env {
        let store = Rc::new(Store::default());
        Env { model: Model::new(store.clone()), store, closed: 0 }
    }

    fn config(&self) -> std::cell::RefMut<'_, Config> {
        self.store.config.borrow_mut()
    }

    fn updates(&self) -> i32 {
        self.store.updates.get()
    }

    /// What the host does after every call: a requested close plays, a finished close is counted.
    fn settle(&mut self) {
        if self.model.take_close_request() {
            self.model.close();
        }
        if self.model.take_closed() {
            self.closed += 1;
        }
    }

    fn key(&mut self, kind: KeyKind) {
        self.model.key(&KeyEvent { kind, ..KeyEvent::default() });
        self.settle();
    }

    fn click(&mut self, x: f64, y: f64) {
        self.model.click(x, y);
        self.settle();
    }

    fn close(&mut self) {
        self.model.close();
        self.settle();
    }

    fn tick(&mut self, at: Instant) {
        self.model.tick(at);
        self.settle();
    }
}

struct Folder(tempfile::TempDir);

impl Folder {
    fn new() -> Folder {
        let root = tempfile::Builder::new().prefix("astralia_test_wallpaper_select_").tempdir().unwrap();
        for name in ["c.png", "a.png", "b.jpg", "notes.txt"] {
            std::fs::write(root.path().join(name), "x").unwrap();
        }
        Folder(root)
    }

    fn root(&self) -> PathBuf {
        self.0.path().to_path_buf()
    }

    fn file(&self, name: &str) -> String {
        self.0.path().join(name).to_string_lossy().into_owned()
    }
}

/// Runs this thread's loop, so the scan finishes and its result arrives.
struct Pump(EventLoop<'static, ()>);

impl Pump {
    fn new() -> Pump {
        let event_loop = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        Pump(event_loop)
    }

    fn scanned(&mut self, model: &mut Model) -> bool {
        let deadline = Instant::now() + Duration::from_secs(10);
        while Instant::now() < deadline {
            self.0.dispatch(Some(Duration::from_millis(5)), &mut ()).unwrap();
            model.poll();
            if !model.scanning() {
                return true;
            }
        }
        false
    }
}

fn opened(pump: &mut Pump, env: &mut Env, folder: &Folder) -> bool {
    env.config().wallpaper_dir = folder.root().to_string_lossy().into_owned();
    env.config().wallpaper_columns.insert("DP-1".into(), vec![folder.file("b.jpg")]);
    env.model.open("DP-1", 0);
    pump.scanned(&mut env.model)
}

#[test]
fn wallpaper_select_listing() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    assert!(opened(&mut pump, &mut env, &folder), "the scan finishes");
    assert_eq!(
        env.model.files(),
        [folder.file("b.jpg"), folder.file("a.png"), folder.file("c.png")],
        "images sort by extension then name and other files are skipped"
    );
    assert!(
        env.model.is_open() && env.model.selected() == 0 && env.model.appear() == 1.0,
        "the current wallpaper starts selected and the cards are shown"
    );

    env.config().wallpaper_columns.insert("DP-1".into(), vec![folder.file("c.png")]);
    env.close();
    env.model.open("DP-1", 0);
    assert!(pump.scanned(&mut env.model) && env.model.selected() == 2, "a wallpaper inside the folder is found by path");

    env.close();
    env.config().wallpaper_columns.insert("DP-1".into(), vec!["/elsewhere/z.png".into()]);
    env.model.open("DP-1", 0);
    assert!(
        pump.scanned(&mut env.model) && env.model.files().len() == 4 && env.model.files()[0] == "/elsewhere/z.png" && env.model.selected() == 0,
        "a wallpaper outside the folder is put in front"
    );
}

#[test]
fn wallpaper_select_stepping() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    opened(&mut pump, &mut env, &folder);
    env.model.step(1);
    assert!(env.model.selected() == 1 && env.model.position() == 1.0, "stepping moves the selection and the scroller");
    env.model.step(10);
    assert_eq!(env.model.selected(), 2, "stepping stops at the last image");
    env.model.step(-10);
    assert_eq!(env.model.selected(), 0, "stepping stops at the first image");

    env.model.scroll(0.6);
    assert_eq!(env.model.selected(), 0, "a part of a wheel notch does not step");
    env.model.scroll(0.6);
    assert_eq!(env.model.selected(), 1, "wheel notches add up to a step");
    env.model.scroll(-1.2);
    assert_eq!(env.model.selected(), 0, "scrolling back steps back, the remainder carried over");
    env.model.scroll(2.0 * cfg::WHEEL_NOTCH);
    assert_eq!(env.model.selected(), 2, "a fast wheel steps more than once");

    env.key(KeyKind::Home);
    assert_eq!(env.model.selected(), 0, "Home selects the first image");
    env.key(KeyKind::Right);
    assert_eq!(env.model.selected(), 1, "Right steps forward");
    env.key(KeyKind::Left);
    assert_eq!(env.model.selected(), 0, "Left steps back");
    env.key(KeyKind::End);
    assert_eq!(env.model.selected(), 2, "End selects the last image");
}

#[test]
fn wallpaper_select_apply() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    opened(&mut pump, &mut env, &folder);
    env.model.step(1);
    env.key(KeyKind::Enter);
    assert!(
        env.updates() == 1 && wallpaper_column_override_animated(&env.store.config(), "DP-1", 0, false) == folder.file("a.png"),
        "Enter sets the wallpaper of the opened column"
    );
    assert!(!env.model.is_open() && env.closed == 1, "applying closes the selector");

    env.model.open("DP-1", 0);
    pump.scanned(&mut env.model);
    env.key(KeyKind::Enter);
    assert!(env.updates() == 1 && !env.model.is_open(), "applying the current wallpaper changes nothing");

    env.model.open("DP-1", 0);
    pump.scanned(&mut env.model);
    env.key(KeyKind::End);
    env.key(KeyKind::Escape);
    assert!(env.updates() == 1 && !env.model.is_open() && env.closed == 3, "Escape closes without a change");
}

#[test]
fn wallpaper_select_clicks() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    opened(&mut pump, &mut env, &folder);
    env.model.begin_frame();
    env.model.add_card(Rect { x: 0.0, y: 0.0, w: 10.0, h: 10.0 }, 0);
    env.model.add_card(Rect { x: 20.0, y: 0.0, w: 10.0, h: 10.0 }, 1);
    assert!(env.model.clickable(25.0, 5.0) && !env.model.clickable(15.0, 5.0), "cards are clickable, the gap is not");
    env.click(25.0, 5.0);
    assert!(env.model.selected() == 1 && env.updates() == 0, "clicking a side card selects it");
    env.click(25.0, 5.0);
    assert!(env.updates() == 1 && !env.model.is_open(), "clicking the selected card applies it");

    opened(&mut pump, &mut env, &folder);
    env.model.begin_frame();
    env.model.add_card(Rect { x: 0.0, y: 0.0, w: 10.0, h: 10.0 }, 0);
    env.click(500.0, 500.0);
    assert!(!env.model.is_open() && env.updates() == 1, "clicking outside the cards closes without a change");
}

#[test]
fn wallpaper_select_animated() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    std::fs::write(folder.root().join("y.mp4"), "x").unwrap();
    std::fs::write(folder.root().join("x.webm"), "x").unwrap();
    let mut env = Env::new();
    {
        let mut c = env.config();
        c.wallpaper_animated_enabled = true;
        c.wallpaper_animated_dir = folder.root().to_string_lossy().into_owned();
        c.wallpaper_animated_columns.insert("DP-1".into(), vec![String::new(), folder.file("y.mp4")]);
        c.wallpaper_animated_column_counts.insert("DP-1".into(), 2);
    }
    env.model.open("DP-1", 1);
    assert!(pump.scanned(&mut env.model), "the animated scan finishes");
    assert!(env.model.animated() && env.model.column_count() == 2, "the animated column count is used");
    assert_eq!(env.model.files(), [folder.file("y.mp4"), folder.file("x.webm")], "only videos are listed, starting from the current one");
    env.model.step(1);
    env.key(KeyKind::Enter);
    assert!(
        wallpaper_column_override_animated(&env.store.config(), "DP-1", 1, true) == folder.file("x.webm") && env.store.config().wallpaper_columns.is_empty(),
        "applying sets the animated column and leaves the static ones alone"
    );
}

#[test]
fn wallpaper_select_column_at() {
    let still = Env::new();
    still.config().wallpaper_column_counts.insert("DP-1".into(), 3);
    assert!(
        still.model.column_at("DP-1", 0.0, 0.0, 900.0, 500.0) == 0
            && still.model.column_at("DP-1", 450.0, 10.0, 900.0, 500.0) == 1
            && still.model.column_at("DP-1", 899.0, 499.0, 900.0, 500.0) == 2,
        "the pointer picks the column it is over"
    );
    assert!(
        still.model.column_at("DP-1", -1.0, 10.0, 900.0, 500.0) == 0
            && still.model.column_at("DP-1", 900.0, 10.0, 900.0, 500.0) == 0
            && still.model.column_at("DP-1", 450.0, 500.0, 900.0, 500.0) == 0,
        "a pointer off the monitor falls back to the first column"
    );
    assert_eq!(still.model.column_at("HDMI-1", 899.0, 10.0, 900.0, 500.0), 0, "a single column monitor always resolves to the first column");

    let animated = Env::new();
    {
        let mut c = animated.config();
        c.wallpaper_animated_enabled = true;
        c.wallpaper_column_counts.insert("DP-1".into(), 4);
        c.wallpaper_animated_column_counts.insert("DP-1".into(), 2);
    }
    assert_eq!(animated.model.column_at("DP-1", 600.0, 10.0, 900.0, 500.0), 1, "the animated column count divides the monitor when animated is on");
}

#[test]
fn wallpaper_select_view() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    opened(&mut pump, &mut env, &folder);
    let mut canvas = RecordingCanvas::default();
    paint(&mut canvas, &mut env.model, 1920.0, 1080.0);
    let groups = canvas.ops.iter().filter(|op| op.kind == OpKind::BeginGroup).count();
    assert_eq!(groups, 4, "the current wallpaper and its neighbours are drawn inside the column clip");
    let covered = canvas.ops.iter().any(|op| op.kind == OpKind::Rect && op.rect.w >= 1920.0 && op.rect.h >= 1080.0);
    assert!(!covered, "nothing covers the wallpaper behind the cards");
    assert!(env.model.clickable(960.0, 500.0), "the cards register for clicks");
    let label = canvas.ops.iter().any(|op| op.kind == OpKind::Text && op.text.contains("b.jpg"));
    assert!(label, "the file name is shown");
}

#[test]
fn wallpaper_select_appear() {
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    env.config().wallpaper_dir = folder.root().to_string_lossy().into_owned();
    env.config().wallpaper_columns.insert("DP-1".into(), vec![folder.file("b.jpg")]);
    env.model.open("DP-1", 0);
    assert!(env.model.appear() == 0.0 && env.model.animating(), "opening starts the cards hidden and animating in");
    env.tick(Instant::now() + Duration::from_millis(cfg::APPEAR_MS as u64 + 50));
    assert!(env.model.appear() == 1.0 && env.model.is_open(), "the cards are fully shown after the appear time");
    pump.scanned(&mut env.model);

    env.model.step(1);
    env.tick(Instant::now() + Duration::from_millis(cfg::STEP_MS as u64 + 50));
    env.key(KeyKind::Enter);
    assert!(env.updates() == 1 && env.model.closing() && env.closed == 0, "applying sets the wallpaper at once and fades the cards out");
    env.tick(Instant::now() + Duration::from_millis(cfg::APPEAR_MS as u64 + 50));
    assert!(!env.model.is_open() && env.closed == 1, "the selector closes once the cards are gone, without waiting for the wallpaper");
}

#[test]
fn wallpaper_select_target_resolution() {
    let _snap = Snap::on();
    let mut pump = Pump::new();
    let folder = Folder::new();
    let mut env = Env::new();
    opened(&mut pump, &mut env, &folder);
    env.model.step(1);
    let mut canvas = RecordingCanvas::default();
    paint(&mut canvas, &mut env.model, 1920.0, 1080.0);
    let mut sharp = 0;
    for (_, px) in &canvas.thumbnails {
        sharp += i32::from(*px == cfg::CENTRE_PX);
        assert!(*px == cfg::CENTRE_PX || *px == cfg::NEIGHBOUR_PX, "thumbnails use only the centre and neighbour sizes");
    }
    assert_eq!(sharp, 1, "only the selected card asks for the sharp size");
    assert_eq!(canvas.thumbnails[0].0, folder.file("a.png"), "the selected card is requested first");
}
