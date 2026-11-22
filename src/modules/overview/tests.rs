use std::cell::{Ref, RefCell};
use std::rc::Rc;
use std::time::{Duration, Instant};

use super::model::{Model, Tile};
use super::paging::{Grid, page_of, step, workspace_at};
use super::view;
use crate::base::animation::set_instant;
use crate::base::input::{KeyEvent, KeyKind};
use crate::base::signal::Signal;
use crate::config::overview_config as cfg;
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;
use crate::render::recording_canvas::{OpKind, RecordingCanvas};
use crate::render::scroll::{ScrollMetrics, paint_scroll, scroll_geometry};
use crate::render::scroll_sequence::ScrollTimings;
use crate::service::compositor::{CloseScope, Compositor, CompositorClient, CompositorMonitor, CompositorState};

#[derive(Default)]
struct FakeCompositor {
    state: RefCell<CompositorState>,
    calls: RefCell<Vec<String>>,
    active_changed: Signal,
    structure_changed: Signal,
}

impl FakeCompositor {
    fn last(&self) -> String {
        self.calls.borrow().last().cloned().unwrap_or_default()
    }

    fn call(&self, s: String) {
        self.calls.borrow_mut().push(s);
    }
}

fn global_suffix(global: bool) -> &'static str {
    if global { " global" } else { "" }
}

impl Compositor for FakeCompositor {
    fn name(&self) -> &'static str {
        "fake"
    }
    fn state(&self) -> Ref<'_, CompositorState> {
        self.state.borrow()
    }
    fn refresh(&self) {
        self.call("refresh".into());
    }
    fn refresh_clients(&self) -> bool {
        false
    }
    fn watch_client_order(&self, _: bool) {}
    fn focus_workspace(&self, id: i32, global: bool) {
        self.call(format!("focus {id}{}", global_suffix(global)));
    }
    fn move_window(&self, address: &str, id: i32, global: bool) {
        self.call(format!("move {address} {id}{}", global_suffix(global)));
    }
    fn close_window(&self, address: &str) {
        self.call(format!("close {address}"));
    }
    fn close_windows(&self, scope: CloseScope, id: i32) {
        let name = match scope {
            CloseScope::Workspace => "workspace",
            CloseScope::Monitor => "monitor",
            CloseScope::All => "all",
        };
        self.call(format!("close-windows {name} {id}"));
    }
    fn move_workspace_in(&self, id: i32, global: bool) {
        self.call(format!("move-in {id}{}", global_suffix(global)));
    }
    fn swap_workspace(&self, id: i32, global: bool) {
        self.call(format!("swap {id}{}", global_suffix(global)));
    }
    fn active_changed(&self) -> &Signal {
        &self.active_changed
    }
    fn structure_changed(&self) -> &Signal {
        &self.structure_changed
    }
}

struct InstantGuard;

impl InstantGuard {
    fn new() -> InstantGuard {
        set_instant(true);
        InstantGuard
    }
}

impl Drop for InstantGuard {
    fn drop(&mut self) {
        set_instant(false);
    }
}

fn key(kind: KeyKind, text: &str, base: char, shift: bool, alt: bool, ctrl: bool) -> KeyEvent {
    KeyEvent { kind, text: text.into(), base_sym: base as u32, shift, alt, ctrl }
}

fn plain(kind: KeyKind) -> KeyEvent {
    key(kind, "", '\0', false, false, false)
}

fn monitor(id: i32, name: &str, x: f64, y: f64, width: f64, height: f64) -> CompositorMonitor {
    CompositorMonitor { id, name: name.into(), x, y, width, height, ..CompositorMonitor::default() }
}

fn client(address: &str, workspace: i32, x: f64, y: f64, w: f64, h: f64) -> CompositorClient {
    CompositorClient { address: address.into(), window_class: "app".into(), workspace_id: workspace, monitor_id: 1, at: [x, y], size: [w, h], ..CompositorClient::default() }
}

fn single() -> Rc<FakeCompositor> {
    let compositor = Rc::new(FakeCompositor::default());
    {
        let mut s = compositor.state.borrow_mut();
        let mut m = monitor(1, "A", 0.0, 0.0, 1366.0, 768.0);
        m.reserved = [0.0, 0.0, 0.0, 20.0];
        s.monitors = vec![m];
        s.focused_monitor = "A".into();
        s.by_monitor.entry("A".into()).or_default().active_id = 1;
    }
    compositor
}

