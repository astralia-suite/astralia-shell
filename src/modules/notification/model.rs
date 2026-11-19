//! The notification cards: entries, their animations and layout.

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;
use std::time::{Duration, Instant};

use crate::base::animation::{self, AnimationManager, Easing, Value, value, writer};
use crate::config::notification_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::service::notification::{NOTIFICATION_URGENCY_NORMAL, Notification};

fn owner(id: u32, slot: u64) -> u64 {
    (u64::from(id) << 2) | slot
}

const OPACITY: u64 = 0;
const SLIDE: u64 = 1;
const EXIT: u64 = 2;
const PROGRESS: u64 = 3;

pub struct Entry {
    pub id: u32,
    pub app: String,
    pub summary: String,
    pub body: String,
    pub urgency: u8,
    opacity: Value,
    slide_offset: Value,
    progress: Value,
    pub exiting: bool,
    /// False where animations are off: no countdown is drawn.
    pub timed: bool,
    pub clickable: bool,
}

impl Entry {
    fn new(id: u32) -> Entry {
        Entry {
            id,
            app: String::new(),
            summary: String::new(),
            body: String::new(),
            urgency: NOTIFICATION_URGENCY_NORMAL,
            opacity: value(0.0),
            slide_offset: value(0.0),
            progress: value(1.0),
            exiting: false,
            timed: false,
            clickable: false,
        }
    }

    fn load(&mut self, n: &Notification) {
        self.app = n.app.clone();
        self.summary = n.summary.clone();
        self.body = n.body.clone();
        self.urgency = n.urgency;
        self.progress.set(1.0);
        self.timed = !animation::instant();
        self.clickable = !n.click_command.is_empty();
    }

    fn same_content(&self, n: &Notification) -> bool {
        self.app == n.app && self.summary == n.summary && self.body == n.body && self.clickable == !n.click_command.is_empty()
    }

    pub fn opacity(&self) -> f32 {
        self.opacity.get()
    }

    pub fn slide_offset(&self) -> f32 {
        self.slide_offset.get()
    }

    pub fn progress(&self) -> f32 {
        self.progress.get()
    }
}

/// The cards, newest first, following the notification service's list.
#[derive(Default)]
pub struct Model {
    entries: Vec<Entry>,
    animations: AnimationManager,
    /// Ids whose exit finished; dropped on the next tick.
    removed: Rc<RefCell<Vec<u32>>>,
}

impl Model {
    pub fn entries(&self) -> &[Entry] {
        &self.entries
    }

    pub fn entry(&self, id: u32) -> Option<&Entry> {
        self.entries.iter().find(|e| e.id == id)
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.drop_removed();
    }

    fn drop_removed(&mut self) {
        let removed = std::mem::take(&mut *self.removed.borrow_mut());
        self.entries.retain(|e| !removed.contains(&e.id));
    }

    fn start_progress(&mut self, id: u32, timeout: Duration) {
        let Some(entry) = self.entry(id) else { return };
        let progress = entry.progress.clone();
        self.animations.animate(1.0, 0.0, timeout.as_millis() as f32, Easing::Linear, writer(&progress), None, owner(id, PROGRESS));
    }

