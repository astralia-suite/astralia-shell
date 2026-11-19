//! The logout choreography: the logo scales in, slashes run round the ring, a burst pushes the
//! buttons out, and the exit plays it the other way.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::Instant;

use super::layout::{Point, button_at};
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::logout_config as cfg;

pub const COUNT: usize = cfg::BUTTON_COUNT as usize;

const OWNER_LOGO: u64 = 1;
const OWNER_CLOSE_CHAIN: u64 = 3;
const OWNER_EXIT: u64 = 4;
const OWNER_BURST: u64 = 5;
const OWNER_HOLD: u64 = 6;
const OWNER_CHARGE: u64 = 7;

fn owner_scale(i: usize) -> u64 {
    30 + i as u64
}
fn owner_border(i: usize) -> u64 {
    40 + i as u64
}
fn owner_slash(i: usize) -> u64 {
    50 + i as u64
}
fn owner_push(i: usize) -> u64 {
    60 + i as u64
}
fn owner_gate(i: usize) -> u64 {
    80 + i as u64
}

/// The commands of the ring, with the lock command in its slot when the session can lock.
pub fn commands(with_lock: bool) -> [String; COUNT] {
    let mut commands: [String; COUNT] = std::array::from_fn(|i| cfg::ACTIONS[i].command.to_string());
    if with_lock {
        commands[cfg::LOCK_INDEX as usize] = cfg::LOCK_COMMAND.to_string();
    }
    commands
}

/// The next step of the choreography, queued by a finished tween and run by the model.
#[derive(Clone, Copy, Debug)]
enum Step {
    LogoDone,
    StartSlashes,
    Gate(usize),
    HoldDone,
    BurstDone,
    ExitBurstDone,
}

type OnExecute = Box<dyn FnMut(&str)>;

fn values() -> [Value; COUNT] {
    std::array::from_fn(|_| value(0.0))
}

pub struct Model {
    commands: [String; COUNT],
    open: bool,
    exiting: bool,
    input_ready: bool,
    selected: usize,
    hovered: Option<usize>,
    logo_scale: Value,
    exit_fade: Value,
    burst: Value,
    charge: Value,
    burst_started: Instant,
    slash: [Value; COUNT],
    travel: [Value; COUNT],
    highlight_scale: [Value; COUNT],
    highlight_border: [Value; COUNT],
    animations: AnimationManager,
    steps: Rc<RefCell<Vec<Step>>>,
    pub on_execute: Option<OnExecute>,
    pub on_closed: Option<Box<dyn FnMut()>>,
}

impl Model {
    pub fn new(commands: [String; COUNT]) -> Model {
        Model {
            commands,
            open: false,
            exiting: false,
            input_ready: false,
            selected: 0,
            hovered: None,
            logo_scale: value(0.0),
            exit_fade: value(0.0),
            burst: value(0.0),
            charge: value(0.0),
            burst_started: Instant::now(),
            slash: values(),
            travel: values(),
            highlight_scale: values(),
            highlight_border: values(),
            animations: AnimationManager::default(),
            steps: Rc::default(),
            on_execute: None,
            on_closed: None,
        }
    }

    fn then(&self, step: Step) -> Option<crate::base::animation::OnComplete> {
        let steps = self.steps.clone();
        Some(Box::new(move |_: &mut AnimationManager| steps.borrow_mut().push(step)))
    }

    fn schedule_after(&mut self, delay_ms: f32, owner: u64, step: Step) {
        let done = self.then(step);
        self.animations.animate(0.0, 1.0, delay_ms, Easing::Linear, |_| {}, done, owner);
    }

    /// Runs queued steps until none is left; a step may queue more (at once when tweens snap).
    fn run_steps(&mut self) {
        loop {
            let steps = std::mem::take(&mut *self.steps.borrow_mut());
            if steps.is_empty() {
                return;
            }
            for step in steps {
                match step {
                    Step::LogoDone => self.schedule_after(cfg::HOLD_MS, OWNER_HOLD, Step::StartSlashes),
                    Step::StartSlashes => self.start_slashes(),
                    Step::Gate(i) => {
                        let slash = &self.slash[i];
                        self.animations.animate(0.0, 1.0, cfg::SLASH_MS, Easing::EaseOutCubic, writer(slash), None, owner_slash(i));
                    }
                    Step::HoldDone if self.exiting => self.start_exit_burst(),
                    Step::HoldDone => self.start_burst(),
                    Step::BurstDone => {
                        self.input_ready = true;
                        self.update_highlight(Some(self.selected));
                    }
                    Step::ExitBurstDone => self.finish_close(),
                }
            }
        }
    }

