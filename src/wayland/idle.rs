//! The `ext-idle-notify-v1` recent-activity pulse, the idle clock it drives and the per-monitor overlays.

use std::time::Instant;

use wayland_client::protocol::wl_seat::WlSeat;
use wayland_client::{Connection, Dispatch, QueueHandle};
use wayland_protocols::ext::idle_notify::v1::client::ext_idle_notification_v1::{self, ExtIdleNotificationV1};
use wayland_protocols::ext::idle_notify::v1::client::ext_idle_notifier_v1::ExtIdleNotifierV1;

use super::State;
use crate::config::idle_config::RECENT_ACTIVITY_PULSE_SECONDS;
use crate::config::settings::Config;
use crate::modules::idle::clock::{Clock, policy_changed, verdict};

/// Marks the focused monitor active unless the seat has been idle for the pulse.
pub fn idle_tick(clock: &mut Clock, idled: bool, focused: &str, now: Instant) {
    if !idled && !focused.is_empty() {
        clock.activity(focused, now);
    }
}

/// Reseeds every output when the idle policy changed; true when it did.
pub fn policy_reset<'a>(clock: &mut Clock, before: &Config, after: &Config, outputs: impl IntoIterator<Item = &'a str>, now: Instant) -> bool {
    if !policy_changed(before, after) {
        return false;
    }
    clock.reset(outputs, now);
    true
}

pub struct Idle {
    notifier: Option<ExtIdleNotifierV1>,
    notification: Option<ExtIdleNotificationV1>,
    /// The seat has had no input for the pulse.
    idled: bool,
    pub clock: Clock,
    policy: Option<Config>,
}

impl Idle {
    pub fn new(notifier: Option<ExtIdleNotifierV1>) -> Idle {
        Idle { notifier, notification: None, idled: false, clock: Clock::default(), policy: None }
    }

    /// Asks for the recent-activity pulse on `seat`, once.
    pub fn init(&mut self, seat: Option<&WlSeat>, qh: &QueueHandle<State>) -> bool {
        if self.notification.is_some() {
            return true;
        }
        let (Some(notifier), Some(seat)) = (&self.notifier, seat) else {
            ::log::info!("idle: compositor is missing ext_idle_notifier_v1 or wl_seat, skipping");
            return false;
        };
        self.notification = Some(notifier.get_idle_notification(RECENT_ACTIVITY_PULSE_SECONDS * 1000, seat, qh, ()));
        true
    }
}

impl Drop for Idle {
    fn drop(&mut self) {
        if let Some(n) = self.notification.take() {
            n.destroy();
        }
    }
}

impl Dispatch<ExtIdleNotifierV1, ()> for State {
    fn event(_: &mut Self, _: &ExtIdleNotifierV1, _: <ExtIdleNotifierV1 as wayland_client::Proxy>::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {}
}

impl Dispatch<ExtIdleNotificationV1, ()> for State {
    fn event(state: &mut Self, _: &ExtIdleNotificationV1, event: ext_idle_notification_v1::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        let Some(idle) = &mut state.idle else { return };
        match event {
            ext_idle_notification_v1::Event::Idled => idle.idled = true,
            ext_idle_notification_v1::Event::Resumed => idle.idled = false,
            _ => {}
        }
    }
}

impl State {
    /// The once-a-second idle pass: activity, then each monitor's verdict, fade and wallpaper pause.
    pub fn idle_timer_tick(&mut self) {
        if self.idle.is_none() {
            return;
        }
        let now = Instant::now();
        let focused = self.core.services.as_ref().map(|s| s.compositor.state().focused_monitor.clone()).unwrap_or_default();
        let cfg = self.config();
        let Some(idle) = &mut self.idle else { return };
        idle_tick(&mut idle.clock, idle.idled, &focused, now);
        let mut due = Vec::new();
        for (i, monitor) in self.monitors.iter_mut().enumerate() {
            let Some(overlay) = &mut monitor.idle else { continue };
            if !idle.clock.known(&monitor.name) {
                idle.clock.activity(&monitor.name, now);
            }
            let (frame, pause) = overlay.update(verdict(&idle.clock, &cfg, &monitor.name, now), &cfg.idle_logo_path);
            if frame {
                due.push(i);
            }
            if let (Some(paused), Some(wallpaper)) = (pause, &mut monitor.wallpaper) {
                if paused {
                    wallpaper.pause_animation();
                } else {
                    wallpaper.resume_animation();
                }
            }
        }
        for i in due {
            self.request_idle_frame(i);
        }
    }

    /// A policy change restarts every monitor's clock.
    pub fn idle_apply_config(&mut self, cfg: &Config) {
        let Some(idle) = &mut self.idle else { return };
        let names: Vec<&str> = self.monitors.iter().map(|m| m.name.as_str()).collect();
        if let Some(before) = &idle.policy {
            policy_reset(&mut idle.clock, before, cfg, names, Instant::now());
        }
        idle.policy = Some(cfg.clone());
    }

    pub fn request_idle_frame(&mut self, i: usize) {
        let Some(overlay) = &mut self.monitors[i].idle else { return };
        if overlay.window.request_frame(&self.qh) {
            self.draw_idle(i);
        }
    }

    pub fn draw_idle(&mut self, i: usize) {
        let monitor = &mut self.monitors[i];
        let Some(overlay) = &mut monitor.idle else { return };
        if overlay.draw(&self.egl, &mut self.renderer, monitor.wallpaper.as_ref()) {
            overlay.window.request_frame(&self.qh);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::settings::Config;
    use crate::modules::idle::clock::Clock;
    use std::time::{Duration, Instant};

    #[test]
    fn idle_policy_reset() {
        let start = Instant::now();
        let later = start + Duration::from_secs(500);
        let mut clock = Clock::default();
        clock.reset(["A", "B"], start);
        let before = Config::default();
        let after = Config { autohide: true, ..before.clone() };
        assert!(!policy_reset(&mut clock, &before, &after, ["A", "B"], later), "an unrelated change does not reseed");
        assert!(clock.idle("A", 300, later));
        let after = Config { ambient_timeout: 30, ..before.clone() };
        assert!(policy_reset(&mut clock, &before, &after, ["A", "B"], later), "a policy change reseeds");
        assert!(!clock.idle("A", 0, later) && !clock.idle("B", 0, later), "every monitor starts fresh");
    }

    #[test]
    fn idle_tick_focused() {
        let start = Instant::now();
        let mut clock = Clock::default();
        idle_tick(&mut clock, false, "A", start);
        assert!(clock.known("A"), "recent activity marks the focused monitor");
        idle_tick(&mut clock, true, "B", start);
        assert!(!clock.known("B"), "nothing while the seat is idle");
        idle_tick(&mut clock, false, "", start);
    }
}
