//! Lock state: the password field, the authentication generation, the failure timer and the card motion.

use crate::base::animation::AnimationManager;
use crate::base::input::KeyEvent;
use crate::config::lock_config as cfg;
use crate::render::geometry::Rect;
use crate::render::text_field::{TextFieldResult, TextFieldRowSlide, TextFieldState, TextFieldTypeAnim, text_field_handle_key, text_field_row_slide_reset, text_field_secret, text_field_secret_reserve, text_field_type_anim_clear, text_field_type_anim_sync, text_field_wipe};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Key {
    None,
    Changed,
    Submit,
}

/// Click targets the view reported on its last paint.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Hits {
    pub pill_button: Rect,
    pub media_prev: Rect,
    pub media_play: Rect,
    pub media_next: Rect,
}

/// Holds the password; deliberately neither `Clone` nor `Debug`.
pub struct Model {
    field: TextFieldState,
    failed: bool,
    authenticating: bool,
    generation: u64,
    fail_clear_at: Option<Instant>,
    hits: Hits,
}

impl Default for Model {
    fn default() -> Model {
        Model { field: TextFieldState { text: text_field_secret(), ..Default::default() }, failed: false, authenticating: false, generation: 0, fail_clear_at: None, hits: Hits::default() }
    }
}

impl Model {
    pub fn key(&mut self, event: &KeyEvent) -> Key {
        if matches!(event.kind, crate::base::input::KeyKind::Text) {
            text_field_secret_reserve(&mut self.field.text, event.text.len());
        }
        match text_field_handle_key(&mut self.field, event) {
            TextFieldResult::Committed => {
                if self.field.text.is_empty() || self.authenticating {
                    Key::None
                } else {
                    Key::Submit
                }
            }
            TextFieldResult::Cancelled => {
                self.clear_password();
                self.failed = false;
                Key::Changed
            }
            TextFieldResult::Changed => {
                self.failed = false;
                Key::Changed
            }
            TextFieldResult::None => Key::None,
        }
    }

    /// Starts an authentication and returns a copy of the password the caller must wipe.
    /// `None` when the field is empty or an authentication is already running.
    pub fn begin_auth(&mut self) -> Option<String> {
        if self.field.text.is_empty() || self.authenticating {
            return None;
        }
        self.authenticating = true;
        self.failed = false;
        self.generation += 1;
        let mut copy = text_field_secret();
        text_field_secret_reserve(&mut copy, self.field.text.len());
        copy.push_str(&self.field.text);
        Some(copy)
    }

    /// True only for success of the authentication that is running now.
    pub fn finish_auth(&mut self, generation: u64, success: bool) -> bool {
        if generation != self.generation || !self.authenticating {
            return false;
        }
        self.authenticating = false;
        if success {
            return true;
        }
        self.clear_password();
        self.failed = true;
        self.fail_clear_at = Instant::now().checked_add(Duration::from_millis(cfg::TIMER_FAIL_MS as u64));
        false
    }

