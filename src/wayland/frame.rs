//! Frame pacing as a pure state machine; the caller owns the `wl_callback`.

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
}
