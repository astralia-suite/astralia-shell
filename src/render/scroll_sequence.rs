//! Opening and closing a scroll panel: slide in, unroll, show the grid; and the reverse.

use std::cell::Cell;
use std::rc::Rc;

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};

/// Called once a close has fully finished.
pub type OnClosed = Box<dyn FnOnce()>;

#[derive(Clone, Copy, Debug)]
pub struct ScrollTimings {
    pub slide_ms: f32,
    pub scroll_ms: f32,
    pub grid_in_ms: f32,
    pub grid_out_ms: f32,
}

impl Default for ScrollTimings {
    fn default() -> Self {
        ScrollTimings { slide_ms: 200.0, scroll_ms: 280.0, grid_in_ms: 200.0, grid_out_ms: 320.0 }
    }
}

/// Three chained tweens on owners `owner_base` (slide), `+ 1` (scroll) and `+ 2` (grid).
pub struct ScrollSequence {
    owner: u64,
    timings: ScrollTimings,
    slide: Value,
    scroll: Value,
    grid: Value,
    closing: Rc<Cell<bool>>,
}

impl ScrollSequence {
    pub fn new(owner_base: u64, timings: ScrollTimings) -> ScrollSequence {
        ScrollSequence {
            owner: owner_base,
            timings,
            slide: value(0.0),
            scroll: value(0.0),
            grid: value(0.0),
            closing: Rc::new(Cell::new(false)),
        }
    }

    /// Restarts from fully out and closed; cancels a close in flight without reporting it.
    pub fn open(&self, am: &mut AnimationManager) {
        self.closing.set(false);
        self.slide.set(1.0);
        self.scroll.set(0.0);
        self.grid.set(0.0);
        am.cancel_owner(self.owner + 1);
        am.cancel_owner(self.owner + 2);
        let (owner, t) = (self.owner, self.timings);
        let (scroll, grid, closing) = (self.scroll.clone(), self.grid.clone(), self.closing.clone());
        let unroll = move |am: &mut AnimationManager| {
            if closing.get() {
                return;
            }
            let show_grid = move |am: &mut AnimationManager| {
                if closing.get() {
                    return;
                }
                am.animate(grid.get(), 1.0, t.grid_in_ms, Easing::EaseOutCubic, writer(&grid), None, owner + 2);
            };
            let from = scroll.get();
            am.animate(from, 1.0, t.scroll_ms, Easing::EaseOutCubic, writer(&scroll), Some(Box::new(show_grid)), owner + 1);
        };
        am.animate(1.0, 0.0, t.slide_ms, Easing::EaseOutCubic, writer(&self.slide), Some(Box::new(unroll)), owner);
    }

    /// Hides the grid, rolls up, slides out, then calls `done`. Ignored while already closing.
    /// Each stage's duration is scaled by how far it has to go, so an early close finishes at once.
    pub fn close(&self, am: &mut AnimationManager, done: Option<OnClosed>) {
        if self.closing.get() {
            return;
        }
        self.closing.set(true);
        let (owner, t) = (self.owner, self.timings);
        let (slide, scroll, closing) = (self.slide.clone(), self.scroll.clone(), self.closing.clone());
        let roll_up = move |am: &mut AnimationManager| {
            let slide_out = move |am: &mut AnimationManager| {
                let finish = move |_: &mut AnimationManager| {
                    closing.set(false);
                    if let Some(done) = done {
                        done();
                    }
                };
                let s = slide.get();
                am.animate(s, 1.0, t.slide_ms * (1.0 - s), Easing::EaseOutCubic, writer(&slide), Some(Box::new(finish)), owner);
            };
            let s = scroll.get();
            am.animate(s, 0.0, t.scroll_ms * s, Easing::EaseOutCubic, writer(&scroll), Some(Box::new(slide_out)), owner + 1);
        };
        let g = self.grid.get();
        am.animate(g, 0.0, t.grid_out_ms * g, Easing::EaseInOutCubic, writer(&self.grid), Some(Box::new(roll_up)), owner + 2);
    }

    pub fn slide(&self) -> f32 {
        self.slide.get()
    }

    pub fn scroll(&self) -> f32 {
        self.scroll.get()
    }

    pub fn grid(&self) -> f32 {
        self.grid.get()
    }

    /// Fully in and unrolled (the grid may still be fading in).
    pub fn settled(&self) -> bool {
        self.scroll.get() == 1.0 && self.slide.get() == 0.0
    }

