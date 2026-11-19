use std::cell::Cell;
use std::rc::Rc;
use std::time::{Duration, Instant};

use super::panel::{ContentBase, Panel, PanelContent, PanelPaint, PanelRegion};
use super::panel_set::{PanelAnchor, PanelId, PanelSet};
use crate::base::animation::set_instant;
use crate::base::input::{Button, KeyEvent, KeyKind};
use crate::config::panel_config;
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;
use crate::render::recording_canvas::{OpKind, RecordingCanvas};

struct InstantGuard;

impl InstantGuard {
    fn on() -> InstantGuard {
        set_instant(true);
        InstantGuard
    }
}

impl Drop for InstantGuard {
    fn drop(&mut self) {
        set_instant(false);
    }
}

#[derive(Default)]
struct Counts {
    activated: Cell<i32>,
    last_id: Cell<i32>,
    dragged: Cell<i32>,
    dropped: Cell<i32>,
    opened: Cell<i32>,
    closed: Cell<i32>,
    dismissed: Cell<i32>,
    hover_id: Cell<i32>,
    body_hover: Cell<i32>,
    hover_a: Cell<i32>,
}

struct FakeContent {
    base: ContentBase,
    height: Rc<Cell<f32>>,
    dialog: Rc<Cell<f32>>,
    header: Rc<Cell<bool>>,
    popup: Rc<Cell<f32>>,
    side: Rc<Cell<PanelAnchor>>,
    counts: Rc<Counts>,
}

#[derive(Clone)]
struct Knobs {
    height: Rc<Cell<f32>>,
    dialog: Rc<Cell<f32>>,
    header: Rc<Cell<bool>>,
    popup: Rc<Cell<f32>>,
    side: Rc<Cell<PanelAnchor>>,
    counts: Rc<Counts>,
}

fn fake() -> (Box<FakeContent>, Knobs) {
    let knobs = Knobs {
        height: Rc::new(Cell::new(100.0)),
        dialog: Rc::new(Cell::new(0.0)),
        header: Rc::new(Cell::new(true)),
        popup: Rc::new(Cell::new(0.0)),
        side: Rc::new(Cell::new(PanelAnchor::Right)),
        counts: Rc::new(Counts { hover_id: Cell::new(-1), body_hover: Cell::new(-2), hover_a: Cell::new(-1), ..Counts::default() }),
    };
    let k = knobs.clone();
    let content = FakeContent {
        base: ContentBase::default(),
        height: k.height,
        dialog: k.dialog,
        header: k.header,
        popup: k.popup,
        side: k.side,
        counts: k.counts,
    };
    (Box::new(content), knobs)
}

impl PanelContent for FakeContent {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Fake"
    }
    fn anchor(&self) -> PanelAnchor {
        self.side.get()
    }
    fn has_header(&self) -> bool {
        self.header.get()
    }
    fn popup_anchor(&self) -> Rect {
        Rect { x: 10.0, y: 20.0, w: 40.0, h: 40.0 }
    }
    fn popup_width(&self) -> f32 {
        if self.popup.get() > 0.0 { 220.0 } else { 0.0 }
    }
    fn popup_height(&self) -> f32 {
        self.popup.get()
    }
    fn hover(&mut self, id: i32, _a: i32) -> bool {
        let changed = id != self.counts.body_hover.get();
        self.counts.body_hover.set(id);
        changed
    }
    fn popup_hover(&mut self, id: i32, a: i32) -> bool {
        let c = &self.counts;
        let changed = id != c.hover_id.get() || a != c.hover_a.get();
        c.hover_id.set(id);
        c.hover_a.set(a);
        changed
    }
    fn paint_popup(&mut self, _canvas: &mut dyn Canvas, r: Rect, paint: &mut PanelPaint) {
        paint.push(PanelRegion { rect: Rect { x: r.x + 4.0, y: r.y + 4.0, w: 100.0, h: 28.0 }, id: 9, a: 1, ..PanelRegion::default() });
    }
    fn opened(&mut self) {
        self.counts.opened.set(self.counts.opened.get() + 1);
    }
    fn closed(&mut self) {
        self.counts.closed.set(self.counts.closed.get() + 1);
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        self.height.get()
    }
    fn paint(&mut self, _canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint) {
        paint.push(PanelRegion { rect: Rect { x: view.x, y: view.y - scroll, w: 50.0, h: 20.0 }, id: 7, a: 3, b: 4, drag: false });
        paint.push(PanelRegion { rect: Rect { x: view.x, y: view.y - scroll + 30.0, w: 100.0, h: 20.0 }, id: 8, drag: true, ..PanelRegion::default() });
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        self.counts.activated.set(self.counts.activated.get() + 1);
        self.counts.last_id.set(region.id);
        true
    }
    fn drag(&mut self, _region: &PanelRegion, _x: f64, _y: f64) -> bool {
        self.counts.dragged.set(self.counts.dragged.get() + 1);
        true
    }
    fn drop_region(&mut self, _region: &PanelRegion) {
        self.counts.dropped.set(self.counts.dropped.get() + 1);
    }
    fn dialog_height(&mut self) -> f32 {
        self.dialog.get()
    }
    fn dismiss_dialog(&mut self) -> bool {
        if self.popup.get() > 0.0 {
            self.popup.set(0.0);
            return true;
        }
        if self.dialog.get() <= 0.0 {
            return false;
        }
        self.dialog.set(0.0);
        self.counts.dismissed.set(self.counts.dismissed.get() + 1);
        true
    }
}

