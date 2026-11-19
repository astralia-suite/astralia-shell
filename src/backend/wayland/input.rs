//! Seat input: keysym translation, pointer tracking and the cursor shape.

use smithay_client_toolkit::reexports::protocols::wp::cursor_shape::v1::client::wp_cursor_shape_device_v1::{Shape, WpCursorShapeDeviceV1};
use smithay_client_toolkit::seat::keyboard::{Keysym, Modifiers};
use smithay_client_toolkit::seat::pointer::{BTN_LEFT, BTN_MIDDLE, BTN_RIGHT};
use wayland_client::Proxy;
use wayland_client::backend::ObjectId;
use wayland_client::protocol::wl_surface::WlSurface;

use crate::base::input::{Button, KeyEvent, KeyKind};

/// The keysym without shift: a letter's lowercase form, anything else as is.
fn base_sym(keysym: Keysym) -> u32 {
    keysym
        .key_char()
        .and_then(|c| c.to_lowercase().next())
        .map(Keysym::from_char)
        .filter(|k| *k != Keysym::NoSymbol)
        .unwrap_or(keysym)
        .raw()
}

/// A key press as the UI sees it; `utf8` is the composed text, `None` for modifiers and dead keys mid-compose.
pub fn translate_key(keysym: Keysym, utf8: Option<&str>, mods: Modifiers) -> Option<KeyEvent> {
    let (shift, alt, ctrl) = (mods.shift, mods.alt, mods.ctrl);
    let with_mods = |kind| Some(KeyEvent { kind, shift, alt, ctrl, ..Default::default() });
    let bare = |kind| Some(KeyEvent { kind, ..Default::default() });
    match keysym {
        Keysym::Up => with_mods(KeyKind::Up),
        Keysym::Down => with_mods(KeyKind::Down),
        Keysym::Left => with_mods(KeyKind::Left),
        Keysym::Right => with_mods(KeyKind::Right),
        Keysym::Return | Keysym::KP_Enter => bare(KeyKind::Enter),
        Keysym::Escape => bare(KeyKind::Escape),
        Keysym::BackSpace => bare(KeyKind::Backspace),
        Keysym::Tab => bare(KeyKind::Tab),
        Keysym::Home | Keysym::KP_Home => with_mods(KeyKind::Home),
        Keysym::End | Keysym::KP_End => with_mods(KeyKind::End),
        Keysym::Delete | Keysym::KP_Delete => bare(KeyKind::Delete),
        _ => {
            let text = utf8.filter(|t| !t.is_empty())?;
            Some(KeyEvent { kind: KeyKind::Text, text: text.to_string(), shift, alt, ctrl, base_sym: base_sym(keysym) })
        }
    }
}

pub fn button_from_code(code: u32) -> Button {
    match code {
        BTN_LEFT => Button::Left,
        BTN_MIDDLE => Button::Middle,
        BTN_RIGHT => Button::Right,
        _ => Button::Other,
    }
}

/// Where the pointer is and the serials a popup grab or cursor change needs.
#[derive(Default)]
pub struct Pointer {
    pub focused: Option<WlSurface>,
    pub x: f64,
    pub y: f64,
    pub enter_serial: u32,
    pub button_serial: u32,
    pub shape_device: Option<WpCursorShapeDeviceV1>,
    hand: Option<bool>,
}

impl Pointer {
    pub fn enter(&mut self, surface: &WlSurface, x: f64, y: f64, serial: u32) {
        self.focused = Some(surface.clone());
        (self.x, self.y, self.enter_serial) = (x, y, serial);
        // A new enter resets the cursor to whatever the surface was given before.
        self.hand = None;
    }

    pub fn leave(&mut self, surface: &WlSurface) {
        if self.focused.as_ref() == Some(surface) {
            self.focused = None;
        }
    }

    /// Clears the focus when it is one of `gone`, so late events for a destroyed surface find nothing.
    pub fn forget(&mut self, gone: &[ObjectId]) {
        if self.focused.as_ref().is_some_and(|s| gone.contains(&s.id())) {
            self.focused = None;
            self.hand = None;
        }
    }

