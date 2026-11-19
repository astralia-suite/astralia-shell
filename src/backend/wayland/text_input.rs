//! IME text through text-input-v3: the focused text field gets the input method's commits, preedit
//! and deletions, and tells it where its caret is.

use smithay_client_toolkit::reexports::protocols::wp::text_input::zv3::client::zwp_text_input_manager_v3::ZwpTextInputManagerV3;
use smithay_client_toolkit::reexports::protocols::wp::text_input::zv3::client::zwp_text_input_v3::{ChangeCause, ContentHint, ContentPurpose, ZwpTextInputV3};
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_seat::WlSeat;
use wayland_client::protocol::wl_surface::WlSurface;

use super::State;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum TextInputPurpose {
    #[default]
    Normal,
    Password,
}

/// What the focused field tells the input method: its caret rectangle in surface coordinates.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextInputState {
    pub cursor_rect_x: i32,
    pub cursor_rect_y: i32,
    pub cursor_rect_w: i32,
    pub cursor_rect_h: i32,
    pub purpose: TextInputPurpose,
}

impl Default for TextInputState {
    fn default() -> Self {
        TextInputState { cursor_rect_x: 0, cursor_rect_y: 0, cursor_rect_w: 1, cursor_rect_h: 1, purpose: TextInputPurpose::Normal }
    }
}

/// One `done`'s worth of input-method changes.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TextInputEdit {
    pub commit_text: Option<String>,
    pub preedit_text: Option<String>,
    /// Bytes before and after the caret.
    pub delete: Option<(u32, u32)>,
}

/// A text field an edit applies to.
pub trait EditTarget {
    fn delete_before(&mut self, n: u32);
    fn commit_text(&mut self, text: &str);
    fn set_preedit(&mut self, text: &str);
}

impl TextInputEdit {
    /// The protocol's order: the deletion, then the commit, then the new preedit.
    pub fn apply(&self, target: &mut impl EditTarget) {
        if let Some((before, _)) = self.delete {
            target.delete_before(before);
        }
        if let Some(text) = &self.commit_text {
            target.commit_text(text);
        }
        if let Some(text) = &self.preedit_text {
            target.set_preedit(text);
        }
    }
}

/// The changes received since the last `done`.
#[derive(Default)]
pub struct PendingEdit(TextInputEdit);

impl PendingEdit {
    pub fn preedit(&mut self, text: &str) {
        self.0.preedit_text = Some(text.to_string());
    }

    pub fn commit(&mut self, text: &str) {
        self.0.commit_text = Some(text.to_string());
    }

    pub fn delete(&mut self, before: u32, after: u32) {
        self.0.delete = Some((before, after));
    }

    pub fn take(&mut self) -> TextInputEdit {
        std::mem::take(&mut self.0)
    }
}

/// The seat's text-input object and which surface's field it serves.
#[derive(Default)]
pub struct TextInput {
    object: Option<ZwpTextInputV3>,
    entered: Option<WlSurface>,
    focus: Option<WlSurface>,
    /// The focused field's surface and caret.
    active: Option<(WlSurface, TextInputState)>,
    pending: PendingEdit,
    commit_serial: u32,
    enabled: bool,
}

impl TextInput {
    pub fn bind(&mut self, manager: &ZwpTextInputManagerV3, seat: &WlSeat, qh: &QueueHandle<State>) {
        if self.object.is_some() {
            return;
        }
        self.object = Some(manager.get_text_input(seat, qh, ()));
        ::log::info!("text-input: text-input-v3 bound");
    }

    fn accepts(&self) -> bool {
        self.active.as_ref().is_some_and(|(s, _)| self.entered.as_ref() == Some(s) || self.focus.as_ref() == Some(s))
    }

    /// The field on `surface` takes the input method, with its caret at `state`.
    pub fn set_focused(&mut self, surface: &WlSurface, state: TextInputState) {
        if self.object.is_none() {
            return;
        }
        if self.active.as_ref().is_some_and(|(s, _)| s == surface) {
            self.active = Some((surface.clone(), state));
            if self.enabled { self.commit_state(false) } else { self.enable() }
            return;
        }
        self.disable();
        self.active = Some((surface.clone(), state));
        self.enable();
    }

    /// Drops the field on `surface`; true when it was the active one, so its preedit clears.
    pub fn clear_focused(&mut self, surface: &WlSurface) -> bool {
        if !self.active.as_ref().is_some_and(|(s, _)| s == surface) {
            return false;
        }
        self.disable();
        self.active = None;
        self.pending = PendingEdit::default();
        true
    }