fn set_active(compositor: &FakeCompositor, name: &str, id: i32) {
    compositor.state.borrow_mut().by_monitor.entry(name.into()).or_default().active_id = id;
}

fn no_art<C: Canvas>() -> impl FnMut(&mut C, &Tile, Rect, f32) -> bool {
    |_, _, _, _| false
}

#[test]
fn overview_paging() {
    let grid = Grid { columns: 5, rows: 2 };
    assert_eq!(grid.per_page(), 10, "a page holds columns times rows");
    assert_eq!(workspace_at(grid, 0, 0, 0), 1, "first cell is workspace 1");
    assert_eq!(workspace_at(grid, 0, 1, 4), 10, "last cell of the first page");
    assert_eq!(workspace_at(grid, 2, 0, 3), 24, "pages continue the numbering");
    assert!(page_of(grid, 1) == 0 && page_of(grid, 10) == 0 && page_of(grid, 11) == 1, "page of a workspace");
    assert!(page_of(grid, 0) == 0 && page_of(grid, -3) == 0, "invalid workspaces map to the first page");
    assert_eq!(step(grid, 0, 1, 1, 0), 2, "step right");
    assert_eq!(step(grid, 0, 1, -1, 0), 5, "step left wraps within the row");
    assert_eq!(step(grid, 0, 5, 1, 0), 1, "step right wraps within the row");
    assert_eq!(step(grid, 0, 3, 0, 1), 8, "step down");
    assert_eq!(step(grid, 0, 3, 0, -1), 8, "step up wraps to the last row");
    assert_eq!(step(grid, 1, 13, 1, 0), 14, "step stays on the given page");
    assert_eq!(step(grid, 1, 3, 1, 0), 14, "the given page wins over the workspace's own page");
    assert_eq!(step(grid, 0, 1, 7, 0), 3, "large steps wrap");
}

#[test]
fn overview_layout() {
    let _instant = InstantGuard::new();
    let compositor = single();
    let mut model = Model::new(compositor.clone());
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);
    let layout = model.layout();
    assert!(layout.cells.len() == 5 && layout.cells[0].workspace == 1 && layout.cells[4].workspace == 5, "five cells in ids");
    assert_eq!(layout.panels.len(), 1, "one panel");
    let panel = layout.panels[0];
    assert!(panel.x >= 0.0 && panel.y >= 0.0 && panel.x + panel.w <= 1366.0 && panel.y + panel.h <= 768.0, "the panel fits the surface");
    assert!((panel.x + panel.w / 2.0 - 683.0).abs() <= 1.0, "the panel is centred");

    model.sync(5000.0, 3000.0);
    assert_eq!(model.layout().scale, cfg::SCALE, "the scale is capped");
    model.sync(400.0, 300.0);
    assert!(model.layout().scale < cfg::SCALE && model.layout().panels[0].w <= 400.0 && model.layout().panels[0].h <= 300.0, "the scale shrinks to fit");
    model.sync(1366.0, 768.0);

    set_active(&compositor, "A", 12);
    model.close();
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);
    assert!(model.layout().cells[0].workspace == 11 && model.selected() == 12, "the page follows the active workspace");

    let two = Rc::new(FakeCompositor::default());
    {
        let mut s = two.state.borrow_mut();
        s.monitors = vec![monitor(1, "A", 0.0, 0.0, 1920.0, 1080.0), monitor(2, "B", 1920.0, 0.0, 1920.0, 1080.0)];
        s.focused_monitor = "A".into();
    }
    set_active(&two, "A", 1);
    set_active(&two, "B", 11);
    let mut global = Model::new(two);
    global.open("A", 1920.0, 1080.0);
    global.key(&plain(KeyKind::Tab));
    assert!(global.global_mode(), "tab switches to the global view");
    global.sync(1920.0, 1080.0);
    assert!(global.layout().panels.len() == 2 && global.layout().cells.len() == 10, "one block per monitor");
    assert_eq!(global.layout().cells[5].workspace, 11, "each block shows its own page");
    global.key(&plain(KeyKind::Tab));
    assert!(!global.global_mode(), "tab switches back");
}