    /// The pointing hand over clickable things, the default arrow elsewhere; an unchanged shape is not re-sent.
    pub fn set_cursor(&mut self, hand: bool) {
        if self.hand == Some(hand) {
            return;
        }
        let Some(device) = &self.shape_device else { return };
        device.set_shape(self.enter_serial, if hand { Shape::Pointer } else { Shape::Default });
        self.hand = Some(hand);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use smithay_client_toolkit::seat::pointer::BTN_SIDE;

    fn none() -> Modifiers {
        Modifiers::default()
    }

    #[test]
    fn keyboard_translation() {
        let a = translate_key(Keysym::a, Some("a"), none()).unwrap();
        assert_eq!((a.kind, a.text.as_str(), a.base_sym), (KeyKind::Text, "a", Keysym::a.raw()));
        assert_eq!(translate_key(Keysym::Return, Some("\r"), none()).unwrap().kind, KeyKind::Enter);
        assert_eq!(translate_key(Keysym::KP_Enter, Some("\r"), none()).unwrap().kind, KeyKind::Enter);
        assert_eq!(translate_key(Keysym::Escape, Some("\u{1b}"), none()).unwrap().kind, KeyKind::Escape);
        assert_eq!(translate_key(Keysym::BackSpace, Some("\u{8}"), none()).unwrap().kind, KeyKind::Backspace);
        assert_eq!(translate_key(Keysym::Tab, Some("\t"), none()).unwrap().kind, KeyKind::Tab);
        assert!(translate_key(Keysym::Shift_L, None, none()).is_none(), "a modifier alone is no event");
        let ctrl = Modifiers { ctrl: true, ..none() };
        let ctrl_a = translate_key(Keysym::a, Some("\u{1}"), ctrl).unwrap();
        assert!(ctrl_a.ctrl && !ctrl_a.shift && ctrl_a.base_sym == Keysym::a.raw());
        let shift = Modifiers { shift: true, ..none() };
        let big = translate_key(Keysym::A, Some("A"), shift).unwrap();
        assert!(big.kind == KeyKind::Text && big.text == "A" && big.shift && big.base_sym == Keysym::a.raw(), "base_sym ignores shift");
        let up = translate_key(Keysym::Up, None, shift).unwrap();
        assert!(up.kind == KeyKind::Up && up.shift);
        let home = translate_key(Keysym::KP_Home, None, Modifiers { alt: true, ..none() }).unwrap();
        assert!(home.kind == KeyKind::Home && home.alt);
        assert_eq!(translate_key(Keysym::KP_End, None, none()).unwrap().kind, KeyKind::End);
        let del = translate_key(Keysym::KP_Delete, None, shift).unwrap();
        assert!(del.kind == KeyKind::Delete && !del.shift, "Delete carries no modifiers, as Enter and Escape");
        let composed = translate_key(Keysym::eacute, Some("é"), none()).unwrap();
        assert_eq!((composed.kind, composed.text.as_str()), (KeyKind::Text, "é"));
        assert!(translate_key(Keysym::dead_acute, None, none()).is_none(), "a dead key while composing is no event");
    }

    struct Bare;

    impl wayland_client::Dispatch<wayland_client::protocol::wl_registry::WlRegistry, wayland_client::globals::GlobalListContents> for Bare {
        fn event(_: &mut Self, _: &wayland_client::protocol::wl_registry::WlRegistry, _: wayland_client::protocol::wl_registry::Event, _: &wayland_client::globals::GlobalListContents, _: &wayland_client::Connection, _: &wayland_client::QueueHandle<Self>) {}
    }

    wayland_client::delegate_noop!(Bare: ignore wayland_client::protocol::wl_compositor::WlCompositor);
    wayland_client::delegate_noop!(Bare: ignore WlSurface);

    #[test]
    fn monitor_removal_drops_input_focus() {
        use wayland_client::protocol::wl_compositor::WlCompositor;
        let Ok(conn) = wayland_client::Connection::connect_to_env() else {
            println!("skipped: no wayland display");
            return;
        };
        let (globals, queue) = wayland_client::globals::registry_queue_init::<Bare>(&conn).unwrap();
        let qh = queue.handle();
        let compositor: WlCompositor = globals.bind(&qh, 1..=4, ()).unwrap();
        let (bar, other) = (compositor.create_surface(&qh, ()), compositor.create_surface(&qh, ()));
        let mut pointer = Pointer::default();
        pointer.enter(&bar, 3.0, 4.0, 7);
        pointer.forget(&[other.id()]);
        assert_eq!(pointer.focused.as_ref(), Some(&bar), "another surface going keeps the focus");
        pointer.forget(&[other.id(), bar.id()]);
        assert!(pointer.focused.is_none(), "the focused surface going clears the focus");
        bar.destroy();
        other.destroy();
    }

    #[test]
    fn input_convert() {
        assert_eq!(button_from_code(BTN_LEFT), Button::Left);
        assert_eq!(button_from_code(BTN_MIDDLE), Button::Middle);
        assert_eq!(button_from_code(BTN_RIGHT), Button::Right);
        assert_eq!(button_from_code(BTN_SIDE), Button::Other);
    }
}