    /// Clears the failure once its deadline passes; true means redraw.
    pub fn tick(&mut self, now: Instant) -> bool {
        if !self.failed || self.fail_clear_at.is_some_and(|at| now < at) {
            return false;
        }
        self.failed = false;
        true
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn until_fail_clear(&self, now: Instant) -> Duration {
        match self.fail_clear_at {
            Some(at) if self.failed => at.saturating_duration_since(now),
            _ => Duration::ZERO,
        }
    }

    pub fn reset(&mut self) {
        self.clear_password();
        self.failed = false;
        self.authenticating = false;
        self.generation += 1;
        self.hits = Hits::default();
    }

    pub fn password(&self) -> &str {
        &self.field.text
    }

    pub fn failed(&self) -> bool {
        self.failed
    }

    pub fn authenticating(&self) -> bool {
        self.authenticating
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    pub fn hits(&mut self) -> &mut Hits {
        &mut self.hits
    }

    fn clear_password(&mut self) {
        text_field_wipe(&mut self.field.text);
        text_field_wipe(&mut self.field.preedit);
    }

    #[cfg(test)]
    fn field_buffer(&self) -> (*const u8, usize) {
        (self.field.text.as_ptr(), self.field.text.capacity())
    }
}

impl Drop for Model {
    fn drop(&mut self) {
        self.clear_password();
    }
}

/// Per-output card motion: the entry pose and the password dot animations.
pub struct Motion {
    pub animations: AnimationManager,
    pub panel_scale: f32,
    pub panel_rotation: f32,
    pub panel_w: f32,
    pub panel_h: f32,
    pub icon_alpha: f32,
    pub content_alpha: f32,
    pub content_scale: f32,
    pub started: bool,
    pub dots: TextFieldTypeAnim,
    pub row: TextFieldRowSlide,
}

impl Default for Motion {
    fn default() -> Motion {
        Motion {
            animations: AnimationManager::default(),
            panel_scale: 1.0,
            panel_rotation: 0.0,
            panel_w: 0.0,
            panel_h: 0.0,
            icon_alpha: 0.0,
            content_alpha: 1.0,
            content_scale: 1.0,
            started: false,
            dots: TextFieldTypeAnim::default(),
            row: TextFieldRowSlide::default(),
        }
    }
}

fn enter(m: &mut Motion, card_w: f32, card_h: f32) {
    m.started = true;
    m.panel_scale = cfg::SCALE_FULL;
    m.panel_rotation = 0.0;
    m.panel_w = card_w;
    m.panel_h = card_h;
    m.icon_alpha = 0.0;
    m.content_alpha = 1.0;
    m.content_scale = cfg::SCALE_FULL;
}

pub fn motion_fit(motion: &mut Motion, card_w: f32, card_h: f32) {
    if !motion.started {
        enter(motion, card_w, card_h);
    } else if !motion.animations.has_active() {
        motion.panel_w = card_w;
        motion.panel_h = card_h;
    }
}

pub fn motion_type(motion: &mut Motion, password: &str) {
    text_field_type_anim_sync(&mut motion.dots, &mut motion.animations, cfg::OWNER_DOT_BASE, password);
}

pub fn motion_clear_dots(motion: &mut Motion) {
    text_field_type_anim_clear(&mut motion.dots, &mut motion.animations, cfg::OWNER_DOT_BASE);
    text_field_row_slide_reset(&mut motion.row, &mut motion.animations, cfg::OWNER_DOT_ROW_X);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::KeyKind;

    fn text(value: &str) -> KeyEvent {
        KeyEvent { kind: KeyKind::Text, text: value.into(), ..Default::default() }
    }

    fn kind(value: KeyKind) -> KeyEvent {
        KeyEvent { kind: value, ..Default::default() }
    }

    #[test]
    fn check_lock() {
        let mut model = Model::default();
        assert_eq!(model.key(&kind(KeyKind::Enter)), Key::None, "an empty password does not submit");
        assert!(model.key(&text("a")) == Key::Changed && model.key(&text("b")) == Key::Changed && model.password() == "ab", "typing edits the password");
        assert!(model.key(&kind(KeyKind::Backspace)) == Key::Changed && model.password() == "a", "backspace removes a character");
        assert!(model.key(&kind(KeyKind::Escape)) == Key::Changed && model.password().is_empty(), "escape clears the password");

        model.key(&text("pw"));
        assert_eq!(model.key(&kind(KeyKind::Enter)), Key::Submit, "enter submits a password");
        let mut password = model.begin_auth().expect("a typed password starts auth");
        let generation = model.generation();
        assert!(password == "pw" && model.authenticating(), "begin_auth hands over the password");
        text_field_wipe(&mut password);
        assert_eq!(model.key(&kind(KeyKind::Enter)), Key::None, "no second submit while authenticating");
        assert!(!model.finish_auth(generation - 1, true) && model.authenticating(), "a stale result is ignored");
        assert!(!model.finish_auth(generation, false) && model.failed() && model.password().is_empty() && !model.authenticating(), "a wrong password fails and clears");
        assert!(model.until_fail_clear(Instant::now()) > Duration::ZERO, "the failure has a clear deadline");
        assert!(!model.tick(Instant::now()) && model.failed(), "the failure holds before its deadline");
        assert!(model.tick(Instant::now() + Duration::from_secs(10)) && !model.failed(), "the failure clears after its deadline");
        model.key(&text("x"));
        model.begin_auth();
        assert!(model.finish_auth(model.generation(), true), "a correct password unlocks");
        model.reset();
        assert!(model.password().is_empty() && !model.authenticating(), "reset clears the state");

        let mut fresh = Motion::default();
        motion_fit(&mut fresh, 900.0, 600.0);
        assert!(
            !fresh.animations.has_active() && fresh.panel_w == 900.0 && fresh.panel_h == 600.0 && fresh.panel_rotation == 0.0 && fresh.panel_scale == cfg::SCALE_FULL && fresh.content_alpha == 1.0 && fresh.icon_alpha == 0.0,
            "the lock card enters at rest, with no spin"
        );
    }

    #[test]
    fn lock_fails_closed() {
        let mut model = Model::default();
        assert!(model.begin_auth().is_none() && !model.authenticating(), "an empty field never starts auth");
        assert!(!model.finish_auth(model.generation(), true), "success without a pending auth never unlocks");

        model.key(&text("pw"));
        model.begin_auth();
        let generation = model.generation();
        assert!(model.begin_auth().is_none(), "no second auth while one runs");
        assert!(!model.finish_auth(generation + 1, true) && !model.finish_auth(generation - 1, true), "a stale generation never unlocks");

        model.reset();
        assert!(!model.finish_auth(generation, true), "a result from before reset never unlocks");

        model.key(&text("pw"));
        model.begin_auth();
        let generation = model.generation();
        assert!(!model.finish_auth(generation, false));
        assert!(!model.finish_auth(generation, true), "a second result for the same auth never unlocks");

        model.key(&text("pw"));
        model.begin_auth();
        assert_eq!(model.key(&text("x")), Key::Changed);
        assert_eq!(model.key(&kind(KeyKind::Enter)), Key::None, "no submit during auth");
    }

    /// Reads `cap` bytes at `ptr`; the caller guarantees the allocation is still alive.
    unsafe fn all_zero(ptr: *const u8, cap: usize) -> bool {
        unsafe { std::slice::from_raw_parts(ptr, cap) }.iter().all(|&b| b == 0)
    }

    #[test]
    fn password_wiped() {
        let mut model = Model::default();
        let (ptr, cap) = model.field_buffer();
        assert!(cap >= crate::render::text_field::TEXT_FIELD_SECRET_CAPACITY, "the field reserves its secret capacity");

        model.key(&text("secret"));
        model.key(&kind(KeyKind::Escape));
        assert_eq!(model.field_buffer(), (ptr, cap), "typing never reallocated");
        assert!(unsafe { all_zero(ptr, cap) }, "escape wipes");

        model.key(&text("secret"));
        let mut handed = model.begin_auth().unwrap();
        assert!(handed.capacity() >= crate::render::text_field::TEXT_FIELD_SECRET_CAPACITY, "the handed copy has secret capacity");
        text_field_wipe(&mut handed);
        model.finish_auth(model.generation(), false);
        assert!(unsafe { all_zero(ptr, cap) }, "a failed auth wipes");

        model.key(&text("secret"));
        model.reset();
        assert!(unsafe { all_zero(ptr, cap) }, "reset wipes");

        let long = "x".repeat(cap + 10);
        model.key(&text(&long));
        assert!(model.password() == long && model.field_buffer().1 > cap, "a long password grows into a fresh buffer");
    }
}
