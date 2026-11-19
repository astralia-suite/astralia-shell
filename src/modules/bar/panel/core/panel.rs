//! One bar panel: a scroll card with a header, a scrolling body, an optional dialog card below
//! and an optional popup, wrapped around a `PanelContent`.

use std::cell::{Cell, RefCell};
use std::rc::{Rc, Weak};
use std::time::{Duration, Instant};

use super::panel_set::PanelAnchor;
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{Button, KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::scroll::{ScrollAxis, ScrollEntry, ScrollMetrics, paint_scroll, scroll_bounds, scroll_geometry, scroll_slide_offset};
use crate::render::scroll_sequence::{ScrollSequence, ScrollTimings};
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics};
use crate::service::runtime;

const OWNER_REVEAL: u64 = 2;
const OWNER_SEQUENCE: u64 = 10;
const CLOSE_ID: i32 = -1;
const SCROLL_STYLE: ScrollMetrics = ScrollMetrics {
    roller_w: cfg::SCROLL_ROLLER,
    overhang: cfg::SCROLL_OVERHANG,
    cap_h: cfg::SCROLL_CAP,
    rounding: metrics::RADIUS_CARD,
    band: cfg::SCROLL_BAND,
    axis: ScrollAxis::Vertical,
    gap: 6.0,
};
const TITLE_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TITLE_PX, bold: true, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };

/// A shared, replaceable callback slot: whoever holds a clone calls whatever was set last.
pub struct Hook<F: ?Sized>(Rc<RefCell<Option<Rc<F>>>>);

impl<F: ?Sized> Clone for Hook<F> {
    fn clone(&self) -> Self {
        Hook(self.0.clone())
    }
}

impl<F: ?Sized> Default for Hook<F> {
    fn default() -> Self {
        Hook(Rc::new(RefCell::new(None)))
    }
}

impl<F: ?Sized> Hook<F> {
    pub fn set(&self, f: Rc<F>) {
        *self.0.borrow_mut() = Some(f);
    }

    pub fn get(&self) -> Option<Rc<F>> {
        self.0.borrow().clone()
    }
}

impl Hook<dyn Fn()> {
    pub fn call(&self) {
        if let Some(f) = self.get() {
            f();
        }
    }
}

/// A clickable area the content painted, in card (or popup) coordinates.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PanelRegion {
    pub rect: Rect,
    pub id: i32,
    pub a: i32,
    pub b: i32,
    pub drag: bool,
}

/// Collects the regions a content paints.
pub struct PanelPaint<'a> {
    regions: &'a mut Vec<PanelRegion>,
}

impl<'a> PanelPaint<'a> {
    pub fn new(regions: &'a mut Vec<PanelRegion>) -> PanelPaint<'a> {
        PanelPaint { regions }
    }

    pub fn region(&mut self, rect: Rect, id: i32) {
        self.push(PanelRegion { rect, id, ..PanelRegion::default() });
    }

    pub fn push(&mut self, region: PanelRegion) {
        self.regions.push(region);
    }
}

/// State every content carries: its own tweens, the change notifier and the last pressed button.
pub struct ContentBase {
    pub animations: AnimationManager,
    pub pressed: Button,
    changed: Hook<dyn Fn()>,
    alive: Rc<()>,
}

impl Default for ContentBase {
    fn default() -> Self {
        ContentBase { animations: AnimationManager::default(), pressed: Button::Left, changed: Hook::default(), alive: Rc::new(()) }
    }
}

impl ContentBase {
    /// Wraps `f` so it does nothing once this content is dropped.
    pub fn guarded(&self, f: impl Fn() + 'static) -> impl Fn() + 'static {
        let alive = Rc::downgrade(&self.alive);
        move || {
            if alive.upgrade().is_some() {
                f();
            }
        }
    }

    /// A callback that asks the panel to repaint, safe to keep past the content's life.
    pub fn notifier(&self) -> impl Fn() + 'static {
        let changed = self.changed.clone();
        self.guarded(move || changed.call())
    }

    pub fn changed(&self) {
        self.changed.call();
    }
}

pub trait PanelContent {
    fn base(&self) -> &ContentBase;
    fn base_mut(&mut self) -> &mut ContentBase;