struct Harness {
    panel: Panel,
    k: Knobs,
    canvas: RecordingCanvas,
    closed: Rc<Cell<i32>>,
}

fn harness() -> Harness {
    let (content, k) = fake();
    let mut panel = Panel::new(content);
    let closed = Rc::new(Cell::new(0));
    let c = closed.clone();
    panel.on_closed = Some(Box::new(move || c.set(c.get() + 1)));
    Harness { panel, k, canvas: RecordingCanvas::default(), closed }
}

impl Harness {
    fn paint(&mut self) -> Rect {
        self.panel.paint(&mut self.canvas, 1000.0, 50.0)
    }
}

fn escape() -> KeyEvent {
    KeyEvent { kind: KeyKind::Escape, ..KeyEvent::default() }
}

#[test]
fn panel_framework() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    assert!(!h.panel.is_open() && h.paint().w == 0.0, "a closed panel paints nothing");
    h.panel.open();
    assert!(h.panel.is_open() && h.k.counts.opened.get() == 1, "open calls the content");
    let card = h.paint();
    let rod = Panel::inset().y;
    assert!(card.w == 400.0 && card.x == 580.0 && card.y == 50.0 + rod, "the card sits at the right margin under the upper roller");
    let chrome = 20.0 + 32.0 + 4.0 + 1.0 + 8.0 + 20.0;
    assert!(card.h == chrome + 100.0, "the card height is chrome plus content");
    assert!(
        h.panel.contains((card.x + 20.0) as f64, (card.y + 10.0) as f64) && !h.panel.contains(10.0, 10.0) && !h.panel.contains((card.x + 20.0) as f64, 50.0),
        "contains covers the card only"
    );

    let bx = (card.x + 20.0 + 5.0) as f64;
    let by = (card.y + 20.0 + 32.0 + 4.0 + 1.0 + 8.0 + 5.0) as f64;
    assert!(h.panel.clickable(bx, by) && !h.panel.clickable((card.x + 1.0) as f64, (card.y + 1.0) as f64), "clickable follows regions");
    assert!(h.panel.press(bx, by, Button::Left) && h.k.counts.activated.get() == 1 && h.k.counts.last_id.get() == 7, "press activates the region under the pointer");

    assert!(h.panel.hover(bx, by) && h.k.counts.body_hover.get() == 7, "hovering a card region reports its id");
    assert!(!h.panel.hover(bx + 1.0, by + 1.0), "hovering the same region changes nothing");
    assert!(h.panel.hover((card.x + 1.0) as f64, (card.y + 1.0) as f64) && h.k.counts.body_hover.get() == -1, "hovering empty card space clears the hover");
    let dx = (card.x + 20.0 + 5.0) as f64;
    let dy = by + 30.0;
    h.panel.press(dx, dy, Button::Left);
    assert!(h.panel.dragging() && h.k.counts.last_id.get() == 8, "a drag region starts a drag");
    h.panel.move_to(dx + 10.0, dy);
    assert!(h.k.counts.dragged.get() == 1, "move reaches the content while dragging");
    h.panel.release();
    assert!(!h.panel.dragging() && h.k.counts.dropped.get() == 1, "release drops");
    assert!(!h.panel.move_to(0.0, 0.0) && !h.panel.release(), "no drag, no move");

    assert!(!h.panel.press((card.x - 200.0) as f64, (card.y + 2000.0) as f64, Button::Middle), "middle clicks are ignored");
    h.k.dialog.set(40.0);
    h.paint();
    assert!(h.panel.contains((card.x + 5.0) as f64, (card.y + card.h + rod + 8.0 + 5.0) as f64), "contains covers the dialog card");
    h.panel.press(5.0, 5.0, Button::Left);
    assert!(h.k.counts.dismissed.get() == 1 && h.panel.is_open(), "an outside press dismisses the dialog first");
    h.paint();
    h.panel.press(5.0, 5.0, Button::Left);
    assert!(!h.panel.is_open() && h.closed.get() == 1 && h.k.counts.closed.get() == 1, "an outside press closes the panel");

    h.panel.open();
    h.paint();
    assert!(h.panel.key(&escape()) && !h.panel.is_open(), "escape closes");

    h.panel.open();
    let card = h.paint();
    h.panel.press((card.x + card.w - 20.0 - 11.0) as f64, (card.y + 20.0 + 16.0) as f64, Button::Left);
    assert!(!h.panel.is_open(), "the close button closes");
}

