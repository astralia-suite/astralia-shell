//! Frame pacing as a pure state machine; the caller owns the `wl_callback`.
//! `PresentClock` turns `wp_presentation` feedback into the time a frame is drawn for.

use std::time::{Duration, Instant};

/// Feedback older than this no longer describes the output's cadence.
const STALE: Duration = Duration::from_millis(500);

/// The time each frame is drawn for, on the grid of refreshes the compositor reported.
/// Consecutive frames differ by whole refreshes however late their frame callbacks arrive,
/// so an animation advances by the same amount every frame.
#[derive(Debug, Default)]
pub struct PresentClock {
    /// When the last frame reached the screen, and the refresh period then.
    presented: Option<(Instant, Duration)>,
    /// The previous draw: when it happened and the time it was drawn for.
    previous: Option<(Instant, Instant)>,
}

impl PresentClock {
    pub fn presented(&mut self, at: Instant, refresh: Duration) {
        self.presented = Some((at, refresh));
    }

    /// The time the frame drawn at `now` is for; `now` while there is no fresh feedback.
    pub fn target(&mut self, now: Instant) -> Instant {
        let fresh = self.presented.filter(|&(at, refresh)| !refresh.is_zero() && now.saturating_duration_since(at) <= STALE);
        let Some((at, refresh)) = fresh else {
            // Never earlier than a time already drawn for.
            let target = self.previous.map_or(now, |(_, target)| target.max(now));
            self.previous = None;
            return target;
        };
        let grid = first_after(at, refresh, now);
        let target = match self.previous {
            Some((previous_now, previous_target)) if now.saturating_duration_since(previous_now) <= STALE => {
                // Whole refreshes since the previous draw: callback jitter within half a refresh does not move the step.
                let steps = (now.saturating_duration_since(previous_now).as_secs_f64() / refresh.as_secs_f64()).round() as u32;
                let stepped = previous_target + refresh * steps;
                if stepped + refresh < grid || stepped > grid + refresh * 2 { grid.max(previous_target) } else { stepped }
            }
            _ => grid,
        };
        self.previous = Some((now, target));
        target
    }
}

/// The first point of the grid `at + refresh × k` after `now`.
fn first_after(at: Instant, refresh: Duration, now: Instant) -> Instant {
    if at > now {
        return at;
    }
    let steps = now.duration_since(at).as_nanos() / refresh.as_nanos() + 1;
    at + refresh * u32::try_from(steps).unwrap_or(u32::MAX)
}

/// A `CLOCK_MONOTONIC` timestamp as an `Instant`, which reads the same clock on Linux.
pub fn monotonic_to_instant(seconds: u64, nanos: u32) -> Option<Instant> {
    let mut now_ts = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    // SAFETY: `now_ts` is a valid `timespec` to write into.
    if unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut now_ts) } != 0 {
        return None;
    }
    let now = Instant::now();
    let monotonic_now = Duration::new(u64::try_from(now_ts.tv_sec).ok()?, u32::try_from(now_ts.tv_nsec).ok()?);
    let then = Duration::new(seconds, nanos);
    if then <= monotonic_now { now.checked_sub(monotonic_now - then) } else { now.checked_add(then - monotonic_now) }
}

/// What the caller does with the surface after a `request` or `done`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameAction {
    None,
    /// Arm `wl_surface.frame`, then draw (the draw commits).
    ArmAndDraw,
    /// Arm `wl_surface.frame`, damage 1x1 and commit; the draw runs on `done`.
    ArmAndCommit,
}

#[derive(Debug, Default)]
pub struct FrameClock {
    pending: bool,
    redraw_requested: bool,
    mapped: bool,
}

impl FrameClock {
    pub fn request(&mut self) -> FrameAction {
        if self.pending {
            self.redraw_requested = true;
            return FrameAction::None;
        }
        self.pending = true;
        if !self.mapped {
            self.mapped = true;
            return FrameAction::ArmAndDraw;
        }
        self.redraw_requested = true;
        FrameAction::ArmAndCommit
    }