    pub fn keyboard_focus(&mut self, surface: &WlSurface, entered: bool) {
        if entered {
            self.focus = Some(surface.clone());
        } else if self.focus.as_ref() == Some(surface) {
            self.focus = None;
        }
        if self.active.is_some() && entered && self.accepts() {
            self.enable();
        }
    }

    pub fn enter(&mut self, surface: &WlSurface) {
        self.entered = Some(surface.clone());
        self.pending = PendingEdit::default();
        if self.active.as_ref().is_some_and(|(s, _)| s == surface) {
            self.enable();
        }
    }

    /// The input method left `surface`; true when the active field's preedit should clear.
    pub fn leave(&mut self, surface: &WlSurface) -> bool {
        if self.entered.as_ref() != Some(surface) {
            return false;
        }
        self.entered = None;
        self.enabled = false;
        self.pending = PendingEdit::default();
        self.active.as_ref().is_some_and(|(s, _)| s == surface)
    }

    pub fn pending(&mut self) -> &mut PendingEdit {
        &mut self.pending
    }

    /// A `done`: the edit for the active field's surface, if any; the caller applies it, then
    /// calls `commit_after_done` with the field's new caret.
    pub fn done(&mut self, serial: u32) -> Option<(WlSurface, TextInputEdit, bool)> {
        let edit = self.pending.take();
        let (surface, _) = self.active.as_ref()?;
        Some((surface.clone(), edit, serial == self.commit_serial))
    }

    /// Sends the field's new caret after a `done` whose serial matched our last commit.
    pub fn commit_after_done(&mut self, state: TextInputState) {
        if let Some((_, s)) = &mut self.active {
            *s = state;
        }
        self.commit_state(true);
    }

    fn enable(&mut self) {
        let Some(object) = &self.object else { return };
        if self.active.is_none() || !self.accepts() {
            return;
        }
        if !self.enabled {
            object.enable();
            self.enabled = true;
        }
        self.commit_state(false);
    }

    fn disable(&mut self) {
        if let Some(object) = self.object.as_ref().filter(|_| self.enabled)
            && self.accepts()
        {
            object.disable();
            object.commit();
            self.commit_serial = self.commit_serial.wrapping_add(1);
            if let Some((surface, _)) = &self.active {
                surface.commit();
            }
        }
        self.enabled = false;
    }

    fn commit_state(&mut self, from_input_method: bool) {
        let Some(object) = &self.object else { return };
        let Some((surface, state)) = &self.active else { return };
        if !self.enabled || !self.accepts() {
            return;
        }
        let purpose = if state.purpose == TextInputPurpose::Password { ContentPurpose::Password } else { ContentPurpose::Normal };
        object.set_content_type(ContentHint::None, purpose);
        object.set_text_change_cause(if from_input_method { ChangeCause::InputMethod } else { ChangeCause::Other });
        object.set_cursor_rectangle(state.cursor_rect_x, state.cursor_rect_y, state.cursor_rect_w.max(1), state.cursor_rect_h.max(1));
        object.commit();
        self.commit_serial = self.commit_serial.wrapping_add(1);
        surface.commit();
    }

    pub fn destroy(&mut self) {
        if let Some(object) = self.object.take() {
            object.destroy();
        }
        *self = TextInput::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Default)]
    struct Log(Vec<String>);

    impl EditTarget for Log {
        fn delete_before(&mut self, n: u32) {
            self.0.push(format!("delete {n}"));
        }
        fn commit_text(&mut self, text: &str) {
            self.0.push(format!("commit {text}"));
        }
        fn set_preedit(&mut self, text: &str) {
            self.0.push(format!("preedit {text}"));
        }
    }

    #[test]
    fn text_input_edit_order() {
        let mut pending = PendingEdit::default();
        pending.preedit("か");
        pending.commit("a");
        pending.delete(2, 0);
        let edit = pending.take();
        let mut log = Log::default();
        edit.apply(&mut log);
        assert_eq!(log.0, ["delete 2", "commit a", "preedit か"], "a done applies delete, then commit, then preedit");
        assert_eq!(pending.take(), TextInputEdit::default(), "the pending edit starts over after a done");
        let mut none = Log::default();
        TextInputEdit::default().apply(&mut none);
        assert!(none.0.is_empty(), "an empty edit touches nothing");
    }
}
