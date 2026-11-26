//! `wp_presentation`: when each committed frame reached the screen, fed into the window's `PresentClock`.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use wayland_client::globals::GlobalList;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Connection, Dispatch, QueueHandle};
use wayland_protocols::wp::presentation_time::client::wp_presentation::{self, WpPresentation};
use wayland_protocols::wp::presentation_time::client::wp_presentation_feedback::{self, WpPresentationFeedback};

use super::State;
use super::frame::{PresentClock, monotonic_to_instant};

pub type SharedClock = Arc<Mutex<PresentClock>>;

/// Bound once at connect; `None` on a compositor without the protocol.
static PRESENTATION: OnceLock<WpPresentation> = OnceLock::new();
/// Timestamps are used only on `CLOCK_MONOTONIC`, the clock `Instant` reads.
static MONOTONIC: AtomicBool = AtomicBool::new(false);

pub fn bind(globals: &GlobalList, qh: &QueueHandle<State>) {
    match globals.bind::<WpPresentation, _, _>(qh, 1..=1, ()) {
        Ok(presentation) => {
            let _ = PRESENTATION.set(presentation);
        }
        Err(_) => ::log::info!("presentation: unavailable, animations follow the wall clock"),
    }
}

/// Asks when the next commit of `surface` reaches the screen.
pub fn request(surface: &WlSurface, qh: &QueueHandle<State>, clock: &SharedClock) {
    if let Some(presentation) = PRESENTATION.get() {
        presentation.feedback(surface, qh, clock.clone());
    }
}

/// `ASTRALIA_FRAME_DEBUG=1` logs the step between the times frames are drawn for.
pub fn debug() -> bool {
    static DEBUG: OnceLock<bool> = OnceLock::new();
    *DEBUG.get_or_init(|| std::env::var("ASTRALIA_FRAME_DEBUG").as_deref() == Ok("1"))
}

impl Dispatch<WpPresentation, ()> for State {
    fn event(_: &mut State, _: &WpPresentation, event: wp_presentation::Event, _: &(), _: &Connection, _: &QueueHandle<State>) {
        if let wp_presentation::Event::ClockId { clk_id } = event {
            let monotonic = i64::from(clk_id) == i64::from(libc::CLOCK_MONOTONIC);
            MONOTONIC.store(monotonic, Ordering::Relaxed);
            if !monotonic {
                ::log::info!("presentation: clock {clk_id} is not CLOCK_MONOTONIC, animations follow the wall clock");
            }
        }
    }
}

impl Dispatch<WpPresentationFeedback, SharedClock> for State {
    fn event(_: &mut State, _: &WpPresentationFeedback, event: wp_presentation_feedback::Event, clock: &SharedClock, _: &Connection, _: &QueueHandle<State>) {
        let wp_presentation_feedback::Event::Presented { tv_sec_hi, tv_sec_lo, tv_nsec, refresh, .. } = event else { return };
        if !MONOTONIC.load(Ordering::Relaxed) {
            return;
        }
        let seconds = (u64::from(tv_sec_hi) << 32) | u64::from(tv_sec_lo);
        let Some(at) = monotonic_to_instant(seconds, tv_nsec) else { return };
        if let Ok(mut clock) = clock.lock() {
            clock.presented(at, Duration::from_nanos(u64::from(refresh)));
        }
    }
}
