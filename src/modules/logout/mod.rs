//! The logout overlay: a ring of power actions around the logo.

pub mod layout;
pub mod model;
pub mod view;
pub mod wayland;

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    use super::layout::{Point, button_at, button_center};
    use super::model::{Model, commands};
    use super::view::paint;
    use crate::base::animation;
    use crate::base::input::{KeyEvent, KeyKind};
    use crate::config::logout_config as cfg;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    struct Snap;
    impl Snap {
        fn on() -> Snap {
            animation::set_instant(true);
            Snap
        }
    }
    impl Drop for Snap {
        fn drop(&mut self) {
            animation::set_instant(false);
        }
    }

    fn key(kind: KeyKind, text: &str) -> KeyEvent {
        KeyEvent { kind, text: text.into(), ..KeyEvent::default() }
    }

    struct Harness {
        model: Model,
        executed: Rc<RefCell<Vec<String>>>,
        closed: Rc<RefCell<i32>>,
    }

    fn harness() -> Harness {
        let mut model = Model::new(commands());
        let executed = Rc::new(RefCell::new(Vec::new()));
        let closed = Rc::new(RefCell::new(0));
        let (e, c) = (executed.clone(), closed.clone());
        model.on_execute = Some(Box::new(move |command: &str| e.borrow_mut().push(command.to_string())));
        model.on_closed = Some(Box::new(move || *c.borrow_mut() += 1));
        Harness { model, executed, closed }
    }

    #[test]
    fn logout_layout() {
        let center = Point { x: 500.0, y: 400.0 };
        let top = button_center(0, center);
        assert!((top.x - 500.0).abs() < 1e-6 && (top.y - 100.0).abs() < 1e-6, "first button sits straight above the center");
        let right = button_center(2, center);
        assert!((right.x - 800.0).abs() < 1e-6 && (right.y - 400.0).abs() < 1e-6, "third button sits right of the center");
        assert_eq!(button_at(Point { x: 500.0, y: 100.0 }, center), Some(0), "button center hits");
        assert_eq!(button_at(Point { x: 554.0, y: 154.0 }, center), Some(0), "button corner hits");
        assert_eq!(button_at(center, center), None, "logo center hits no button");
    }

    #[test]
    fn logout_commands() {
        let commands = commands();
        assert!(commands[0] == "systemctl poweroff" && commands[1] == "systemctl reboot", "default commands come from the config");
        assert_eq!(commands[cfg::LOCK_INDEX as usize], "astralia lock", "the lock command sits in its slot");
    }

    #[test]
    fn logout_model() {
        let _snap = Snap::on();
        let Harness { model: mut m, executed, closed } = harness();
        assert!(!m.open_state() && !m.key(&key(KeyKind::Right, "")), "closed model ignores keys");

        m.open();
        assert!(m.open_state() && m.input_ready() && !m.exiting() && m.logo_scale() == 1.0 && m.burst() == 1.0, "instant open runs the whole choreography");
        assert!(m.travel(0) == 1.0 && m.travel(7) == 1.0 && m.slash(3) == 1.0, "buttons sit on the ring");
        assert!(m.selected() == 0 && m.highlight_scale(0) == 1.0 && m.highlight_border(0) == 1.0, "the first button starts selected and highlighted");
        assert!(!m.animating(), "nothing is left animating");

        assert!(m.key(&key(KeyKind::Right, "")) && m.selected() == 1 && m.highlight_scale(1) == 1.0 && m.highlight_scale(0) == 0.0, "right moves the highlight");
        assert!(m.key(&key(KeyKind::Left, "")) && m.key(&key(KeyKind::Left, "")) && m.selected() == 7, "left wraps around");
        assert!(m.key(&key(KeyKind::Text, "3")) && m.selected() == 2, "digits select a button");
        assert!(!m.key(&key(KeyKind::Text, "9")) && !m.key(&key(KeyKind::Text, "ab")) && m.selected() == 2, "other text is ignored");

        assert!(!m.hover(0.0, 0.0, 1920.0, 1200.0), "hover on empty space changes nothing");
        let center = Point { x: 960.0, y: 600.0 };
        let first = button_center(0, center);
        assert!(m.hover(first.x, first.y, 1920.0, 1200.0) && m.hovered() == Some(0), "hover finds a button");
        assert!(!m.hover(first.x + 1.0, first.y + 1.0, 1920.0, 1200.0), "same button is not a change");
        assert!(m.clear_hover() && m.hovered().is_none() && !m.clear_hover(), "leaving clears the hover once");

        m.key(&key(KeyKind::Text, "1"));
        assert!(m.key(&key(KeyKind::Enter, "")) && executed.borrow().as_slice() == ["systemctl poweroff"], "enter runs the selected command");
        assert!(!m.open_state() && *closed.borrow() == 1 && m.selected() == 0 && m.travel(0) == 0.0, "executing hides the card and resets");

        m.open();
        m.key(&key(KeyKind::Text, "3"));
        m.key(&key(KeyKind::Enter, ""));
        assert!(executed.borrow().len() == 2 && executed.borrow()[1] == "astralia lock", "the lock button runs the lock command");

        m.open();
        m.key(&key(KeyKind::Text, "5"));
        m.key(&key(KeyKind::Enter, ""));
        assert!(executed.borrow().len() == 2 && !m.open_state() && *closed.borrow() == 3, "a button without a command still closes");

        m.open();
        assert!(m.key(&key(KeyKind::Escape, "")) && !m.open_state() && *closed.borrow() == 4 && m.exit_fade() == 0.0, "escape closes through the exit sequence");

        m.open();
        m.click(10.0, 10.0, 1920.0, 1200.0);
        assert!(!m.open_state() && *closed.borrow() == 5, "a click outside closes");
        m.open();
        let second = button_center(1, center);
        m.click(second.x, second.y, 1920.0, 1200.0);
        assert!(executed.borrow().len() == 3 && executed.borrow()[2] == "systemctl reboot" && !m.open_state(), "a click on a button runs it");
    }

    #[test]
    fn logout_choreography() {
        let Harness { model: mut m, closed, .. } = harness();
        m.open();
        assert!(m.open_state() && !m.input_ready() && m.animating() && m.logo_scale() == 0.0, "animated open starts from nothing");
        assert!(!m.key(&key(KeyKind::Right, "")), "keys wait for the burst to finish");
        let start = Instant::now();
        m.tick(start + Duration::from_millis(700));
        assert_eq!(m.logo_scale(), 1.0, "the logo has scaled in");
        let mut ms = 700;
        while ms < 4000 && !m.input_ready() {
            m.tick(start + Duration::from_millis(ms));
            ms += 50;
        }
        assert!(m.input_ready(), "input opens once the choreography ends");
        m.toggle();
        assert!(m.open_state() && m.exiting() && !m.input_ready(), "closing plays the exit first");
        let mut ms = 0;
        while ms < 4000 && m.open_state() {
            m.tick(Instant::now() + Duration::from_millis(ms + 5000));
            ms += 50;
        }
        assert!(!m.open_state() && *closed.borrow() == 1, "the card closes when the exit ends");
        m.open();
        m.fast_hide();
        assert!(!m.open_state() && !m.animating() && *closed.borrow() == 2, "fast hide stops everything");
    }

    #[test]
    fn logout_view() {
        let idle = harness();
        let mut none = RecordingCanvas::default();
        paint(&mut none, &idle.model, 1920.0, 1200.0, "");
        assert!(none.ops.is_empty(), "a closed card draws nothing");

        let _snap = Snap::on();
        let mut h = harness();
        h.model.open();
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &h.model, 1920.0, 1200.0, "");
        assert!(canvas.count(OpKind::Rounded) == 17 && canvas.count(OpKind::Text) == 8, "eight buttons with a glyph each");
        assert!(canvas.count(OpKind::BeginGroup) == 1 && canvas.count(OpKind::EndGroup) == 1, "one logo group");
        let face = cfg::BUTTON_SIZE as f32;
        let top = &canvas.ops[1];
        assert!(top.rect.w > face && top.rect.w < face * 1.06, "the selected button is enlarged");
        assert_eq!(canvas.ops[5].rect.w, face, "the others keep their size");
        let centered = canvas
            .ops
            .iter()
            .filter(|op| op.kind == OpKind::BeginGroup)
            .any(|op| op.rect.x == 960.0 - cfg::LOGO_SIZE as f32 / 2.0 && op.value == 1.0);
        assert!(centered, "the logo is centered at full scale");
    }
}
