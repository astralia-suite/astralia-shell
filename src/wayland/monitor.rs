//! One output's surfaces, and the coalesced repaint request services send them.

use std::cell::Cell;
use std::io;
use std::rc::Rc;

use calloop::LoopHandle;
use calloop::ping::{Ping, make_ping};
use wayland_client::protocol::wl_output::WlOutput;

use crate::modules::bar::wayland::bar::Bar;
use crate::modules::idle::wayland::IdleOverlay;
use crate::modules::notification::wayland::Notifications;
use crate::modules::osd::wayland::{Osd, OsdShow};
use crate::modules::wallpaper::wayland::Wallpaper;

/// What changed since the last repaint pass.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Reasons {
    /// Something every bar shows.
    pub all: bool,
    /// `config.json`: geometry and style are re-applied first.
    pub config: bool,
    /// Something an open panel shows.
    pub panels: bool,
    /// A wallpaper decode finished.
    pub wallpaper: bool,
    /// The latest level to show on the OSD.
    pub osd: Option<OsdShow>,
    /// The notification list changed.
    pub notifications: bool,
    /// The launcher has finished work to apply, or an icon decoded.
    pub launcher: bool,
    /// The wallpaper picker's scan landed, or a thumbnail decoded.
    pub wallpaper_select: bool,
    /// The settings' scan landed, or a thumbnail decoded.
    pub settings: bool,
    /// A polkit request arrived, changed or ended.
    pub polkit: bool,
}

/// A cloneable request for a repaint; any number of requests before the loop wakes run its handler once.
#[derive(Clone)]
pub struct Redraw {
    ping: Ping,
    pending: Rc<Cell<Reasons>>,
}

impl Redraw {
    pub fn new<D: 'static>(handle: &LoopHandle<'static, D>, mut on: impl FnMut(Reasons, &mut D) + 'static) -> io::Result<Redraw> {
        let (ping, source) = make_ping()?;
        let pending = Rc::new(Cell::new(Reasons::default()));
        let taken = pending.clone();
        handle
            .insert_source(source, move |(), _, data| {
                let reasons = taken.take();
                if reasons != Reasons::default() {
                    on(reasons, data);
                }
            })
            .map_err(|e| io::Error::other(e.error))?;
        Ok(Redraw { ping, pending })
    }

    fn request(&self, f: impl FnOnce(&mut Reasons)) {
        let mut reasons = self.pending.get();
        f(&mut reasons);
        self.pending.set(reasons);
        self.ping.ping();
    }

    /// Bars and open panels.
    pub fn all(&self) {
        self.request(|r| {
            r.all = true;
            r.panels = true;
        });
    }

    pub fn panels(&self) {
        self.request(|r| r.panels = true);
    }

    pub fn config(&self) {
        self.request(|r| r.config = true);
    }

    pub fn osd(&self, show: OsdShow) {
        self.request(|r| r.osd = Some(show));
    }

    pub fn notifications(&self) {
        self.request(|r| r.notifications = true);
    }

    pub fn launcher(&self) {
        self.request(|r| r.launcher = true);
    }

    pub fn wallpaper_select(&self) {
        self.request(|r| r.wallpaper_select = true);
    }

    pub fn settings(&self) {
        self.request(|r| r.settings = true);
    }

    pub fn polkit(&self) {
        self.request(|r| r.polkit = true);
    }

    pub fn wallpaper(&self) {
        self.request(|r| r.wallpaper = true);
    }
}

/// Which surface repaints for a wallpaper change.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrameTarget {
    None,
    Background,
    /// The lock surface covers the wallpaper and draws its columns itself.
    Lock,
}

/// A config change re-lays the wallpaper surface; a pending decode repaints whichever surface shows
/// the wallpaper.
pub fn wallpaper_frame_target(locked: bool, config: bool, pending: bool) -> FrameTarget {
    match (config, pending, locked) {
        (true, _, _) => FrameTarget::Background,
        (false, false, _) => FrameTarget::None,
        (false, true, true) => FrameTarget::Lock,
        (false, true, false) => FrameTarget::Background,
    }
}

/// An output and what the shell shows on it.
pub struct Monitor {
    pub output: WlOutput,
    pub name: String,
    pub scale: i32,
    /// Absent while the bar is disabled on this output.
    pub bar: Option<Bar>,
    pub wallpaper: Option<Wallpaper>,
    pub osd: Option<Osd>,
    /// Only on outputs where notifications are enabled.
    pub notifications: Option<Notifications>,
    /// With the idle capability, on every output but the headless one.
    pub idle: Option<IdleOverlay>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wallpaper_frame_routing() {
        assert_eq!(wallpaper_frame_target(false, false, true), FrameTarget::Background, "a new frame repaints the wallpaper surface");
        assert_eq!(wallpaper_frame_target(true, false, true), FrameTarget::Lock, "while locked the lock surface shows it, so it repaints");
        assert_eq!(wallpaper_frame_target(false, false, false), FrameTarget::None, "nothing pending, nothing repaints");
        assert_eq!(wallpaper_frame_target(true, true, false), FrameTarget::Background, "a config change still re-lays the wallpaper");
    }
    use calloop::EventLoop;
    use std::time::Duration;

    #[test]
    fn redraw_coalesces() {
        let mut event_loop: EventLoop<'static, u32> = EventLoop::try_new().unwrap();
        let seen = Rc::new(Cell::new(Reasons::default()));
        let s = seen.clone();
        let redraw = Redraw::new(&event_loop.handle(), move |reasons, runs: &mut u32| {
            *runs += 1;
            s.set(reasons);
        })
        .unwrap();
        let mut runs = 0;
        for _ in 0..10 {
            redraw.all();
        }
        event_loop.dispatch(Duration::from_millis(50), &mut runs).unwrap();
        assert_eq!(runs, 1, "ten requests between two dispatches run the handler once");
        assert_eq!(seen.get(), Reasons { all: true, panels: true, ..Reasons::default() });
        redraw.clone().config();
        event_loop.dispatch(Duration::from_millis(50), &mut runs).unwrap();
        assert_eq!((runs, seen.get()), (2, Reasons { config: true, ..Reasons::default() }), "each run sees only the reasons since the last");
        event_loop.dispatch(Duration::from_millis(10), &mut runs).unwrap();
        assert_eq!(runs, 2, "nothing requested, nothing run");
    }
}
