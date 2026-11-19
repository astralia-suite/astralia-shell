//! Tweens: a scalar eased from one value to another over time, written through a setter.

use std::cell::Cell;
use std::rc::Rc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Easing {
    Linear,
    EaseOutQuad,
    EaseInOutCubic,
    EaseOutCubic,
    EaseInCubic,
    EaseOutBack,
    EaseInBack,
}

pub fn apply_easing(easing: Easing, t: f32) -> f32 {
    let t = t.clamp(0.0, 1.0);
    const C1: f32 = 1.70158;
    const C3: f32 = C1 + 1.0;
    match easing {
        Easing::Linear => t,
        Easing::EaseOutQuad => t * (2.0 - t),
        Easing::EaseInOutCubic => {
            if t < 0.5 {
                4.0 * t * t * t
            } else {
                let f = 2.0 * t - 2.0;
                0.5 * f * f * f + 1.0
            }
        }
        Easing::EaseOutCubic => {
            let f = t - 1.0;
            f * f * f + 1.0
        }
        Easing::EaseInCubic => t * t * t,
        Easing::EaseOutBack => {
            let f = t - 1.0;
            1.0 + C3 * f * f * f + C1 * f * f
        }
        Easing::EaseInBack => C3 * t * t * t - C1 * t * t,
    }
}

thread_local! {
    static INSTANT: Cell<bool> = const { Cell::new(false) };
}

/// While on, every tween snaps to its end value as it starts.
pub fn set_instant(on: bool) {
    INSTANT.with(|f| f.set(on));
}

pub fn instant() -> bool {
    INSTANT.with(Cell::get)
}

/// An animated scalar: the owner reads it, the tween writes it.
pub type Value = Rc<Cell<f32>>;

pub fn value(v: f32) -> Value {
    Rc::new(Cell::new(v))
}

pub fn writer(v: &Value) -> impl FnMut(f32) + 'static {
    let v = v.clone();
    move |x| v.set(x)
}

pub type OnComplete = Box<dyn FnOnce(&mut AnimationManager)>;

/// Owner ids for state with no fixed id; above every small constant owner.
pub fn unique_owner() -> u64 {
    static NEXT: AtomicU64 = AtomicU64::new(1 << 32);
    NEXT.fetch_add(1, Ordering::Relaxed)
}

struct Entry {
    owner: u64,
    from: f32,
    to: f32,
    duration_ms: f32,
    started_at: Instant,
    easing: Easing,
    setter: Box<dyn FnMut(f32)>,
    on_complete: Option<OnComplete>,
}

#[derive(Default)]
pub struct AnimationManager {
    entries: Vec<Entry>,
    next_id: u32,
}

impl AnimationManager {
    /// Starts a tween, replacing any tween of the same non-zero `owner`. Returns 0 when it snapped.
    #[allow(clippy::too_many_arguments)]
    pub fn animate(
        &mut self,
        from: f32,
        to: f32,
        duration_ms: f32,
        easing: Easing,
        mut setter: impl FnMut(f32) + 'static,
        on_complete: Option<OnComplete>,
        owner: u64,
    ) -> u32 {
        self.cancel_owner(owner);
        if duration_ms <= 0.0 || instant() {
            setter(to);
            if let Some(f) = on_complete {
                f(self);
            }
            return 0;
        }
        self.next_id += 1;
        self.entries.push(Entry {
            owner,
            from,
            to,
            duration_ms,
            started_at: Instant::now(),
            easing,
            setter: Box::new(setter),
            on_complete,
        });
        self.next_id
    }

    /// Drops every tween of `owner` without writing or completing it. Owner 0 is never cancelled.
    pub fn cancel_owner(&mut self, owner: u64) {
        if owner != 0 {
            self.entries.retain(|e| e.owner != owner);
        }
    }

    pub fn tick(&mut self, now: Instant) {
        let mut completed = Vec::new();
        self.entries.retain_mut(|e| {
            let elapsed_ms = now.saturating_duration_since(e.started_at).as_secs_f32() * 1000.0;
            let t = (elapsed_ms / e.duration_ms).min(1.0);
            (e.setter)(e.from + (e.to - e.from) * apply_easing(e.easing, t));
            if t < 1.0 {
                return true;
            }
            completed.extend(e.on_complete.take());
            false
        });
        for f in completed {
            f(self);
        }
    }

