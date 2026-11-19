//! Polkit card state: open and close motion, the prompt, and the password field.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::polkit_config as cfg;
use crate::render::marquee::MarqueeTextState;
use crate::render::text_field::{TextFieldResult, TextFieldState, TextFieldTypeAnim, text_field_handle_key, text_field_secret, text_field_secret_reserve, text_field_type_anim_clear, text_field_type_anim_sync, text_field_wipe};
use crate::service::polkit::PolkitService;

const SCALE_MS: f32 = 350.0;
const OWNER_CARD_SCALE: u64 = 10;
const OWNER_DOTS: u64 = 1000;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Prompt {
    pub message: String,
    pub needs_input: bool,
    pub info: String,
    pub info_is_error: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Key {
    None,
    Changed,
    Submit,
    Cancel,
}

pub fn prompt(service: &PolkitService) -> Prompt {
    let request = service.request();
    Prompt {
        message: if request.message.is_empty() { request.action_id } else { request.message },
        needs_input: service.response_required(),
        info: service.info(),
        info_is_error: service.info_is_error(),
    }
}

/// Holds the password; deliberately neither `Clone` nor `Debug`.
pub struct Model {
    open: bool,
    closing: bool,
    card_scale: Value,
    last_error: bool,
    prompt: Prompt,
    field: TextFieldState,
    dot_anim: TextFieldTypeAnim,
    marquee: MarqueeTextState,
    animations: AnimationManager,
    /// Set by the close tween's completion; `finish_close` runs once it is seen.
    close_done: Rc<Cell<bool>>,
    pub on_closed: Option<Box<dyn FnMut()>>,
}

impl Default for Model {
    fn default() -> Model {
        Model {
            open: false,
            closing: false,
            card_scale: value(0.0),
            last_error: false,
            prompt: Prompt::default(),
            field: TextFieldState { text: text_field_secret(), ..Default::default() },
            dot_anim: TextFieldTypeAnim::default(),
            marquee: MarqueeTextState::new(),
            animations: AnimationManager::default(),
            close_done: Rc::default(),
            on_closed: None,
        }
    }
}

impl Model {
    fn clear_password(&mut self) {
        text_field_wipe(&mut self.field.text);
        text_field_wipe(&mut self.field.preedit);
        self.field.error_message.clear();
        text_field_type_anim_clear(&mut self.dot_anim, &mut self.animations, OWNER_DOTS);
    }

    /// Closes at once without reporting.
    pub fn reset(&mut self) {
        self.animations.cancel_owner(OWNER_CARD_SCALE);
        self.clear_password();
        self.open = false;
        self.closing = false;
        self.last_error = false;
        self.card_scale.set(0.0);
        self.close_done.set(false);
    }

    /// Opens or closes to match `pending`; true when that started a change.
    pub fn sync(&mut self, pending: bool) -> bool {
        if pending && (!self.open || self.closing) {
            self.begin_open();
            return true;
        }
        if !pending && self.open && !self.closing {
            self.begin_close();
            return true;
        }
        false
    }

    fn begin_open(&mut self) {
        self.open = true;
        self.closing = false;
        self.last_error = false;
        self.close_done.set(false);
        self.animations.animate(self.card_scale.get(), 1.0, SCALE_MS, Easing::EaseOutBack, writer(&self.card_scale), None, OWNER_CARD_SCALE);
    }

    fn begin_close(&mut self) {
        self.clear_password();
        self.last_error = false;
        self.closing = true;
        let done = self.close_done.clone();
        let on_done = Box::new(move |_: &mut AnimationManager| done.set(true));
        self.animations.animate(self.card_scale.get(), 0.0, SCALE_MS, Easing::EaseInBack, writer(&self.card_scale), Some(on_done), OWNER_CARD_SCALE);
        self.finish_close();
    }

    fn finish_close(&mut self) {
        if !self.close_done.replace(false) {
            return;
        }
        self.open = false;
        self.closing = false;
        if let Some(on_closed) = &mut self.on_closed {
            on_closed();
        }
    }

    pub fn set_prompt(&mut self, prompt: Prompt) {
        if prompt.info_is_error && !self.last_error {
            self.field.error_message = cfg::ERROR_TEXT.into();
        }
        self.last_error = prompt.info_is_error;
        self.prompt = prompt;
    }

    pub fn key(&mut self, event: &KeyEvent) -> Key {
        if event.kind == KeyKind::Text {
            text_field_secret_reserve(&mut self.field.text, event.text.len());
        }
        match text_field_handle_key(&mut self.field, event) {
            TextFieldResult::Changed => {
                text_field_type_anim_sync(&mut self.dot_anim, &mut self.animations, OWNER_DOTS, &self.field.text);
                Key::Changed
            }
            TextFieldResult::Committed if self.field.text.is_empty() => Key::None,
            TextFieldResult::Committed => Key::Submit,
            TextFieldResult::Cancelled => Key::Cancel,
            TextFieldResult::None => Key::None,
        }
    }

    /// Moves the password out, leaving a fresh secret buffer; the caller wipes what it gets.
    pub fn take_password(&mut self) -> String {
        let password = std::mem::replace(&mut self.field.text, text_field_secret());
        text_field_type_anim_clear(&mut self.dot_anim, &mut self.animations, OWNER_DOTS);
        password
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.finish_close();
    }

    pub fn open(&self) -> bool {
        self.open
    }

    pub fn closing(&self) -> bool {
        self.closing
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn card_scale(&self) -> f32 {
        self.card_scale.get()
    }

    pub fn prompt(&self) -> &Prompt {
        &self.prompt
    }

    pub fn password(&self) -> &str {
        &self.field.text
    }

    pub fn error(&self) -> &str {
        &self.field.error_message
    }

    pub fn show_info(&self) -> bool {
        !self.prompt.info.is_empty() && !self.prompt.info_is_error
    }

    pub fn dot_anim(&self) -> &TextFieldTypeAnim {
        &self.dot_anim
    }

    /// The marquee and the manager that drives it, borrowed together for the view.
    pub fn marquee(&mut self) -> (&mut AnimationManager, &mut MarqueeTextState) {
        (&mut self.animations, &mut self.marquee)
    }
}

impl Drop for Model {
    fn drop(&mut self) {
        self.clear_password();
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::base::animation::set_instant;
    use std::time::Duration;

    pub(crate) struct Snap;
    impl Snap {
        pub(crate) fn on() -> Snap {
            set_instant(true);
            Snap
        }
    }
    impl Drop for Snap {
        fn drop(&mut self) {
            set_instant(false);
        }
    }

    pub(crate) fn key(kind: KeyKind, text: &str) -> KeyEvent {
        KeyEvent { kind, text: text.into(), ..Default::default() }
    }

    pub(crate) fn prompt(needs_input: bool, info: &str, is_error: bool) -> Prompt {
        Prompt { message: "Install software".into(), needs_input, info: info.into(), info_is_error: is_error }
    }

    #[test]
    fn check_polkit_model() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let closed = Rc::new(Cell::new(0));
        let c = closed.clone();
        model.on_closed = Some(Box::new(move || c.set(c.get() + 1)));

        assert!(!model.open() && !model.sync(false), "starts closed and ignores a close");
        assert!(model.sync(true) && model.open() && model.card_scale() == 1.0, "opens at full size when instant");
        assert!(!model.sync(true), "a second open is not a change");

        model.set_prompt(prompt(true, "", false));
        assert_eq!(model.key(&key(KeyKind::Enter, "")), Key::None, "enter with no password does nothing");
        assert!(model.key(&key(KeyKind::Text, "h")) == Key::Changed && model.key(&key(KeyKind::Text, "é")) == Key::Changed, "typing changes the field");
        assert!(model.password() == "hé" && model.dot_anim().chars.len() == 2, "two characters and two dots");
        assert!(model.key(&key(KeyKind::Backspace, "")) == Key::Changed && model.password() == "h" && model.dot_anim().chars.len() == 1, "backspace removes a whole character");
        assert_eq!(model.key(&key(KeyKind::Up, "")), Key::None, "arrows are ignored");
        assert_eq!(model.key(&key(KeyKind::Enter, "")), Key::Submit, "enter submits");
        assert!(model.take_password() == "h" && model.password().is_empty() && model.dot_anim().chars.is_empty(), "taking the password empties the field");
        assert_eq!(model.key(&key(KeyKind::Escape, "")), Key::Cancel, "escape cancels");

        model.set_prompt(prompt(true, "Sorry, try again.", true));
        assert!(model.error() == cfg::ERROR_TEXT && !model.show_info(), "a failed attempt shows the error, not the info line");
        model.key(&key(KeyKind::Text, "x"));
        assert!(model.error().is_empty(), "typing clears the error");
        model.set_prompt(prompt(true, "Sorry, try again.", true));
        assert!(model.error().is_empty(), "the same failure does not raise the error twice");
        model.set_prompt(prompt(true, "Touch the sensor", false));
        assert!(model.show_info() && model.error().is_empty(), "plain info is shown");
        model.set_prompt(prompt(true, "Sorry, try again.", true));
        assert_eq!(model.error(), cfg::ERROR_TEXT, "a new failure raises it again");

        assert!(model.sync(false) && !model.open() && closed.get() == 1, "closing reports once");
        assert!(model.password().is_empty() && model.error().is_empty(), "closing wipes the field");

        model.sync(true);
        model.key(&key(KeyKind::Text, "a"));
        model.reset();
        assert!(!model.open() && model.password().is_empty() && model.card_scale() == 0.0 && closed.get() == 1, "reset closes silently");
    }

    #[test]
    fn check_polkit_animation() {
        let mut model = Model::default();
        model.sync(true);
        assert!(model.open() && model.animating() && model.card_scale() == 0.0, "opening animates from zero");
        model.tick(Instant::now() + Duration::from_secs(1));
        assert!(model.card_scale() == 1.0 && !model.animating(), "opening finishes");
        let closed = Rc::new(Cell::new(0));
        let c = closed.clone();
        model.on_closed = Some(Box::new(move || c.set(c.get() + 1)));
        model.sync(false);
        assert!(model.open() && model.closing() && closed.get() == 0, "closing keeps the card until the animation ends");
        assert!(!model.sync(false), "closing twice is not a change");
        model.sync(true);
        assert!(!model.closing(), "pending again during the close reopens");
        model.reset();
    }

    #[test]
    fn polkit_password_wiped() {
        let mut model = Model::default();
        model.sync(true);
        model.key(&key(KeyKind::Text, "secret"));
        let mut taken = model.take_password();
        assert_eq!(taken.capacity(), crate::render::text_field::TEXT_FIELD_SECRET_CAPACITY);
        text_field_wipe(&mut taken);
        assert!(model.password().is_empty() && model.password_capacity() >= crate::render::text_field::TEXT_FIELD_SECRET_CAPACITY, "a fresh secret buffer is left behind");
        model.key(&key(KeyKind::Text, "again"));
        let (ptr, cap) = (model.password().as_ptr(), model.password_capacity());
        model.sync(false);
        // SAFETY: the buffer is still owned by the field (wiping keeps the allocation); it is read, not freed.
        let bytes = unsafe { std::slice::from_raw_parts(ptr, cap) };
        assert!(bytes.iter().all(|&b| b == 0), "closing zeroes the field's buffer");
    }

    impl Model {
        fn password_capacity(&self) -> usize {
            self.field.text.capacity()
        }
    }
}