#[test]
fn overview_tiles() {
    let _instant = InstantGuard::new();
    let compositor = single();
    {
        let mut s = compositor.state.borrow_mut();
        s.clients = vec![client("0x1", 1, 0.0, 0.0, 683.0, 374.0), client("0x2", 2, -500.0, 9000.0, 100.0, 100.0), client("0x3", 99, 0.0, 0.0, 10.0, 10.0)];
        s.clients[0].floating = true;
    }
    let mut model = Model::new(compositor);
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);
    assert_eq!(model.tiles().len(), 2, "windows on other pages get no tile");
    let cell = model.layout().cells.iter().find(|c| c.workspace == 2).map(|c| c.rect).expect("cell for workspace two");
    for tile in model.tiles() {
        let r = model.tile_rect(tile);
        let c = if tile.workspace == 1 { model.layout().cells[0].rect } else { cell };
        assert!(r.x >= c.x - 1e-3 && r.y >= c.y - 1e-3 && r.x + r.w <= c.x + c.w + 1e-3 && r.y + r.h <= c.y + c.h + 1e-3, "tiles stay inside their cell");
    }
    assert_eq!(model.tiles()[0].address, "0x2", "floating windows draw on top");
}

#[test]
fn overview_input() {
    let _instant = InstantGuard::new();
    let compositor = single();
    compositor.state.borrow_mut().clients = vec![client("0x1", 1, 100.0, 100.0, 400.0, 300.0)];
    let mut model = Model::new(compositor.clone());
    let mut close_requests = 0;
    assert!(!model.key(&plain(KeyKind::Right)), "a closed overview ignores keys");
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);

    assert!(model.key(&plain(KeyKind::Right)) && compositor.last() == "focus 2" && model.selected() == 2, "right selects the next workspace");
    assert!(model.key(&plain(KeyKind::Left)) && model.key(&plain(KeyKind::Left)) && compositor.last() == "focus 5", "left wraps within the page");
    assert!(model.key(&plain(KeyKind::Down)) && compositor.last() == "focus 5", "down stays on the only row");
    assert!(model.key(&key(KeyKind::Right, "", '\0', true, false, false)) && compositor.last() == "swap 1", "shift moves the workspace contents");
    assert!(model.key(&key(KeyKind::Right, "", '\0', false, true, false)) && compositor.last() == "move-in 2", "alt moves the workspace into the monitor");
    assert!(model.key(&key(KeyKind::Text, "3", '3', false, false, false)) && compositor.last() == "focus 3", "digits pick a workspace on the page");
    assert!(model.key(&key(KeyKind::Text, "0", '0', false, false, false)) && compositor.last() == "focus 3", "zero is past the five and does nothing");
    assert!(model.key(&key(KeyKind::Text, "#", '3', true, false, false)) && compositor.last() == "swap 3", "shift digit uses the base symbol");
    assert!(model.key(&key(KeyKind::Text, "d", 'd', false, false, false)) && compositor.last() == "close-windows workspace 3", "d closes the workspace");
    assert!(model.key(&key(KeyKind::Text, "D", 'd', true, false, false)) && compositor.last() == "close-windows monitor 1", "shift d closes the monitor");
    assert!(model.key(&key(KeyKind::Text, "d", 'd', false, false, true)) && compositor.last() == "close-windows all -1", "ctrl d closes everything");
    assert!(!model.key(&key(KeyKind::Text, "x", 'x', false, false, false)), "other text is ignored");
    assert!(model.key(&plain(KeyKind::Escape)), "escape is taken");
    close_requests += usize::from(model.take_close_request());
    assert_eq!(close_requests, 1, "escape asks to close");

    model.sync(1366.0, 768.0);
    let fourth = model.layout().cells[3].rect;
    model.press(f64::from(fourth.x) + 2.0, f64::from(fourth.y) + 2.0, false, false);
    assert_eq!(compositor.last(), "focus 4", "clicking a cell focuses it");
    model.press(f64::from(fourth.x) + 2.0, f64::from(fourth.y) + 2.0, true, false);
    assert_eq!(compositor.last(), "swap 4", "shift click swaps the workspace");
    model.press(f64::from(fourth.x) + 2.0, f64::from(fourth.y) + 2.0, false, true);
    assert_eq!(compositor.last(), "move-in 4", "alt click moves the workspace in");
    model.press(1.0, 1.0, false, false);
    close_requests += usize::from(model.take_close_request());
    assert_eq!(close_requests, 2, "clicking outside closes");

    let tile = model.tile_rect(&model.tiles()[0]);
    let (tx, ty) = (f64::from(tile.x), f64::from(tile.y));
    model.press(tx + 2.0, ty + 2.0, false, false);
    assert!(model.dragging() && model.clickable(tx + 2.0, ty + 2.0), "pressing a tile starts a drag");
    let target = model.layout().cells[4].rect;
    model.move_to(f64::from(target.x) + 3.0, f64::from(target.y) + 3.0);
    assert_eq!(model.drag_target(), 5, "the cell under the pointer is the drop target");
    model.sync(1366.0, 768.0);
    let last = model.tiles().last().expect("a tile").clone();
    assert_eq!(model.tile_rect(&last).x, (f64::from(target.x) + 3.0 - 2.0) as f32, "the dragged tile follows the pointer");
    model.release();
    assert!(!model.dragging() && compositor.last() == "move 0x1 5", "dropping on another workspace moves the window");

    model.press(tx + 2.0, ty + 2.0, false, false);
    model.move_to(tx + 4.0, ty + 4.0);
    model.release();
    assert_eq!(compositor.last(), "focus 1", "dropping on the same workspace focuses it");
}