    pub fn closing(&self) -> bool {
        self.closing.get()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::animation::set_instant;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    struct Clock(Instant);

    impl Clock {
        fn tick(&self, am: &mut AnimationManager, s: u64) {
            am.tick(self.0 + Duration::from_secs(s));
        }
    }

    fn counter() -> (Rc<Cell<u32>>, impl Fn() -> Option<OnClosed>) {
        let n = Rc::new(Cell::new(0));
        let m = n.clone();
        (n, move || {
            let m = m.clone();
            Some(Box::new(move || m.set(m.get() + 1)) as OnClosed)
        })
    }

    #[test]
    fn sequence_open() {
        let c = Clock(Instant::now());
        let mut am = AnimationManager::default();
        let s = ScrollSequence::new(10, ScrollTimings::default());
        s.open(&mut am);
        assert!(s.slide() == 1.0 && s.scroll() == 0.0 && s.grid() == 0.0 && am.has_active());
        c.tick(&mut am, 1);
        assert!(s.slide() == 0.0 && s.scroll() == 0.0 && !s.settled(), "slides in first");
        c.tick(&mut am, 2);
        assert!(s.scroll() == 1.0 && s.grid() == 0.0 && s.settled(), "then unrolls");
        c.tick(&mut am, 3);
        assert!(s.grid() == 1.0 && !am.has_active(), "then the grid appears");
    }

    #[test]
    fn sequence_close() {
        let c = Clock(Instant::now());
        let mut am = AnimationManager::default();
        let s = ScrollSequence::new(10, ScrollTimings::default());
        s.open(&mut am);
        for t in 1..=3 {
            c.tick(&mut am, t);
        }
        let (done, report) = counter();
        s.close(&mut am, report());
        assert!(s.closing() && done.get() == 0);
        c.tick(&mut am, 4);
        assert!(s.grid() == 0.0 && s.scroll() == 1.0, "grid goes first");
        c.tick(&mut am, 5);
        assert!(s.scroll() == 0.0 && s.slide() == 0.0 && done.get() == 0, "then rolls up");
        c.tick(&mut am, 6);
        assert!(s.slide() == 1.0 && done.get() == 1 && !s.closing() && !am.has_active(), "slides out, reports once");

        s.open(&mut am);
        for t in 7..=9 {
            c.tick(&mut am, t);
        }
        s.close(&mut am, report());
        s.close(&mut am, report());
        for t in 10..14 {
            c.tick(&mut am, t);
        }
        assert_eq!(done.get(), 2, "a second close while closing is ignored");
    }

    #[test]
    fn sequence_interrupts() {
        let c = Clock(Instant::now());
        let mut am = AnimationManager::default();
        let s = ScrollSequence::new(10, ScrollTimings::default());
        let (done, report) = counter();
        s.open(&mut am);
        s.close(&mut am, report());
        assert!(done.get() == 1 && s.scroll() == 0.0 && s.slide() == 1.0, "close before the rise finishes at once");

        s.open(&mut am);
        am.tick(Instant::now() + Duration::from_millis(100));
        assert!(s.slide() > 0.0 && s.slide() < 1.0, "part way in");
        s.close(&mut am, report());
        assert!(s.scroll() == 0.0 && done.get() == 1, "close during the rise never unrolls");
        c.tick(&mut am, 2);
        c.tick(&mut am, 3);
        assert!(done.get() == 2 && s.scroll() == 0.0 && s.slide() == 1.0, "close still completes");

        let mut other = AnimationManager::default();
        let again = ScrollSequence::new(10, ScrollTimings::default());
        let (finished, report) = counter();
        again.open(&mut other);
        for t in 1..=3 {
            c.tick(&mut other, t);
        }
        again.close(&mut other, report());
        c.tick(&mut other, 4);
        again.open(&mut other);
        for t in 5..9 {
            c.tick(&mut other, t);
        }
        assert!(
            finished.get() == 0 && !again.closing() && again.settled() && again.grid() == 1.0,
            "opening again cancels a close in flight"
        );
    }

    #[test]
    fn sequence_instant() {
        set_instant(true);
        let mut am = AnimationManager::default();
        let s = ScrollSequence::new(10, ScrollTimings::default());
        s.open(&mut am);
        let opened = s.settled() && s.grid() == 1.0 && !am.has_active();
        let (done, report) = counter();
        s.close(&mut am, report());
        set_instant(false);
        assert!(opened, "instant opens straight to the end");
        assert!(done.get() == 1 && s.slide() == 1.0 && !s.closing(), "and closes inside the call");
    }
}