    fn title(&self) -> &str;
    fn tint(&self) -> Color {
        palette().accent
    }
    fn anchor(&self) -> PanelAnchor {
        PanelAnchor::Right
    }
    fn has_header(&self) -> bool {
        true
    }
    fn width(&self) -> f32 {
        cfg::WIDTH
    }
    fn max_height(&self) -> f32 {
        cfg::MAX_HEIGHT
    }
    fn opened(&mut self) {}
    fn closed(&mut self) {}
    fn content_height(&mut self, canvas: &mut dyn Canvas) -> f32;
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint);
    fn activate(&mut self, _region: &PanelRegion, _x: f64, _y: f64) -> bool {
        false
    }
    fn drag(&mut self, _region: &PanelRegion, _x: f64, _y: f64) -> bool {
        false
    }
    fn drop_region(&mut self, _region: &PanelRegion) {}
    fn hover(&mut self, _id: i32, _a: i32) -> bool {
        false
    }
    fn key(&mut self, _event: &KeyEvent) -> bool {
        false
    }
    fn wheel(&mut self, _x: f64, _y: f64, _dy: f64) -> bool {
        false
    }
    fn scrollable(&self) -> bool {
        true
    }
    fn wants_text(&self) -> bool {
        false
    }
    fn cursor(&self) -> Rect {
        Rect::default()
    }
    fn paint_header(&mut self, _canvas: &mut dyn Canvas, _r: Rect, _paint: &mut PanelPaint) {}
    fn dialog_height(&mut self) -> f32 {
        0.0
    }
    fn paint_dialog(&mut self, _canvas: &mut dyn Canvas, _r: Rect, _paint: &mut PanelPaint) {}
    fn dismiss_dialog(&mut self) -> bool {
        false
    }
    fn popup_anchor(&self) -> Rect {
        Rect::default()
    }
    fn popup_width(&self) -> f32 {
        0.0
    }
    fn popup_height(&self) -> f32 {
        0.0
    }
    fn paint_popup(&mut self, _canvas: &mut dyn Canvas, _r: Rect, _paint: &mut PanelPaint) {}
    fn popup_hover(&mut self, _id: i32, _a: i32) -> bool {
        false
    }
    /// Zero means no periodic refresh.
    fn refresh_interval(&self) -> Duration {
        Duration::ZERO
    }
    fn refresh(&mut self) {}
}

fn entry_of(anchor: PanelAnchor) -> ScrollEntry {
    match anchor {
        PanelAnchor::Left => ScrollEntry::FromLeft,
        PanelAnchor::Center => ScrollEntry::FromTop,
        PanelAnchor::Right => ScrollEntry::FromRight,
    }
}

/// How far the rollers reach out from the card.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Inset {
    pub x: f32,
    pub y: f32,
}

/// The refresh task's link back to its panel; a generation bump stops the task.
#[derive(Default)]
struct RefreshTimer {
    generation: Cell<u64>,
    due: Cell<bool>,
}

pub struct Panel {
    content: Box<dyn PanelContent>,
    animations: AnimationManager,
    sequence: ScrollSequence,
    timer: Rc<RefreshTimer>,
    close_done: Rc<Cell<bool>>,
    regions: Vec<PanelRegion>,
    popup_regions: Vec<PanelRegion>,
    open: bool,
    reveal: Value,
    target: f32,
    scroll: f32,
    content_h: f32,
    view_h: f32,
    card: Rect,
    dialog: Rect,
    dragging: bool,
    drag_region: PanelRegion,
    pub anchor_override: Option<PanelAnchor>,
    /// Called whenever what the panel shows changed; shared with the content's notifier.
    pub on_changed: Hook<dyn Fn()>,
    /// Called once a close has fully finished.
    pub on_closed: Option<Box<dyn Fn()>>,
}