    fn add(&mut self, n: &Notification) {
        let mut entry = Entry::new(n.id);
        entry.load(n);
        entry.slide_offset.set(cfg::SLIDE_OFFSET);
        let (opacity, slide) = (entry.opacity.clone(), entry.slide_offset.clone());
        self.entries.insert(0, entry);
        self.animations.animate(0.0, 1.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&opacity), None, owner(n.id, OPACITY));
        self.animations.animate(cfg::SLIDE_OFFSET, 0.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&slide), None, owner(n.id, SLIDE));
        self.start_progress(n.id, n.timeout);
    }

    fn start_exit(&mut self, id: u32) {
        let Some(entry) = self.entries.iter_mut().find(|e| e.id == id) else { return };
        if entry.exiting {
            return;
        }
        entry.exiting = true;
        let opacity = entry.opacity.clone();
        self.animations.cancel_owner(owner(id, PROGRESS));
        self.animations.animate(opacity.get(), 0.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&opacity), None, owner(id, OPACITY));
        let removed = self.removed.clone();
        let done = Box::new(move |_: &mut AnimationManager| removed.borrow_mut().push(id));
        self.animations.animate(0.0, 1.0, cfg::ANIM_NORMAL_MS + cfg::ANIM_EXIT_BUFFER_MS, Easing::Linear, |_| {}, Some(done), owner(id, EXIT));
        self.drop_removed();
    }

    /// Adds new notifications, updates changed ones and starts the exit of those that are gone.
    pub fn sync(&mut self, list: &[Notification]) {
        for n in list {
            match self.entries.iter_mut().find(|e| e.id == n.id) {
                None => self.add(n),
                Some(entry) if !entry.same_content(n) => {
                    entry.load(n);
                    self.start_progress(n.id, n.timeout);
                }
                Some(_) => {}
            }
        }
        let gone: Vec<u32> = self.entries.iter().filter(|e| !e.exiting && !list.iter().any(|n| n.id == e.id)).map(|e| e.id).collect();
        for id in gone {
            self.start_exit(id);
        }
    }
}

pub type Boxes = Vec<(u32, Rect)>;

/// What a press on the stack did.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Press {
    Nothing,
    /// A close button hid its card on this monitor.
    Closed,
    /// A clickable card: the service runs its command.
    Open(u32),
}

/// What this monitor's stack adds over the model: cards closed here, and the pointer.
#[derive(Default)]
pub struct ViewState {
    hovered: Option<u32>,
    card_hovered: Option<u32>,
    animations: AnimationManager,
    local_exit: HashMap<u32, Value>,
}

impl ViewState {
    /// Fades a card out on this monitor only; false when it already fades.
    pub fn hide_locally(&mut self, id: u32) -> bool {
        if self.local_exit.contains_key(&id) {
            return false;
        }
        let fade = value(1.0);
        self.local_exit.insert(id, fade.clone());
        self.animations.animate(1.0, 0.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&fade), None, u64::from(id));
        true
    }

    /// True when the hovered close button changed.
    pub fn set_hover(&mut self, id: Option<u32>) -> bool {
        if id == self.hovered {
            return false;
        }
        self.hovered = id;
        true
    }

    /// The pointer at `(x, y)`; true when the close or card hover changed.
    pub fn set_pointer(&mut self, close: &Boxes, cards: &Boxes, x: f64, y: f64) -> bool {
        let over_close = box_at(close, x, y);
        let changed = self.set_hover(over_close);
        let card = if over_close.is_some() { None } else { box_at(cards, x, y) };
        let changed = changed || card != self.card_hovered;
        self.card_hovered = card;
        changed
    }

    pub fn press(&mut self, close: &Boxes, cards: &Boxes, x: f64, y: f64) -> Press {
        if let Some(id) = box_at(close, x, y) {
            return if self.hide_locally(id) { Press::Closed } else { Press::Nothing };
        }
        box_at(cards, x, y).map_or(Press::Nothing, Press::Open)
    }

    pub fn clear_pointer(&mut self) -> bool {
        let changed = self.set_hover(None) || self.card_hovered.is_some();
        self.card_hovered = None;
        changed
    }

    pub fn wants_hand(&self) -> bool {
        self.hovered.is_some() || self.card_hovered.is_some()
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn forget_missing(&mut self, entries: &[Entry]) {
        self.local_exit.retain(|id, _| entries.iter().any(|e| e.id == *id));
    }

    pub fn hovered(&self) -> Option<u32> {
        self.hovered
    }

    pub fn local_opacity(&self, id: u32) -> f32 {
        self.local_exit.get(&id).map_or(1.0, |v| v.get())
    }

    pub fn fading(&self, id: u32) -> bool {
        self.local_exit.contains_key(&id)
    }
}

/// One card's place in the stack; `slide_offset` and `opens` are read from its entry at layout time.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Card {
    pub id: u32,
    pub y: f32,
    pub height: f32,
    pub header: f32,
    pub summary: f32,
    pub body: f32,
    pub slide_offset: f32,
    pub opens: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Layout {
    pub cards: Vec<Card>,
    pub height: f32,
}