    fn set_highlight(&mut self, index: Option<usize>, on: bool) {
        let Some(i) = index.filter(|&i| i < COUNT) else { return };
        let target = if on { 1.0 } else { 0.0 };
        let (scale, border) = (&self.highlight_scale[i], &self.highlight_border[i]);
        self.animations.animate(scale.get(), target, cfg::BUTTON_SCALE_MS, Easing::EaseOutCubic, writer(scale), None, owner_scale(i));
        self.animations.animate(border.get(), target, cfg::BUTTON_BORDER_MS, Easing::EaseOutCubic, writer(border), None, owner_border(i));
    }

    fn update_highlight(&mut self, index: Option<usize>) {
        let on = index.is_some_and(|i| i == self.selected || Some(i) == self.hovered);
        self.set_highlight(index, on);
    }

    fn select(&mut self, index: usize) {
        let old = self.selected;
        self.selected = index;
        self.update_highlight(Some(old));
        self.update_highlight(Some(index));
    }

    fn cancel_sequence(&mut self) {
        for owner in [OWNER_LOGO, OWNER_CLOSE_CHAIN, OWNER_EXIT, OWNER_BURST, OWNER_HOLD, OWNER_CHARGE] {
            self.animations.cancel_owner(owner);
        }
        for i in 0..COUNT {
            self.animations.cancel_owner(owner_slash(i));
            self.animations.cancel_owner(owner_push(i));
            self.animations.cancel_owner(owner_gate(i));
        }
    }

    fn finish_close(&mut self) {
        self.open = false;
        if let Some(on_closed) = &mut self.on_closed {
            on_closed();
        }
    }

    fn start_burst(&mut self) {
        self.burst.set(0.0);
        let done = self.then(Step::BurstDone);
        self.animations.animate(0.0, 1.0, cfg::BURST_MS, Easing::EaseOutCubic, writer(&self.burst), done, OWNER_BURST);
        for i in 0..COUNT {
            self.animations.animate(0.0, 1.0, cfg::PUSH_MS, Easing::EaseOutBack, writer(&self.travel[i]), None, owner_push(i));
        }
    }

    fn start_exit_burst(&mut self) {
        self.burst.set(0.0);
        self.exit_fade.set(1.0);
        let done = self.then(Step::ExitBurstDone);
        self.animations.animate(0.0, 1.0, cfg::BURST_MS, Easing::EaseOutCubic, writer(&self.burst), done, OWNER_BURST);
        self.animations.animate(1.0, 0.0, cfg::EXIT_FADE_MS, Easing::EaseOutCubic, writer(&self.exit_fade), None, OWNER_EXIT);
        for i in 0..COUNT {
            let travel = &self.travel[i];
            self.animations.animate(travel.get(), 1.0 + cfg::EXIT_SPREAD, cfg::EXIT_FADE_MS, Easing::EaseOutCubic, writer(travel), None, owner_push(i));
        }
    }

    fn start_slashes(&mut self) {
        self.animations.animate(0.0, 1.0, cfg::CHARGE_MS, Easing::Linear, writer(&self.charge), None, OWNER_CHARGE);
        let step = cfg::SLASH_MS * cfg::SLASH_ADVANCE_FRAC;
        for e in 0..COUNT {
            self.schedule_after(e as f32 * step, owner_gate(e), Step::Gate(e));
        }
        let slash_time = (COUNT - 1) as f32 * step + cfg::SLASH_MS;
        self.schedule_after(slash_time + cfg::HOLD_MS, OWNER_HOLD, Step::HoldDone);
    }

    fn start_open(&mut self) {
        self.cancel_sequence();
        self.exiting = false;
        self.input_ready = false;
        for v in [&self.logo_scale, &self.exit_fade, &self.burst, &self.charge] {
            v.set(0.0);
        }
        self.burst_started = Instant::now();
        self.slash.iter().chain(&self.travel).for_each(|v| v.set(0.0));
        let done = self.then(Step::LogoDone);
        self.animations.animate(0.0, 1.0, cfg::LOGO_ANIM_MS, Easing::EaseOutBack, writer(&self.logo_scale), done, OWNER_LOGO);
    }

    fn start_close(&mut self) {
        self.cancel_sequence();
        self.input_ready = false;
        self.exiting = true;
        self.charge.set(0.0);
        self.burst.set(0.0);
        self.exit_fade.set(1.0);
        self.burst_started = Instant::now();
        let previous = self.hovered.take();
        self.set_highlight(Some(self.selected), false);
        self.set_highlight(previous, false);
        self.slash.iter().for_each(|v| v.set(0.0));
        self.start_slashes();
    }