#[test]
fn panel_headerless() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    h.k.header.set(false);
    h.panel.open();
    let card = h.paint();
    assert!(card.h == 20.0 + 100.0 + 20.0, "a headerless card is the content between two paddings");
    let bx = (card.x + 20.0 + 5.0) as f64;
    let by = (card.y + 20.0 + 5.0) as f64;
    assert!(h.panel.press(bx, by, Button::Left) && h.k.counts.last_id.get() == 7, "headerless content starts at the top padding");
    assert!(h.panel.key(&escape()) && !h.panel.is_open(), "escape closes a headerless card");
}

#[test]
fn panel_popup() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    h.panel.open();
    assert!(!h.panel.popup_wanted(), "no popup without content for it");
    let card = h.paint();
    h.k.popup.set(90.0);
    assert!(h.panel.popup_wanted() && h.panel.popup_width() == 220.0 && h.panel.popup_height() == 90.0, "the popup follows the content");
    let anchor = h.panel.popup_anchor();
    assert!(anchor.x == card.x + 10.0 && anchor.y == card.y + 20.0 && anchor.w == 40.0, "the anchor is moved to surface coordinates");
    h.panel.paint_popup(&mut h.canvas);
    assert!(h.panel.popup_clickable(10.0, 10.0) && !h.panel.popup_clickable(150.0, 10.0), "popup regions are clickable");
    assert!(h.panel.press_popup(10.0, 10.0, Button::Left) && h.k.counts.activated.get() == 1 && h.k.counts.last_id.get() == 9, "a popup press activates its region");
    assert!(h.panel.hover_popup(10.0, 10.0) && h.k.counts.hover_id.get() == 9 && h.k.counts.hover_a.get() == 1, "hovering a popup region reports its id");
    assert!(!h.panel.hover_popup(11.0, 11.0), "hovering the same region changes nothing");
    assert!(h.panel.hover_popup(200.0, 80.0) && h.k.counts.hover_id.get() == -1, "leaving the regions clears the hover");
    let before = h.k.counts.activated.get();
    assert!(h.panel.press_popup(200.0, 80.0, Button::Left) && h.k.counts.activated.get() == before, "a popup press outside any region does nothing");
    h.panel.close();
    assert!(!h.panel.popup_wanted(), "a closing panel has no popup");
}

#[test]
fn popup_done_dismisses() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    h.panel.open();
    h.paint();
    h.k.popup.set(90.0);
    assert!(h.panel.popup_wanted());
    h.panel.dismiss_popup();
    assert!(h.panel.is_open() && !h.panel.popup_wanted(), "a dismissed popup leaves the panel open");
    h.k.popup.set(90.0);
    assert!(h.panel.popup_wanted(), "the popup comes back when the content asks again");
}

#[test]
fn panel_set_lookup() {
    let mut set = PanelSet::new();
    assert!(set.active_id().and_then(|id| set.find(id)).is_none(), "an empty set has no active panel to find");
    set.add(PanelId::Time, fake().0);
    assert!(set.find(PanelId::Time).is_some() && set.find(PanelId::Tray).is_none(), "find returns only the panels that were added");
}

