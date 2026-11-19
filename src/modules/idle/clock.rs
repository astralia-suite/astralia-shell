use std::collections::BTreeMap;
use std::time::Instant;

use crate::config::settings::{Config, ambient_enabled_for, ambient_timeout_for, screensaver_enabled_for, screensaver_timeout_for};

/// Last activity per output name.
#[derive(Default)]
pub struct Clock {
    last_activity: BTreeMap<String, Instant>,
}

impl Clock {
    pub fn reset<'a>(&mut self, outputs: impl IntoIterator<Item = &'a str>, now: Instant) {
        for output in outputs {
            self.last_activity.insert(output.to_string(), now);
        }
    }

    pub fn activity(&mut self, output: &str, now: Instant) {
        self.last_activity.insert(output.to_string(), now);
    }

    /// Strictly more than `timeout_seconds` whole seconds since the last activity; an unknown output is never idle.
    pub fn idle(&self, output: &str, timeout_seconds: u32, now: Instant) -> bool {
        self.last_activity.get(output).is_some_and(|&t| now.saturating_duration_since(t).as_secs() > u64::from(timeout_seconds))
    }

    pub fn known(&self, output: &str) -> bool {
        self.last_activity.contains_key(output)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Verdict {
    pub ambient: bool,
    pub screensaver: bool,
}

pub fn verdict(clock: &Clock, config: &Config, output: &str, now: Instant) -> Verdict {
    Verdict {
        ambient: ambient_enabled_for(config, output) && clock.idle(output, ambient_timeout_for(config, output), now),
        screensaver: screensaver_enabled_for(config, output) && clock.idle(output, screensaver_timeout_for(config, output), now),
    }
}

pub fn policy_changed(before: &Config, after: &Config) -> bool {
    before.idle_enabled != after.idle_enabled
        || before.ambient_enabled != after.ambient_enabled
        || before.ambient_timeout != after.ambient_timeout
        || before.screensaver_enabled != after.screensaver_enabled
        || before.screensaver_timeout != after.screensaver_timeout
        || before.monitor_overrides != after.monitor_overrides
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::settings::Config;
    use crate::modules::idle::bounce::{Bounce, bounce_step};
    use std::time::{Duration, Instant};

    fn s(n: u64) -> Duration {
        Duration::from_secs(n)
    }

    #[test]
    fn check_idle() {
        let start = Instant::now();
        let mut clock = Clock::default();
        assert!(!clock.idle("A", 0, start + s(100)), "an unknown output is never idle");
        clock.reset(["A", "B"], start);
        assert!(clock.known("A") && clock.known("B"), "a reset seeds every output");
        assert!(!clock.idle("A", 150, start + s(150)) && clock.idle("A", 150, start + s(151)), "idle begins strictly after the timeout");
        clock.activity("A", start + s(140));
        assert!(!clock.idle("A", 150, start + s(200)) && clock.idle("B", 150, start + s(200)), "activity on one output leaves the other idle");

        let mut config = Config::default();
        let mut fresh = Clock::default();
        fresh.reset(["A"], start);
        let early = verdict(&fresh, &config, "A", start + s(100));
        assert!(!early.ambient && !early.screensaver, "nothing shows before the ambient timeout");
        let ambient = verdict(&fresh, &config, "A", start + s(200));
        assert!(ambient.ambient && !ambient.screensaver, "the ambient overlay comes first");
        let saver = verdict(&fresh, &config, "A", start + s(400));
        assert!(saver.ambient && saver.screensaver, "the screensaver follows");
        config.idle_enabled = false;
        let off = verdict(&fresh, &config, "A", start + s(400));
        assert!(!off.ambient && !off.screensaver, "disabled idle management shows nothing");
        config.idle_enabled = true;
        let o = config.monitor_overrides.entry("A".into()).or_default();
        o.enabled = true;
        o.ambient_timeout = 10;
        o.screensaver_enabled = false;
        let overridden = verdict(&fresh, &config, "A", start + s(400));
        assert!(overridden.ambient && !overridden.screensaver, "an override picks its own timeout and switches");

        let before = Config::default();
        let mut after = before.clone();
        assert!(!policy_changed(&before, &after), "equal policies are unchanged");
        after.screensaver_timeout = 30;
        assert!(policy_changed(&before, &after), "a new timeout is a policy change");
        after = before.clone();
        after.idle_enabled = false;
        assert!(policy_changed(&before, &after), "toggling idle management is a policy change");
        after = before.clone();
        after.autohide = true;
        assert!(!policy_changed(&before, &after), "unrelated settings are not");

        let mut bounce = Bounce { x: 10.0, y: 10.0, vx: 100.0, vy: 50.0 };
        bounce_step(&mut bounce, 0.5, 1000.0, 800.0, 200.0);
        assert!(bounce.x == 60.0 && bounce.y == 35.0 && bounce.vx == 100.0, "the logo moves by velocity times time");
        let mut right = Bounce { x: 790.0, y: 10.0, vx: 100.0, vy: 50.0 };
        bounce_step(&mut right, 1.0, 1000.0, 800.0, 200.0);
        assert!(right.x == 800.0 && right.vx == -100.0, "the right edge reverses horizontal motion");
        let mut top = Bounce { x: 10.0, y: 5.0, vx: 100.0, vy: -50.0 };
        bounce_step(&mut top, 1.0, 1000.0, 800.0, 200.0);
        assert!(top.y == 0.0 && top.vy == 50.0, "the top edge reverses vertical motion");
    }
}