pub fn card_height(header: f32, summary: f32, body: f32) -> f32 {
    let mut height = header + cfg::CARD_PAD * 2.0 + cfg::EXTRA_HEIGHT;
    if summary > 0.0 {
        height += cfg::CONTENT_SPACING + summary;
    }
    if body > 0.0 {
        height += cfg::CONTENT_SPACING + body;
    }
    height
}

pub const APP_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::APP_PX, bold: true, max_width: cfg::WRAP_WIDTH as i32, wrap: false };
pub const SUMMARY_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::SUMMARY_PX, bold: true, max_width: cfg::WRAP_WIDTH as i32, wrap: true };
pub const BODY_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::BODY_PX, bold: false, max_width: cfg::WRAP_WIDTH as i32, wrap: true };

/// Stacks the newest cards from the bottom up until `max_height`; the newest always shows.
pub fn layout_notifications(canvas: &mut dyn Canvas, model: &Model, view: &ViewState, max_height: f32) -> Layout {
    let mut layout = Layout::default();
    let mut used = 0.0;
    for entry in model.entries() {
        if view.fading(entry.id) && view.local_opacity(entry.id) <= 0.0 {
            continue;
        }
        let app = if entry.app.is_empty() { cfg::APP_FALLBACK } else { entry.app.as_str() };
        let header = cfg::URGENCY_DOT.max(canvas.measure(app, &APP_STYLE).h);
        let summary = if entry.summary.is_empty() { 0.0 } else { canvas.measure(&entry.summary, &SUMMARY_STYLE).h };
        let body = if entry.body.is_empty() { 0.0 } else { canvas.measure(&entry.body, &BODY_STYLE).h };
        let height = card_height(header, summary, body);
        let next = used + if layout.cards.is_empty() { 0.0 } else { cfg::GAP } + height;
        if !layout.cards.is_empty() && next > max_height {
            break;
        }
        used = next;
        let opens = entry.clickable && !entry.exiting;
        layout.cards.push(Card { id: entry.id, y: 0.0, height, header, summary, body, slide_offset: entry.slide_offset(), opens });
    }
    layout.height = used;
    let mut y = used;
    for card in &mut layout.cards {
        y -= card.height;
        card.y = y;
        y -= cfg::GAP;
    }
    layout
}

fn close_box(card: &Card) -> Rect {
    Rect { x: cfg::CARD_WIDTH - cfg::CLOSE_HIT, y: card.y + card.slide_offset, w: cfg::CLOSE_HIT, h: cfg::CLOSE_HIT }
}

fn card_box(card: &Card) -> Rect {
    Rect { x: 0.0, y: card.y + card.slide_offset, w: cfg::CARD_WIDTH, h: card.height }
}

pub fn close_boxes(layout: &Layout, view: &ViewState) -> Boxes {
    layout.cards.iter().filter(|c| !view.fading(c.id)).map(|c| (c.id, close_box(c))).collect()
}

pub fn card_boxes(layout: &Layout, view: &ViewState) -> Boxes {
    layout.cards.iter().filter(|c| !view.fading(c.id) && c.opens).map(|c| (c.id, card_box(c))).collect()
}

/// Where the surface takes input: a clickable card whole, a plain card only at its close button.
pub fn input_boxes(layout: &Layout, view: &ViewState) -> Boxes {
    layout.cards.iter().filter(|c| !view.fading(c.id)).map(|c| (c.id, if c.opens { card_box(c) } else { close_box(c) })).collect()
}

pub fn box_at(boxes: &Boxes, x: f64, y: f64) -> Option<u32> {
    boxes
        .iter()
        .find(|(_, b)| x >= f64::from(b.x) && x < f64::from(b.x + b.w) && y >= f64::from(b.y) && y < f64::from(b.y + b.h))
        .map(|(id, _)| *id)
}

#[cfg(test)]
pub fn close_at(layout: &Layout, view: &ViewState, x: f64, y: f64) -> Option<u32> {
    box_at(&close_boxes(layout, view), x, y)
}