    /// Stops everything at once and closes.
    pub fn fast_hide(&mut self) {
        self.cancel_sequence();
        for i in 0..COUNT {
            for v in [&self.slash[i], &self.travel[i], &self.highlight_scale[i], &self.highlight_border[i]] {
                v.set(0.0);
            }
            self.animations.cancel_owner(owner_scale(i));
            self.animations.cancel_owner(owner_border(i));
        }
        self.hovered = None;
        for v in [&self.logo_scale, &self.exit_fade, &self.burst, &self.charge] {
            v.set(0.0);
        }
        self.exiting = false;
        self.input_ready = false;
        self.steps.borrow_mut().clear();
        self.finish_close();
    }

    pub fn open(&mut self) {
        self.selected = 0;
        self.open = true;
        self.start_open();
        self.run_steps();
    }

    /// Opens a closed card, else plays the exit.
    pub fn toggle(&mut self) {
        if self.open {
            self.start_close();
            self.run_steps();
        } else {
            self.open();
        }
    }

    fn execute(&mut self, index: usize) {
        let command = self.commands[index].clone();
        if !command.is_empty()
            && let Some(on_execute) = &mut self.on_execute
        {
            on_execute(&command);
        }
        self.selected = 0;
        self.fast_hide();
    }

    /// A key once the choreography is done; true when it was used.
    pub fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.input_ready {
            return false;
        }
        let used = match event.kind {
            KeyKind::Left => {
                self.select((self.selected + COUNT - 1) % COUNT);
                true
            }
            KeyKind::Right => {
                self.select((self.selected + 1) % COUNT);
                true
            }
            KeyKind::Text => match event.text.as_bytes() {
                [d @ b'1'..=b'9'] if usize::from(d - b'1') < COUNT => {
                    self.select(usize::from(d - b'1'));
                    true
                }
                _ => false,
            },
            KeyKind::Enter => {
                self.execute(self.selected);
                true
            }
            KeyKind::Escape => {
                self.toggle();
                true
            }
            _ => false,
        };
        self.run_steps();
        used
    }

    /// A click on a button runs it once input is ready; a click anywhere else closes.
    pub fn click(&mut self, x: f64, y: f64, width: f64, height: f64) {
        if let Some(button) = button_at(Point { x, y }, Point { x: width / 2.0, y: height / 2.0 }) {
            if self.input_ready {
                self.execute(button);
            }
            return;
        }
        self.toggle();
    }

    /// True when the hovered button changed.
    pub fn hover(&mut self, x: f64, y: f64, width: f64, height: f64) -> bool {
        if !self.input_ready {
            return false;
        }
        let found = button_at(Point { x, y }, Point { x: width / 2.0, y: height / 2.0 });
        if found == self.hovered {
            return false;
        }
        let old = std::mem::replace(&mut self.hovered, found);
        self.update_highlight(old);
        self.update_highlight(found);
        true
    }

    pub fn clear_hover(&mut self) -> bool {
        let Some(old) = self.hovered.take() else { return false };
        self.update_highlight(Some(old));
        true
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.run_steps();
    }

    pub fn open_state(&self) -> bool {
        self.open
    }

    pub fn exiting(&self) -> bool {
        self.exiting
    }

    #[cfg(test)]
    pub fn input_ready(&self) -> bool {
        self.input_ready
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    #[cfg(test)]
    pub fn selected(&self) -> usize {
        self.selected
    }

    pub fn hovered(&self) -> Option<usize> {
        self.hovered
    }

    pub fn logo_scale(&self) -> f32 {
        self.logo_scale.get()
    }

    pub fn exit_fade(&self) -> f32 {
        self.exit_fade.get()
    }

    pub fn burst(&self) -> f32 {
        self.burst.get()
    }

    pub fn scrim(&self) -> f32 {
        cfg::SCRIM_PEAK * self.charge.get() * (1.0 - (self.burst.get() / cfg::SCRIM_OUT_FRAC).min(1.0))
    }

    pub fn flash(&self) -> f32 {
        let burst = self.burst.get();
        if burst > 0.0 { cfg::FLASH_PEAK * (1.0 - (burst / cfg::FLASH_FRAC).min(1.0)) } else { 0.0 }
    }

    pub fn slash(&self, i: usize) -> f32 {
        self.slash[i].get()
    }

    pub fn travel(&self, i: usize) -> f32 {
        self.travel[i].get()
    }

    pub fn highlight_scale(&self, i: usize) -> f32 {
        self.highlight_scale[i].get()
    }

    pub fn highlight_border(&self, i: usize) -> f32 {
        self.highlight_border[i].get()
    }

    pub fn burst_started(&self) -> Instant {
        self.burst_started
    }
}
