//! Keyboard events as the UI sees them, after keymap translation.

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum KeyKind {
    /// Committed text in `KeyEvent::text`.
    #[default]
    Text,
    /// Input-method composition in progress, replaced by the next `Preedit` or `Text`.
    Preedit,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Tab,
    Home,
    End,
    Delete,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct KeyEvent {
    pub kind: KeyKind,
    pub text: String,
    pub shift: bool,
    pub alt: bool,
    pub ctrl: bool,
    /// The keysym without modifiers applied, for shortcuts.
    pub base_sym: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Button {
    #[default]
    Left,
    Middle,
    Right,
    Other,
}

/// The system clipboard as text: `set` copies, `request` hands the pasted text to `done` (empty
/// when there is none or it is not text), at once or once it has arrived.
pub trait Clipboard {
    fn set(&self, text: String);
    fn request(&self, done: Box<dyn FnOnce(String)>);
}