#[test]
fn overview_stages() {
    let compositor = single();
    compositor.state.borrow_mut().clients = vec![client("0x1", 1, 100.0, 100.0, 400.0, 300.0)];
    let mut model = Model::new(compositor.clone());
    let mut closed = 0;
    let at = |s: u64| Instant::now() + Duration::from_secs(s);
    model.open("A", 1366.0, 768.0);
    assert!(model.slide_y() == 768.0 && model.scroll() == 0.0 && model.animating(), "the closed scroll starts below the screen");
    model.sync(1366.0, 768.0);
    assert!(model.indicator().is_none() && !model.clickable(683.0, 384.0), "nothing reacts while the scroll is closed");
    let calls = compositor.calls.borrow().len();
    model.press(683.0, 384.0, false, false);
    assert!(compositor.calls.borrow().len() == calls && !model.dragging(), "pointer presses are ignored until the scroll is open");

    model.tick(at(1));
    assert!(model.slide_y() == 0.0 && model.scroll() == 0.0 && !model.fully_open(), "the scroll rises first and is still closed");
    model.tick(at(2));
    assert!(model.scroll() == 1.0 && model.grid_alpha() == 0.0 && model.fully_open() && model.animating(), "then it unrolls and the grid starts to fade in");
    model.tick(at(3));
    assert!(model.grid_alpha() == 1.0 && !model.animating(), "the grid settles last");
    model.sync(1366.0, 768.0);
    assert!(model.indicator().is_some(), "the indicator shows once settled");

    model.close();
    assert!(model.is_open() && !model.fully_open(), "closing keeps the overview until the drop ends");
    assert!(!model.key(&plain(KeyKind::Right)), "keys are ignored while closing");
    model.tick(at(4));
    assert!(model.grid_alpha() == 0.0 && model.scroll() == 1.0, "the grid fades out before the scroll moves");
    model.tick(at(5));
    closed += usize::from(model.take_closed());
    assert!(model.scroll() == 0.0 && model.slide_y() == 0.0 && model.is_open() && closed == 0, "the scroll rolls up before it drops");
    model.tick(at(6));
    closed += usize::from(model.take_closed());
    assert!(!model.is_open() && closed == 1, "the overview closes after the drop");

    model.open("A", 1366.0, 768.0);
    model.close();
    model.tick(at(1));
    model.tick(at(2));
    closed += usize::from(model.take_closed());
    assert!(!model.is_open() && closed == 2 && model.scroll() == 0.0, "closing during the rise never unrolls the scroll");

    let _instant = InstantGuard::new();
    let mut snap = Model::new(compositor);
    snap.open("A", 1366.0, 768.0);
    assert!(snap.fully_open() && !snap.animating(), "instant mode opens straight to the final scroll");
    snap.close();
    assert!(!snap.is_open() && snap.take_closed(), "and closes straight away");
}