impl Panel {
    pub fn new(mut content: Box<dyn PanelContent>) -> Panel {
        let on_changed = Hook::default();
        content.base_mut().changed = on_changed.clone();
        Panel {
            content,
            animations: AnimationManager::default(),
            sequence: ScrollSequence::new(OWNER_SEQUENCE, ScrollTimings::default()),
            timer: Rc::new(RefreshTimer::default()),
            close_done: Rc::new(Cell::new(false)),
            regions: Vec::new(),
            popup_regions: Vec::new(),
            open: false,
            reveal: value(-1.0),
            target: -1.0,
            scroll: 0.0,
            content_h: 0.0,
            view_h: 0.0,
            card: Rect::default(),
            dialog: Rect::default(),
            dragging: false,
            drag_region: PanelRegion::default(),
            anchor_override: None,
            on_changed,
            on_closed: None,
        }
    }

    fn changed(&self) {
        self.on_changed.call();
    }

    pub fn open(&mut self) {
        if self.open && !self.closing() {
            return;
        }
        self.open = true;
        self.close_done.set(false);
        self.reveal.set(-1.0);
        self.target = -1.0;
        self.scroll = 0.0;
        self.sequence.open(&mut self.animations);
        self.content.opened();
        self.start_refresh();
        self.changed();
    }

    pub fn close(&mut self) {
        if !self.open || self.closing() {
            return;
        }
        self.dragging = false;
        self.content.closed();
        self.stop_refresh();
        let done = self.close_done.clone();
        self.sequence.close(&mut self.animations, Some(Box::new(move || done.set(true))));
        self.finish_close();
        self.changed();
    }

    pub fn toggle(&mut self) {
        if self.is_open() { self.close() } else { self.open() }
    }

    pub fn set_anchor(&mut self, anchor: Option<PanelAnchor>) {
        self.anchor_override = anchor;
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.content.base_mut().animations.tick(now);
        self.finish_close();
    }

    /// Completes a close once the scroll has slid out.
    fn finish_close(&mut self) {
        if !self.close_done.replace(false) {
            return;
        }
        self.open = false;
        self.reveal.set(-1.0);
        self.target = -1.0;
        if let Some(f) = &self.on_closed {
            f();
        }
    }

    fn stop_refresh(&self) {
        self.timer.generation.set(self.timer.generation.get() + 1);
        self.timer.due.set(false);
    }

    /// Wakes the panel once after `refresh_interval`; the content refreshes at the next paint, which re-arms
    /// the timer with the interval asked then.
    fn start_refresh(&self) {
        self.stop_refresh();
        let interval = self.content.refresh_interval();
        if interval.is_zero() {
            return;
        }
        let generation = self.timer.generation.get();
        let timer: Weak<RefreshTimer> = Rc::downgrade(&self.timer);
        let changed = self.on_changed.clone();
        runtime::spawn(async move {
            async_io::Timer::after(interval).await;
            let Some(timer) = timer.upgrade() else { return };
            if timer.generation.get() != generation {
                return;
            }
            timer.due.set(true);
            changed.call();
        });
    }

    fn region_at(&self, x: f64, y: f64) -> Option<PanelRegion> {
        let (lx, ly) = (x - self.card.x as f64, y - self.card.y as f64);
        self.regions.iter().find(|r| r.rect.contains(lx, ly)).copied()
    }