const _: () = {
    assert!(panel_config::CONFIRM_DIALOG_HEIGHT == 114.0, "the confirm dialog is 114 high");
    assert!(panel_config::FIELD_DIALOG_HEIGHT == 122.0, "the password dialog is 122 high");
    assert!(panel_config::PAIRING_DIALOG_HEIGHT == 160.0, "the pairing dialog is 160 high");
};

#[test]
fn panel_scroll() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    h.k.height.set(900.0);
    h.panel.open();
    let card = h.paint();
    let (x, y) = ((card.x + 10.0) as f64, (card.y + 100.0) as f64);
    assert!(card.h == 520.0, "tall content stops at the maximum height");
    assert!(h.panel.wheel(x, y, 80.0), "wheel scrolls a tall panel");
    assert!(h.panel.wheel(x, y, 5000.0), "wheel scrolls again");
    h.paint();
    assert!(!h.panel.wheel(x, y, 5000.0), "scroll clamps at the end");
    assert!(h.panel.wheel(x, y, -9000.0), "scroll returns to the top");
    h.panel.close();
    h.panel.open();
    assert!(!h.panel.wheel(0.0, 0.0, -1.0), "opening resets scroll");
}

#[test]
fn panel_animation() {
    let at = |s: u64| Instant::now() + Duration::from_secs(s);
    let mut h = harness();
    h.panel.open();
    let card = h.paint();
    assert!(h.panel.animating(), "the scroll animates in");
    assert!(!h.panel.press((card.x + 25.0) as f64, (card.y + 80.0) as f64, Button::Left) && h.k.counts.activated.get() == 0, "the card ignores presses until the scroll is open");
    assert!(h.canvas.count(OpKind::Text) == 0, "the content waits for the open scroll");
    h.panel.tick(at(1));
    h.panel.tick(at(2));
    h.panel.tick(at(3));
    assert!(!h.panel.animating(), "the stages finish");
    h.canvas.ops.clear();
    h.paint();
    assert!(h.canvas.count(OpKind::Text) > 0, "the content shows once the scroll is open");
    let y = (card.y + 20.0 + 32.0 + 4.0 + 1.0 + 8.0 + 5.0) as f64;
    assert!(h.panel.press((card.x + 25.0) as f64, y, Button::Left) && h.k.counts.activated.get() == 1, "and the card takes presses");
    h.panel.close();
    assert!(h.panel.is_open() && h.panel.animating() && h.panel.closing(), "closing keeps the card until the scroll has slid out");
    h.panel.tick(at(4));
    assert!(h.panel.is_open() && h.closed.get() == 0, "the content fades before the scroll rolls up");
    h.panel.tick(at(5));
    h.panel.tick(at(6));
    assert!(!h.panel.is_open() && h.closed.get() == 1, "the panel closes after the slide");
}

fn beyond(canvas: &RecordingCanvas, outside: impl Fn(&Rect) -> bool) -> bool {
    let mut any = false;
    for op in canvas.ops.iter().filter(|o| o.kind == OpKind::Rounded) {
        any = true;
        if !outside(&op.rect) {
            return false;
        }
    }
    any
}

#[test]
fn panel_entry() {
    const WIDTH: f32 = 1000.0;
    type Case = (PanelAnchor, fn(&Rect) -> bool, &'static str);
    let cases: [Case; 3] = [
        (PanelAnchor::Left, |b| b.x + b.w <= 0.0, "a left panel's scroll starts off the left edge"),
        (PanelAnchor::Right, |b| b.x >= WIDTH, "a right panel's scroll starts off the right edge"),
        (PanelAnchor::Center, |b| b.y + b.h <= 0.0, "a centre panel's scroll starts above the top edge"),
    ];
    for (side, outside, name) in cases {
        let mut h = harness();
        h.k.side.set(side);
        h.panel.open();
        h.panel.paint(&mut h.canvas, WIDTH, 50.0);
        assert!(beyond(&h.canvas, outside), "{name}");
    }
}

#[test]
fn panel_resting_scroll() {
    let _instant = InstantGuard::on();
    let mut h = harness();
    h.panel.open();
    let card = h.paint();
    let extent = h.panel.extent();
    let inset = Panel::inset();
    assert!(
        extent.x == card.x - inset.x && extent.y == 50.0 && extent.w == card.w + 2.0 * inset.x && extent.h == card.h + 2.0 * inset.y,
        "the extent wraps the card and both rollers"
    );
    assert!(h.panel.card().y == 50.0 + inset.y, "the card box is where the paper rests");
}