#[test]
fn overview_view() {
    let _instant = InstantGuard::new();
    let compositor = single();
    compositor.state.borrow_mut().clients = vec![client("0x1", 1, 0.0, 0.0, 683.0, 374.0)];
    let mut model = Model::new(compositor);
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);
    let metrics = ScrollMetrics::default();
    let mut frame = RecordingCanvas::default();
    paint_scroll(&mut frame, &scroll_geometry(model.layout().panels[0], 1.0, &metrics), &metrics);
    let scroll_ops = frame.count(OpKind::Rounded);

    let mut canvas = RecordingCanvas::default();
    view::paint(&mut canvas, &mut model, &mut no_art());
    assert_eq!(canvas.count(OpKind::Rounded), scroll_ops + 10 + 2 + 2 + 2, "the scroll, cells, one tile, the banner and the indicator with its shadow");
    let frames: Vec<_> = canvas.ops.iter().filter(|op| op.kind == OpKind::Rounded).collect();
    assert!(frames.len() >= 2 && frames[frames.len() - 1].color.a == 0.0 && frames[frames.len() - 2].color.a == 0.0, "the indicator and its shadow leave the tiles visible");
    assert!(canvas.count(OpKind::Text) == 6 && canvas.count(OpKind::Image) <= 1, "a number per cell and the banner");
    assert_eq!(model.banner(), (1, 1), "the banner counts the active workspace's windows");
    let bottom = model.layout().panels[0].y + model.layout().panels[0].h;
    assert!(canvas.ops.iter().any(|op| op.kind == OpKind::Rounded && op.rect.y == bottom + cfg::BANNER_GAP), "the banner sits under the grid");

    let mut live = RecordingCanvas::default();
    let mut art_calls = 0;
    view::paint(&mut live, &mut model, &mut |_: &mut RecordingCanvas, _: &Tile, _: Rect, _: f32| {
        art_calls += 1;
        true
    });
    assert!(art_calls == 1 && live.count(OpKind::Rounded) == scroll_ops + 10 + 2 + 2, "live art replaces the placeholder tile");
}

#[test]
fn overview_staged_view() {
    let compositor = single();
    compositor.state.borrow_mut().clients = vec![client("0x1", 1, 0.0, 0.0, 683.0, 374.0)];
    let mut model = Model::new(compositor);
    let now = Instant::now();
    let after = |ms: f32| now + Duration::from_secs_f32(ms / 1000.0);
    let timings = ScrollTimings::default();
    model.open("A", 1366.0, 768.0);
    model.sync(1366.0, 768.0);
    let mut rising = RecordingCanvas::default();
    view::paint(&mut rising, &mut model, &mut no_art());
    assert!(rising.count(OpKind::Rounded) > 0 && rising.count(OpKind::Text) == 0, "the closed scroll rises with no grid");

    model.tick(after(1000.0));
    model.tick(after(timings.scroll_ms / 2.0));
    model.sync(1366.0, 768.0);
    assert!(model.scroll() > 0.0 && model.scroll() < 1.0, "the scroll is half unrolled");
    let mut unrolling = RecordingCanvas::default();
    view::paint(&mut unrolling, &mut model, &mut no_art());
    assert!(unrolling.count(OpKind::Text) == 0 && unrolling.count(OpKind::BeginGroup) == 0, "the scroll unrolls empty");

    model.tick(after(2000.0));
    model.sync(1366.0, 768.0);
    let mut waiting = RecordingCanvas::default();
    view::paint(&mut waiting, &mut model, &mut no_art());
    assert!(model.scroll() == 1.0 && waiting.count(OpKind::Text) == 0, "the grid waits for the open scroll");

    model.tick(after(3000.0));
    model.sync(1366.0, 768.0);
    let mut open = RecordingCanvas::default();
    view::paint(&mut open, &mut model, &mut no_art());
    assert!(open.count(OpKind::Text) == 6 && open.count(OpKind::BeginGroup) == 0, "the grid and banner appear once the scroll is open");

    model.close();
    let closing = Instant::now();
    model.tick(closing + Duration::from_secs_f32(timings.grid_out_ms / 2000.0));
    model.sync(1366.0, 768.0);
    let mut fading = RecordingCanvas::default();
    view::paint(&mut fading, &mut model, &mut no_art());
    assert!(model.grid_alpha() > 0.0 && model.grid_alpha() < 1.0 && model.scroll() == 1.0 && fading.count(OpKind::Text) == 6, "the grid and banner fade while the scroll stays open");

    model.tick(closing + Duration::from_secs(4));
    model.sync(1366.0, 768.0);
    let mut gone = RecordingCanvas::default();
    view::paint(&mut gone, &mut model, &mut no_art());
    assert!(gone.count(OpKind::Text) == 0 && model.scroll() == 1.0, "the grid is gone before the scroll rolls up");
}
