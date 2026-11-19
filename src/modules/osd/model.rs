//! What the OSD card shows and when it hides.

use std::cell::Cell;
use std::rc::Rc;
use std::time::{Duration, Instant};

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::config::osd_config as cfg;
use crate::render::glyphs::{brightness_threshold, volume_threshold};
use crate::render::icons;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Kind {
    #[default]
    Volume,
    Mic,
    Brightness,
}

const OWNER_OPACITY: u64 = 1;
const OWNER_BAR_FILL: u64 = 2;
const OWNER_ICON_MIX: u64 = 3;

/// What an idle model reports as the time to its next change: never, in practice.
const IDLE: Duration = Duration::from_secs(3600);

pub struct Model {
    kind: Kind,
    percent: i32,
    muted: bool,
    /// Shared with the fade-out's completion, which clears it.
    visible: Rc<Cell<bool>>,
    hiding: Rc<Cell<bool>>,
    opacity: Value,
    bar_fill: Value,
    icon_mix: Value,
    created: Instant,
    hide_at: Instant,
    animations: AnimationManager,
}

impl Model {
    /// `created` starts the ready delay, so the volume and brightness read at startup show nothing.
    pub fn new(created: Instant) -> Model {
        Model {
            kind: Kind::Volume,
            percent: 0,
            muted: false,
            visible: Rc::default(),
            hiding: Rc::default(),
            opacity: value(0.0),
            bar_fill: value(0.0),
            icon_mix: value(0.0),
            created,
            hide_at: created,
            animations: AnimationManager::default(),
        }
    }

    /// Shows `percent` of `kind` and restarts the hide timer; false during the ready delay.
    pub fn show(&mut self, kind: Kind, percent: i32, muted: bool, now: Instant) -> bool {
        if now.saturating_duration_since(self.created) < cfg::READY_DELAY {
            return false;
        }
        self.kind = kind;
        self.percent = percent.clamp(0, 100);
        self.muted = muted;
        self.visible.set(true);
        self.hiding.set(false);
        self.hide_at = now + cfg::VISIBLE_FOR;
        let a = &mut self.animations;
        a.animate(self.opacity.get(), 1.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&self.opacity), None, OWNER_OPACITY);
        a.animate(self.bar_fill.get(), self.percent as f32 / 100.0, cfg::ANIM_FAST_MS, Easing::EaseOutCubic, writer(&self.bar_fill), None, OWNER_BAR_FILL);
        let mix = if muted { 1.0 } else { 0.0 };
        a.animate(self.icon_mix.get(), mix, cfg::ANIM_FAST_MS, Easing::Linear, writer(&self.icon_mix), None, OWNER_ICON_MIX);
        true
    }

    pub fn hide(&mut self) {
        if self.hiding.get() {
            return;
        }
        self.hiding.set(true);
        let (visible, hiding) = (self.visible.clone(), self.hiding.clone());
        let done = Box::new(move |_: &mut AnimationManager| {
            visible.set(false);
            hiding.set(false);
        });
        self.animations.animate(self.opacity.get(), 0.0, cfg::ANIM_NORMAL_MS, Easing::EaseOutCubic, writer(&self.opacity), Some(done), OWNER_OPACITY);
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        if self.visible.get() && now >= self.hide_at {
            self.hide();
        }
    }

    /// The wait until the card starts hiding; an hour when it is hidden.
    pub fn until_hide(&self, now: Instant) -> Duration {
        if self.visible.get() { self.hide_at.saturating_duration_since(now) } else { IDLE }
    }

    pub fn glyph(&self) -> &'static str {
        match self.kind {
            Kind::Brightness => brightness_threshold(self.percent),
            Kind::Mic if self.muted => icons::MIC_OFF,
            Kind::Mic => icons::MIC_ON,
            Kind::Volume => volume_threshold(self.muted, self.percent),
        }
    }

    pub fn label(&self) -> String {
        if self.muted { "muted".into() } else { format!("{}%", self.percent) }
    }

    #[cfg(test)]
    pub fn percent(&self) -> i32 {
        self.percent
    }

    pub fn muted(&self) -> bool {
        self.muted
    }

    pub fn visible(&self) -> bool {
        self.visible.get()
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn opacity(&self) -> f32 {
        self.opacity.get()
    }

    pub fn bar_fill(&self) -> f32 {
        self.bar_fill.get()
    }

    pub fn icon_mix(&self) -> f32 {
        self.icon_mix.get()
    }

    pub fn hide_at(&self) -> Instant {
        self.hide_at
    }
}