    pub fn has_active(&self) -> bool {
        !self.entries.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    const ALL: [Easing; 7] = [
        Easing::Linear,
        Easing::EaseOutQuad,
        Easing::EaseInOutCubic,
        Easing::EaseOutCubic,
        Easing::EaseInCubic,
        Easing::EaseOutBack,
        Easing::EaseInBack,
    ];

    fn counter() -> (Rc<Cell<u32>>, OnComplete) {
        let n = Rc::new(Cell::new(0));
        let m = n.clone();
        (n, Box::new(move |_: &mut AnimationManager| m.set(m.get() + 1)))
    }

    #[test]
    fn easing_endpoints() {
        for e in ALL {
            assert!(apply_easing(e, 0.0).abs() < 1e-5, "{e:?} starts at zero");
            assert!((apply_easing(e, 1.0) - 1.0).abs() < 1e-5, "{e:?} ends at one");
        }
        assert!(apply_easing(Easing::EaseOutQuad, 0.5) > 0.5);
        assert!(apply_easing(Easing::EaseInCubic, 0.5) < 0.5);
        assert!(apply_easing(Easing::EaseInBack, 0.5) < 0.0);
    }

    #[test]
    fn interpolation() {
        let mut m = AnimationManager::default();
        let v = value(-1.0);
        let (done, on_complete) = counter();
        let now = Instant::now();
        m.animate(0.0, 10.0, 100.0, Easing::Linear, writer(&v), Some(on_complete), 0);
        m.tick(now);
        assert_eq!(v.get(), 0.0);
        m.tick(now + Duration::from_millis(50));
        // `animate` stamps its own start after `now`; allow 5 ms of that gap under a loaded test run.
        assert!((v.get() - 5.0).abs() < 0.5, "midpoint {}", v.get());
        assert!(done.get() == 0 && m.has_active());
        m.tick(now + Duration::from_millis(150));
        assert!((v.get() - 10.0).abs() < 1e-3);
        assert!(done.get() == 1 && !m.has_active());
    }

    #[test]
    fn zero_duration_snaps() {
        let mut m = AnimationManager::default();
        let v = value(-1.0);
        let (done, on_complete) = counter();
        m.animate(0.0, 5.0, 0.0, Easing::Linear, writer(&v), Some(on_complete), 0);
        assert!(v.get() == 5.0 && done.get() == 1 && !m.has_active());
    }

    #[test]
    fn owner_replaces_and_cancels() {
        let mut m = AnimationManager::default();
        let (first, second) = (value(-1.0), value(-1.0));
        let now = Instant::now();
        m.animate(0.0, 10.0, 100.0, Easing::Linear, writer(&first), None, 1);
        m.animate(0.0, 20.0, 100.0, Easing::Linear, writer(&second), None, 1);
        m.tick(now + Duration::from_millis(150));
        assert!(first.get() == -1.0 && second.get() == 20.0);

        let cancelled = value(-1.0);
        let (done, on_complete) = counter();
        m.animate(0.0, 10.0, 100.0, Easing::Linear, writer(&cancelled), Some(on_complete), 2);
        m.cancel_owner(2);
        m.tick(now + Duration::from_millis(400));
        assert!(!m.has_active() && cancelled.get() == -1.0 && done.get() == 0);
    }

    #[test]
    fn instant_snaps() {
        let mut m = AnimationManager::default();
        let v = value(-1.0);
        let (done, on_complete) = counter();
        set_instant(true);
        assert!(instant());
        m.animate(0.0, 7.0, 500.0, Easing::EaseOutCubic, writer(&v), Some(on_complete), 0);
        set_instant(false);
        assert!(v.get() == 7.0 && done.get() == 1 && !m.has_active());
        assert!(!instant());
    }

    #[test]
    fn tick_before_start_holds_from() {
        let mut m = AnimationManager::default();
        let v = value(-1.0);
        let early = Instant::now();
        m.animate(3.0, 9.0, 100.0, Easing::Linear, writer(&v), None, 0);
        m.tick(early); // earlier than the tween's start
        assert_eq!(v.get(), 3.0);
        assert!(m.has_active());
    }

    #[test]
    fn on_complete_can_chain() {
        let mut m = AnimationManager::default();
        let v = value(0.0);
        let w = v.clone();
        let now = Instant::now();
        m.animate(
            0.0,
            1.0,
            10.0,
            Easing::Linear,
            writer(&v),
            Some(Box::new(move |m: &mut AnimationManager| {
                m.animate(1.0, 2.0, 10.0, Easing::Linear, writer(&w), None, 0);
            })),
            0,
        );
        m.tick(now + Duration::from_millis(20));
        assert_eq!(v.get(), 1.0);
        assert!(m.has_active()); // the chained tween is running
    }

    #[test]
    fn unique_owners_are_large_and_distinct() {
        let (a, b) = (unique_owner(), unique_owner());
        assert!(a >= 1 << 32 && b >= 1 << 32 && a != b);
    }
}
