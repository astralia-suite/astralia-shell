//! Text too wide for its box scrolls back and forth: pause, scroll to the end, pause, snap back.

use crate::base::animation::{AnimationManager, Easing, Value, instant, unique_owner, value, writer};
use crate::render::canvas::{Canvas, GroupOptions, TextStyle};
use crate::render::geometry::Rect;
use crate::render::tokens::Color;

const MARQUEE_PAUSE_MS: f32 = 1200.0;
const MARQUEE_SPEED_PX_PER_SEC: f32 = 35.0;

pub struct MarqueeTextState {
    pub scroll_offset: Value,
    /// Scrolling, not merely overflowing: false in instant mode even when the text is too wide.
    pub marqueeing: bool,
    pub last_text: String,
    pub last_box_w: f32,
    pub owner: u64,
}

impl MarqueeTextState {
    pub fn new() -> MarqueeTextState {
        MarqueeTextState {
            scroll_offset: value(0.0),
            marqueeing: false,
            last_text: String::new(),
            last_box_w: -1.0,
            owner: unique_owner(),
        }
    }
}

impl Default for MarqueeTextState {
    fn default() -> Self {
        Self::new()
    }
}

// Each stage starts the next from its completion, so the chain re-arms itself forever. A stage
// that ends while instant mode is on stops the chain; otherwise every stage would snap at once
// and recurse without end.
fn start_pause_at_start(am: &mut AnimationManager, offset: Value, owner: u64, overflow: f32) {
    if instant() {
        return;
    }
    let next = move |am: &mut AnimationManager| start_scroll(am, offset, owner, overflow);
    am.animate(0.0, 0.0, MARQUEE_PAUSE_MS, Easing::Linear, |_| {}, Some(Box::new(next)), owner);
}

fn start_scroll(am: &mut AnimationManager, offset: Value, owner: u64, overflow: f32) {
    let duration_ms = overflow / MARQUEE_SPEED_PX_PER_SEC * 1000.0;
    let set = writer(&offset);
    let next = move |am: &mut AnimationManager| start_pause_at_end(am, offset, owner, overflow);
    am.animate(0.0, overflow, duration_ms, Easing::Linear, set, Some(Box::new(next)), owner);
}

fn start_pause_at_end(am: &mut AnimationManager, offset: Value, owner: u64, overflow: f32) {
    let next = move |am: &mut AnimationManager| start_snap(am, offset, owner, overflow);
    am.animate(overflow, overflow, MARQUEE_PAUSE_MS, Easing::Linear, |_| {}, Some(Box::new(next)), owner);
}

fn start_snap(am: &mut AnimationManager, offset: Value, owner: u64, overflow: f32) {
    let set = writer(&offset);
    let next = move |am: &mut AnimationManager| start_pause_at_start(am, offset, owner, overflow);
    am.animate(overflow, 0.0, 0.0, Easing::Linear, set, Some(Box::new(next)), owner);
}

/// Starts, restarts (new text or box width) or stops the scroll for this frame's inputs.
pub fn marquee_scroll_update(am: &mut AnimationManager, s: &mut MarqueeTextState, text: &str, text_width: f32, box_w: f32) {
    let overflow = text_width - box_w;
    if overflow <= 0.5 || instant() {
        if s.marqueeing {
            am.cancel_owner(s.owner);
            s.marqueeing = false;
            s.scroll_offset.set(0.0);
        }
        s.last_text = text.to_owned();
        s.last_box_w = box_w;
        return;
    }
    let inputs_changed = !s.marqueeing || s.last_text != text || s.last_box_w != box_w;
    if !inputs_changed {
        return;
    }
    am.cancel_owner(s.owner);
    s.scroll_offset.set(0.0);
    s.last_text = text.to_owned();
    s.last_box_w = box_w;
    s.marqueeing = true;
    start_pause_at_start(am, s.scroll_offset.clone(), s.owner, overflow);
}