    pub fn done(&mut self) -> FrameAction {
        if !self.pending || !self.redraw_requested {
            self.pending = false;
            return FrameAction::None;
        }
        self.redraw_requested = false;
        FrameAction::ArmAndDraw
    }

    pub fn drop_callback(&mut self) {
        self.pending = false;
        self.redraw_requested = false;
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn pending(&self) -> bool {
        self.pending
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_clock_sequence() {
        let mut c = FrameClock::default();
        assert_eq!(c.request(), FrameAction::ArmAndDraw);
        assert_eq!(c.request(), FrameAction::None);
        assert_eq!(c.done(), FrameAction::ArmAndDraw);
        assert_eq!(c.done(), FrameAction::None);
        assert_eq!(c.request(), FrameAction::ArmAndCommit);
    }

    #[test]
    fn frame_clock_drop_ignores_late_done() {
        let mut c = FrameClock::default();
        c.request();
        c.request();
        c.drop_callback();
        assert!(!c.pending());
        assert_eq!(c.done(), FrameAction::None);
    }

    const REFRESH: Duration = Duration::from_micros(16_667);

    fn ms(value: f64) -> Duration {
        Duration::from_secs_f64(value / 1000.0)
    }

    #[test]
    fn present_clock_falls_back_to_now() {
        let base = Instant::now();
        let mut clock = PresentClock::default();
        assert_eq!(clock.target(base), base, "no feedback draws for now");
        clock.presented(base, Duration::ZERO);
        assert_eq!(clock.target(base + ms(5.0)), base + ms(5.0), "a variable refresh draws for now");
        clock.presented(base, REFRESH);
        let late = base + ms(900.0);
        assert_eq!(clock.target(late), late, "stale feedback draws for now");
    }

    #[test]
    fn present_clock_snaps_to_the_grid() {
        let base = Instant::now();
        let mut clock = PresentClock::default();
        clock.presented(base, REFRESH);
        assert_eq!(clock.target(base + ms(5.0)), base + REFRESH, "the first draw is for the next refresh");
        assert_eq!(clock.target(base + ms(7.0)), base + REFRESH, "a second draw within the refresh keeps its time");
    }

    #[test]
    fn present_clock_steps_whole_refreshes_through_jitter() {
        let base = Instant::now();
        let mut clock = PresentClock::default();
        clock.presented(base, REFRESH);
        // Callbacks straddling the grid points, as shojiwm's render-ahead sends them.
        let callbacks = [16.0, 34.0, 49.5, 67.8, 83.0, 101.5];
        let targets: Vec<Instant> = callbacks.iter().map(|&at| clock.target(base + ms(at))).collect();
        for pair in targets.windows(2) {
            assert_eq!(pair[1] - pair[0], REFRESH, "every frame advances by one refresh");
        }
    }

    #[test]
    fn present_clock_skips_dropped_frames() {
        let base = Instant::now();
        let mut clock = PresentClock::default();
        clock.presented(base, REFRESH);
        let first = clock.target(base + ms(16.0));
        let after_drop = clock.target(base + ms(49.0));
        assert_eq!(after_drop - first, REFRESH * 2, "a dropped frame moves two refreshes");
    }

    #[test]
    fn present_clock_resyncs_and_never_goes_back() {
        let base = Instant::now();
        let mut clock = PresentClock::default();
        clock.presented(base, REFRESH);
        // Callbacks every 0.6 refresh each round up to a whole step and would run ahead of the wall clock.
        let mut previous = clock.target(base);
        for i in 1..40 {
            let now = base + ms(10.0 * f64::from(i));
            let target = clock.target(now);
            assert!(target >= previous, "time never goes backwards");
            assert!(target <= now + REFRESH * 3, "a drifting step resyncs to the grid");
            previous = target;
        }
    }

    #[test]
    fn monotonic_matches_instant() {
        let mut ts = libc::timespec { tv_sec: 0, tv_nsec: 0 };
        // SAFETY: `ts` is a valid `timespec` to write into.
        unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts) };
        let at = monotonic_to_instant(ts.tv_sec as u64, ts.tv_nsec as u32).unwrap();
        assert!(at.elapsed() < Duration::from_millis(50), "a monotonic timestamp of now is about now");
    }
}