    pub fn contains(&self, x: f64, y: f64) -> bool {
        self.open && (self.card.contains(x, y) || self.dialog.contains(x, y))
    }

    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.open && self.region_at(x, y).is_some()
    }

    pub fn press(&mut self, x: f64, y: f64, button: Button) -> bool {
        if !self.open || self.closing() || !self.sequence.settled() {
            return false;
        }
        if button != Button::Left && button != Button::Right {
            return false;
        }
        if let Some(hit) = self.region_at(x, y) {
            if hit.id == CLOSE_ID {
                self.close();
                return true;
            }
            if hit.drag {
                self.dragging = true;
                self.drag_region = hit;
            }
            self.content.base_mut().pressed = button;
            if self.content.activate(&hit, x - self.card.x as f64, y - self.card.y as f64) {
                self.changed();
            }
            return true;
        }
        if !self.card.contains(x, y) && !self.dialog.contains(x, y) {
            if self.content.dismiss_dialog() {
                self.changed();
            } else {
                self.close();
            }
        }
        true
    }

    /// Pointer motion; only reaches the content while a drag region is held.
    pub fn move_to(&mut self, x: f64, y: f64) -> bool {
        if !self.dragging {
            return false;
        }
        let region = self.drag_region;
        if self.content.drag(&region, x - self.card.x as f64, y - self.card.y as f64) {
            self.changed();
        }
        true
    }

    pub fn hover(&mut self, x: f64, y: f64) -> bool {
        if !self.open || self.closing() || !self.sequence.settled() || self.dragging {
            return false;
        }
        let (id, a) = self.region_at(x, y).map_or((-1, -1), |r| (r.id, r.a));
        if !self.content.hover(id, a) {
            return false;
        }
        self.changed();
        true
    }

    pub fn release(&mut self) -> bool {
        if !self.dragging {
            return false;
        }
        self.dragging = false;
        let region = self.drag_region;
        self.content.drop_region(&region);
        self.changed();
        true
    }

    pub fn wheel(&mut self, x: f64, y: f64, dy: f64) -> bool {
        if !self.open || self.closing() || !self.sequence.settled() {
            return false;
        }
        if self.content.wheel(x - self.card.x as f64, y - self.card.y as f64, dy) {
            self.changed();
            return true;
        }
        if !self.content.scrollable() {
            return false;
        }
        let next = (self.scroll + dy as f32).clamp(0.0, (self.content_h - self.view_h).max(0.0));
        if next == self.scroll {
            return false;
        }
        self.scroll = next;
        self.changed();
        true
    }

    pub fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.open || self.closing() {
            return false;
        }
        if self.content.key(event) {
            self.changed();
            return true;
        }
        if event.kind == KeyKind::Escape {
            self.close();
            return true;
        }
        false
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active() || self.content.base().animations.has_active()
    }

    pub fn dragging(&self) -> bool {
        self.dragging
    }

    pub fn closing(&self) -> bool {
        self.sequence.closing()
    }

    pub fn dialog(&self) -> Rect {
        self.dialog
    }

    pub fn content(&self) -> &dyn PanelContent {
        self.content.as_ref()
    }

    pub fn content_mut(&mut self) -> &mut dyn PanelContent {
        self.content.as_mut()
    }

    pub fn card(&self) -> Rect {
        self.card
    }

    pub fn popup_wanted(&self) -> bool {
        self.open && !self.closing() && self.sequence.settled() && self.content.popup_height() > 0.0
    }

    pub fn popup_width(&self) -> f32 {
        self.content.popup_width()
    }

    pub fn popup_height(&self) -> f32 {
        self.content.popup_height()
    }

    /// The content's popup anchor in surface coordinates.
    pub fn popup_anchor(&self) -> Rect {
        let a = self.content.popup_anchor();
        Rect { x: self.card.x + a.x, y: self.card.y + a.y, ..a }
    }

    pub fn paint_popup(&mut self, canvas: &mut dyn Canvas) {
        self.popup_regions.clear();
        let r = Rect { x: 0.0, y: 0.0, w: self.content.popup_width(), h: self.content.popup_height() };
        self.content.paint_popup(canvas, r, &mut PanelPaint::new(&mut self.popup_regions));
    }

    pub fn popup_clickable(&self, x: f64, y: f64) -> bool {
        self.popup_regions.iter().any(|r| r.rect.contains(x, y))
    }

    pub fn hover_popup(&mut self, x: f64, y: f64) -> bool {
        let (id, a) = self.popup_regions.iter().find(|r| r.rect.contains(x, y)).map_or((-1, -1), |r| (r.id, r.a));
        if !self.content.popup_hover(id, a) {
            return false;
        }
        self.changed();
        true
    }

    pub fn press_popup(&mut self, x: f64, y: f64, button: Button) -> bool {
        if button != Button::Left && button != Button::Right {
            return false;
        }
        if let Some(hit) = self.popup_regions.iter().find(|r| r.rect.contains(x, y)).copied() {
            self.content.base_mut().pressed = button;
            if self.content.activate(&hit, x, y) {
                self.changed();
            }
        }
        true
    }

    pub fn dismiss_popup(&mut self) {
        if self.content.dismiss_dialog() {
            self.changed();
        }
    }

    pub fn anchor(&self) -> PanelAnchor {
        self.anchor_override.unwrap_or_else(|| self.content.anchor())
    }

    pub fn card_x(&self, surface_width: f32) -> f32 {
        let width = self.content.width();
        match self.anchor() {
            PanelAnchor::Left => cfg::SIDE_MARGIN,
            PanelAnchor::Center => (surface_width - width) / 2.0,
            PanelAnchor::Right => surface_width - width - cfg::SIDE_MARGIN,
        }
    }

    pub fn inset() -> Inset {
        Inset { x: SCROLL_STYLE.overhang, y: SCROLL_STYLE.reach() }
    }

    /// The card, both rollers and the dialog card.
    pub fn extent(&self) -> Rect {
        let room = Panel::inset();
        let (c, d) = (self.card, self.dialog);
        let left = c.x - room.x;
        let top = c.y - room.y;
        let mut right = c.x + c.w + room.x;
        let mut bottom = c.y + c.h + room.y;
        if d.w > 0.0 {
            right = right.max(d.x + d.w);
            bottom = bottom.max(d.y + d.h);
        }
        Rect { x: left, y: top, w: right - left, h: bottom - top }
    }

    pub fn paint(&mut self, canvas: &mut dyn Canvas, surface_width: f32, top: f32) -> Rect {
        let x = self.card_x(surface_width);
        self.paint_at(canvas, x, top, surface_width)
    }

    /// Paints the card at `x` with its upper roller at `top`; returns the card box.
    pub fn paint_at(&mut self, canvas: &mut dyn Canvas, x: f32, top: f32, surface_width: f32) -> Rect {
        if !self.open {
            return Rect::default();
        }
        if self.timer.due.replace(false) && !self.closing() {
            self.content.refresh();
            self.start_refresh();
        }
        let width = self.content.width();
        self.content_h = self.content.content_height(canvas);
        let header = self.content.has_header();
        let chrome = if header {
            cfg::PADDING + cfg::HEADER_HEIGHT + cfg::HEADER_DIVIDER_GAP + 1.0 + cfg::CONTENT_GAP + cfg::PADDING
        } else {
            2.0 * cfg::PADDING
        };
        let height = self.content.max_height().min(chrome + self.content_h);
        if !self.closing() {
            if self.reveal.get() < 0.0 {
                self.reveal.set(height);
                self.target = height;
            } else if (height - self.target).abs() > 0.5 {
                self.target = height;
                let from = self.reveal.get();
                self.animations.animate(from, height, cfg::REVEAL_MS, Easing::EaseOutCubic, writer(&self.reveal), None, OWNER_REVEAL);
            }
        }
        let full = if self.closing() { self.target.max(height) } else { height };
        let reveal = self.reveal.get();
        let visible = if reveal < 0.0 { full } else { reveal.min(full) };

        let rod = SCROLL_STYLE.reach();
        self.card = Rect { x, y: top + rod, w: width, h: full };
        let closed = scroll_bounds(&scroll_geometry(self.card, 0.0, &SCROLL_STYLE));
        let slide = scroll_slide_offset(entry_of(self.anchor()), self.sequence.slide(), closed, surface_width, 0.0);
        let px = x + slide.dx;
        let py = top + rod + slide.dy;
        let paper = Rect { x: px, y: py, w: width, h: visible };
        paint_scroll(canvas, &scroll_geometry(paper, self.sequence.scroll(), &SCROLL_STYLE), &SCROLL_STYLE);

        self.regions.clear();
        self.dialog = Rect::default();
        let grid = self.sequence.grid();
        if grid <= 0.0 {
            return self.card;
        }

        let mut chrome_regions = Vec::new();
        let mut body = Vec::new();
        let mut dialog = Vec::new();

        canvas.set_opacity(grid);
        canvas.begin_group(paper, GroupOptions { clip: visible + 0.5 < full, ..GroupOptions::default() });
        let mut view_top = cfg::PADDING;
        if header {
            let p = palette();
            let title = self.content.title().to_string();
            let size = canvas.measure(&title, &TITLE_STYLE);
            let pad = cfg::HEADER_TAG_PAD;
            let title_y = cfg::PADDING + (cfg::HEADER_HEIGHT - size.h) / 2.0;
            let tag = Rect { x: cfg::PADDING - pad, y: title_y - pad / 2.0, w: size.w + 2.0 * pad, h: size.h + pad + metrics::SHADOW_WIDGET };
            let style = StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() };
            sticker(canvas, tag, metrics::RADIUS_WIDGET, self.content.tint(), &style);
            canvas.text(&title, &TITLE_STYLE, cfg::PADDING, title_y, p.text);
            let close = Rect {
                x: width - cfg::PADDING - cfg::CLOSE_BUTTON,
                y: cfg::PADDING + (cfg::HEADER_HEIGHT - cfg::CLOSE_BUTTON) / 2.0,
                w: cfg::CLOSE_BUTTON,
                h: cfg::CLOSE_BUTTON,
            };
            selectable(canvas, close, metrics::RADIUS_WIDGET, false, false, p.accent_container, 1.0);
            let glyph = canvas.measure(icons::CLOSE, &ICON_STYLE);
            canvas.text(icons::CLOSE, &ICON_STYLE, close.x + (close.w - glyph.w) / 2.0, close.y + (close.h - glyph.h) / 2.0, p.text);
            let mut chrome_paint = PanelPaint::new(&mut chrome_regions);
            chrome_paint.region(close, CLOSE_ID);
            let header_box = Rect { x: cfg::PADDING, y: cfg::PADDING, w: close.x - cfg::ROW_GAP - cfg::PADDING, h: cfg::HEADER_HEIGHT };
            self.content.paint_header(canvas, header_box, &mut chrome_paint);
            let divider_y = cfg::PADDING + cfg::HEADER_HEIGHT + cfg::HEADER_DIVIDER_GAP;
            canvas.rect(Rect { x: cfg::PADDING, y: divider_y, w: width - 2.0 * cfg::PADDING, h: 1.0 }, p.border_outer);
            view_top = divider_y + 1.0 + cfg::CONTENT_GAP;
        }
        self.view_h = (full - cfg::PADDING - view_top).max(0.0);
        self.scroll = self.scroll.clamp(0.0, (self.content_h - self.view_h).max(0.0));
        let inner_w = width - 2.0 * cfg::PADDING;
        canvas.begin_group(Rect { x: cfg::PADDING, y: view_top, w: inner_w, h: self.view_h }, GroupOptions { clip: true, ..GroupOptions::default() });
        let view = Rect { x: 0.0, y: 0.0, w: inner_w, h: self.view_h };
        self.content.paint(canvas, view, self.scroll, &mut PanelPaint::new(&mut body));
        canvas.end_group();
        canvas.end_group();
        canvas.set_opacity(1.0);

        let view_bottom = view_top + self.view_h;
        for region in &mut body {
            let r = &mut region.rect;
            r.x += cfg::PADDING;
            r.y += view_top;
            let bottom = (r.y + r.h).min(view_bottom);
            r.y = r.y.max(view_top);
            r.h = (bottom - r.y).max(0.0);
        }

        let dialog_h = if self.closing() || !self.sequence.settled() { 0.0 } else { self.content.dialog_height() };
        if dialog_h > 0.0 {
            let dialog_y = full + rod + cfg::GAP_BELOW_BAR;
            canvas.begin_group(Rect { x: px, y: py + dialog_y, w: width, h: dialog_h }, GroupOptions::default());
            let r = Rect { x: 0.0, y: 0.0, w: width, h: dialog_h };
            self.content.paint_dialog(canvas, r, &mut PanelPaint::new(&mut dialog));
            canvas.end_group();
            for region in &mut dialog {
                region.rect.y += dialog_y;
            }
            self.dialog = Rect { x: px, y: py + dialog_y, w: width, h: dialog_h };
        }

        self.regions.extend(dialog);
        self.regions.extend(chrome_regions);
        self.regions.extend(body);
        self.card
    }
}