/// Draws `text` in a box `width` wide: plainly when it fits, clipped and scrolled when it does not,
/// ellipsized by the canvas when it does not fit but is not scrolling.
#[allow(clippy::too_many_arguments)]
pub fn draw_marquee_text(
    canvas: &mut dyn Canvas,
    am: &mut AnimationManager,
    s: &mut MarqueeTextState,
    text: &str,
    mut style: TextStyle,
    x: f32,
    y: f32,
    width: f32,
    color: Color,
) {
    style.max_width = 0;
    let size = canvas.measure(text, &style);
    marquee_scroll_update(am, s, text, size.w, width);
    if size.w <= 0.0 {
        return;
    }
    if size.w <= width {
        canvas.text(text, &style, x, y, color);
        return;
    }
    if !s.marqueeing {
        style.max_width = width as i32;
        canvas.text(text, &style, x, y, color);
        return;
    }
    canvas.begin_group(Rect { x, y, w: width, h: size.h }, GroupOptions { clip: true, ..GroupOptions::default() });
    canvas.text(text, &style, -s.scroll_offset.get(), 0.0, color);
    canvas.end_group();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::animation::set_instant;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};
    use crate::render::tokens::WHITE;
    use std::time::{Duration, Instant};

    #[test]
    fn marquee_fits_draws_plain() {
        let (mut c, mut am, mut s) = (RecordingCanvas::default(), AnimationManager::default(), MarqueeTextState::new());
        draw_marquee_text(&mut c, &mut am, &mut s, "abc", TextStyle::default(), 5.0, 6.0, 100.0, WHITE);
        assert_eq!(c.ops.len(), 1);
        assert_eq!((c.ops[0].kind, c.ops[0].rect.x, c.ops[0].rect.y), (OpKind::Text, 5.0, 6.0));
        assert!(!s.marqueeing && !am.has_active());
    }

    #[test]
    fn marquee_overflow_scrolls_clipped() {
        let (mut c, mut am, mut s) = (RecordingCanvas::default(), AnimationManager::default(), MarqueeTextState::new());
        let start = Instant::now();
        draw_marquee_text(&mut c, &mut am, &mut s, "abcdefghij", TextStyle::default(), 0.0, 0.0, 40.0, WHITE); // 90 px in 40
        assert!(s.marqueeing);
        assert_eq!(c.ops[0].kind, OpKind::BeginGroup);
        assert_eq!(c.ops[0].rect, Rect { x: 0.0, y: 0.0, w: 40.0, h: 20.0 });
        am.tick(start + Duration::from_secs(2)); // the 1.2 s pause ends, the scroll starts
        am.tick(start + Duration::from_secs(5)); // 50 px at 35 px/s has finished
        assert_eq!(s.scroll_offset.get(), 50.0);
        c.ops.clear();
        draw_marquee_text(&mut c, &mut am, &mut s, "abcdefghij", TextStyle::default(), 0.0, 0.0, 40.0, WHITE);
        assert_eq!(c.ops[1].rect.x, -50.0);
    }

    #[test]
    fn marquee_restarts_and_stops() {
        let (mut am, mut s) = (AnimationManager::default(), MarqueeTextState::new());
        marquee_scroll_update(&mut am, &mut s, "long", 90.0, 40.0);
        s.scroll_offset.set(20.0);
        marquee_scroll_update(&mut am, &mut s, "longer", 99.0, 40.0);
        assert!(s.marqueeing && s.scroll_offset.get() == 0.0);
        marquee_scroll_update(&mut am, &mut s, "longer", 99.0, 120.0);
        assert!(!s.marqueeing && !am.has_active());
    }

    #[test]
    fn marquee_instant_stops() {
        set_instant(true);
        let (mut am, mut s) = (AnimationManager::default(), MarqueeTextState::new());
        marquee_scroll_update(&mut am, &mut s, "long", 90.0, 40.0); // must return, not recurse
        set_instant(false);
        assert!(!s.marqueeing && s.scroll_offset.get() == 0.0 && !am.has_active());
    }

    #[test]
    fn marquee_instant_mid_chain_stops() {
        let (mut am, mut s) = (AnimationManager::default(), MarqueeTextState::new());
        marquee_scroll_update(&mut am, &mut s, "long", 90.0, 40.0);
        set_instant(true);
        am.tick(Instant::now() + Duration::from_secs(60)); // a stage ends while instant is on
        let idle = !am.has_active();
        marquee_scroll_update(&mut am, &mut s, "long", 90.0, 40.0);
        set_instant(false);
        assert!(idle && !s.marqueeing && s.scroll_offset.get() == 0.0);
    }
}
