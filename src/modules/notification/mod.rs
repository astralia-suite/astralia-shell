//! The per-monitor notification popups.

pub mod model;
pub mod view;
pub mod wayland;

#[cfg(test)]
mod tests {
    use std::time::{Duration, Instant};

    use super::model::*;
    use super::view::paint;
    use crate::base::animation;
    use crate::config::notification_config as cfg;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};
    use crate::render::theme::palette;
    use crate::render::tokens::metrics;
    use crate::service::notification::{NOTIFICATION_URGENCY_CRITICAL, NOTIFICATION_URGENCY_NORMAL, Notification};

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

    fn make(id: u32, app: &str, summary: &str, body: &str, urgency: u8) -> Notification {
        Notification {
            app: app.into(),
            summary: summary.into(),
            body: body.into(),
            urgency,
            timeout: Duration::from_millis(5000),
            ..Notification::new(id, Instant::now())
        }
    }

    fn plain(id: u32, app: &str) -> Notification {
        make(id, app, "", "", NOTIFICATION_URGENCY_NORMAL)
    }

    #[test]
    fn notification_card_height() {
        assert_eq!(card_height(20.0, 0.0, 0.0), 60.0, "card height with the app name only");
        assert_eq!(card_height(20.0, 24.0, 0.0), 94.0, "card height with a title");
        assert_eq!(card_height(20.0, 24.0, 40.0), 144.0, "card height with all fields");
        assert_eq!(card_height(6.0, 0.0, 0.0), 46.0, "the dot sets the minimum header");
    }

    #[test]
    fn notification_instant_sync() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let mut list = vec![make(1, "mail", "Hello", "World", NOTIFICATION_URGENCY_NORMAL)];
        model.sync(&list);
        let e = &model.entries()[0];
        assert!(model.entries().len() == 1 && e.opacity() == 1.0 && e.slide_offset() == 0.0, "an instant entry appears settled");
        assert!(!e.timed && !model.animating(), "static backends keep no countdown");

        list.push(make(2, "chat", "Ping", "", NOTIFICATION_URGENCY_NORMAL));
        model.sync(&list);
        let ids: Vec<u32> = model.entries().iter().map(|e| e.id).collect();
        assert_eq!(ids, [2, 1], "the newest entry comes first");

        list[0].body = "Changed".into();
        model.sync(&list);
        assert!(model.entries()[1].body == "Changed" && model.entries().len() == 2, "same id updates in place");

        list.remove(0);
        model.sync(&list);
        assert!(model.entries().len() == 1 && model.entries()[0].id == 2, "a dismissed entry leaves at once");
        model.sync(&[]);
        assert!(model.entries().is_empty(), "an empty list empties the model");
    }

    #[test]
    fn notification_animated_sync() {
        let mut animated = Model::default();
        animated.sync(&[make(7, "a", "s", "", NOTIFICATION_URGENCY_NORMAL)]);
        let e = &animated.entries()[0];
        assert!(e.timed && e.opacity() == 0.0 && e.slide_offset() == cfg::SLIDE_OFFSET && animated.animating(), "an animated entry slides and fades in");
        animated.tick(Instant::now() + Duration::from_secs(10));
        let e = &animated.entries()[0];
        assert!(e.opacity() == 1.0 && e.slide_offset() == 0.0 && e.progress() == 0.0, "the entry settles and the countdown ends");
        animated.sync(&[]);
        assert!(animated.entries().len() == 1 && animated.entries()[0].exiting, "an animated entry exits before it is removed");
        animated.sync(&[]);
        animated.tick(Instant::now() + Duration::from_secs(20));
        assert!(animated.entries().is_empty(), "the exit finishes");
    }

    #[test]
    fn notification_view_state() {
        let _snap = Snap::on();
        let mut view = ViewState::default();
        assert!(view.local_opacity(5) == 1.0 && !view.fading(5), "unknown ids are fully visible");
        assert!(view.hide_locally(5) && view.fading(5) && view.local_opacity(5) == 0.0, "hiding fades the card out");
        assert!(!view.hide_locally(5), "hiding twice is a no-op");
        assert!(
            view.set_hover(Some(5)) && view.hovered() == Some(5) && !view.set_hover(Some(5)) && view.set_hover(None) && view.hovered().is_none(),
            "hover changes are reported once"
        );
        let mut model = Model::default();
        model.sync(&[plain(9, "a")]);
        view.forget_missing(model.entries());
        assert!(!view.fading(5), "forgotten ids stop fading");
    }

    #[test]
    fn notification_layout() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let mut view = ViewState::default();
        let mut canvas = RecordingCanvas::default();

        let empty = layout_notifications(&mut canvas, &model, &view, 480.0);
        assert!(empty.cards.is_empty() && empty.height == 0.0, "no entries, no layout");

        model.sync(&[plain(1, "a"), make(2, "b", "title", "body", NOTIFICATION_URGENCY_NORMAL)]);
        let layout = layout_notifications(&mut canvas, &model, &view, 480.0);
        assert!(layout.cards.len() == 2 && layout.cards[0].id == 2, "newest card is laid out first");
        let (newest, oldest) = (card_height(20.0, 20.0, 20.0), card_height(20.0, 0.0, 0.0));
        assert!(layout.cards[0].height == newest && layout.cards[1].height == oldest, "heights follow the measured text");
        assert_eq!(layout.height, newest + oldest + cfg::GAP, "stack height adds the gap");
        assert!(layout.cards[0].y == layout.height - newest && layout.cards[1].y == 0.0, "the newest card sits at the bottom of the stack");

        let mut tall = Model::default();
        let five: Vec<Notification> = (1..=5).map(|i| make(i, "x", "x", "y", NOTIFICATION_URGENCY_NORMAL)).collect();
        tall.sync(&five);
        let fit = layout_notifications(&mut canvas, &tall, &view, 480.0);
        assert!(fit.cards.len() < 5 && fit.height <= 480.0 && fit.cards[0].id == 5, "older cards beyond the limit are dropped");
        let one = layout_notifications(&mut canvas, &tall, &view, 10.0);
        assert!(one.cards.len() == 1 && one.cards[0].id == 5, "the newest card always shows");

        view.hide_locally(2);
        let hidden = layout_notifications(&mut canvas, &model, &view, 480.0);
        assert!(hidden.cards.len() == 1 && hidden.cards[0].id == 1, "a locally closed card takes no space");
    }

    #[test]
    fn notification_close_hit() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let mut view = ViewState::default();
        let mut canvas = RecordingCanvas::default();
        model.sync(&[plain(1, "a"), plain(2, "b")]);
        let layout = layout_notifications(&mut canvas, &model, &view, 480.0);
        let x = f64::from(cfg::CARD_WIDTH - 10.0);
        let y = |i: usize, dy: f32| f64::from(layout.cards[i].y + dy);
        assert_eq!(close_at(&layout, &view, x, y(0, 5.0)), Some(2), "the newest card's close button hits");
        assert_eq!(close_at(&layout, &view, x, y(1, 5.0)), Some(1), "the older card's close button hits");
        assert_eq!(close_at(&layout, &view, 200.0, y(0, 5.0)), None, "the card body hits nothing");
        assert_eq!(close_at(&layout, &view, x, y(0, cfg::CLOSE_HIT + 1.0)), None, "below the button hits nothing");
        assert_eq!(close_boxes(&layout, &view).len(), 2, "one box per live card");
        view.hide_locally(2);
        assert_eq!(close_at(&layout, &view, x, y(0, 5.0)), None, "a fading card cannot be closed again");
    }

    #[test]
    fn notification_card_click() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let mut view = ViewState::default();
        let mut canvas = RecordingCanvas::default();
        let mut portal = make(2, "Network", "Captive Portal", "", NOTIFICATION_URGENCY_NORMAL);
        portal.click_command = "open".into();
        model.sync(&[plain(1, "mail"), portal]);
        let layout = layout_notifications(&mut canvas, &model, &view, 480.0);
        let close = close_boxes(&layout, &view);
        let cards = card_boxes(&layout, &view);
        assert!(cards.len() == 1 && cards[0].0 == 2, "only a card with a command opens");
        let y = |i: usize| f64::from(layout.cards[i].y + 5.0);
        assert_eq!(box_at(&cards, 200.0, y(0)), Some(2), "the card body hits the clickable card");
        assert_eq!(box_at(&cards, 200.0, y(1)), None, "a plain card stays click-through");
        assert_eq!(box_at(&input_boxes(&layout, &view), 200.0, y(0)), Some(2), "the input region covers a clickable card");
        assert_eq!(box_at(&input_boxes(&layout, &view), 200.0, y(1)), None, "the input region keeps a plain card to its close button");

        let close_x = f64::from(cfg::CARD_WIDTH - 10.0);
        assert!(view.set_pointer(&close, &cards, close_x, y(0)) && view.hovered() == Some(2), "the close button wins over the card");
        assert!(
            view.set_pointer(&close, &cards, 200.0, y(0)) && view.hovered().is_none() && view.wants_hand(),
            "the card shows a hand without lighting the close glyph"
        );
        assert!(!view.set_pointer(&close, &cards, 201.0, y(0)), "moving within a card is quiet");
        assert!(view.clear_pointer() && !view.wants_hand(), "leaving clears the hand");
        assert_eq!(view.press(&close, &cards, 200.0, y(0)), Press::Open(2), "a press on the card opens it");
        assert_eq!(view.press(&close, &cards, close_x, y(0)), Press::Closed, "a press on the close button hides it");

        assert!(card_boxes(&layout, &view).is_empty(), "a fading card cannot open");
    }

    #[test]
    fn notification_paint() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let view = ViewState::default();
        let mut canvas = RecordingCanvas::default();
        model.sync(&[make(1, "mail", "Hello", "World", NOTIFICATION_URGENCY_CRITICAL)]);
        let layout = layout_notifications(&mut canvas, &model, &view, 480.0);
        canvas.ops.clear();
        paint(&mut canvas, &model, &view, &layout);
        let shadow = metrics::SHADOW_OFFSET;
        assert_eq!(canvas.count(OpKind::Rounded), 7, "shadow, card, its ring, app tag with its shadow and ring, urgency dot");
        assert_eq!(canvas.count(OpKind::Text), 4, "app, title, body and the close glyph");
        assert_eq!(canvas.count(OpKind::BeginGroup), 0, "no countdown group on a static backend");
        assert!(canvas.ops[1].rect.w == cfg::CARD_WIDTH && canvas.ops[1].rect.h == layout.cards[0].height - shadow, "the card spans the stack width");
        let p = palette();
        assert!(
            canvas.ops[5].color.r == p.critical.r && canvas.ops[5].color.g == p.critical.g && canvas.ops[2].color.r == p.critical_container.r,
            "a critical card uses the critical colour"
        );
    }

    #[test]
    fn notification_paint_animated() {
        let view = ViewState::default();
        let mut canvas = RecordingCanvas::default();
        let mut animated = Model::default();
        animated.sync(&[plain(1, "mail")]);
        animated.tick(Instant::now() + Duration::from_millis(300));
        let layout = layout_notifications(&mut canvas, &animated, &view, 480.0);
        canvas.ops.clear();
        paint(&mut canvas, &animated, &view, &layout);
        assert!(canvas.count(OpKind::BeginGroup) == 2 && canvas.count(OpKind::EndGroup) == 2, "animated cards draw a countdown track and fill");
    }

    #[test]
    fn notification_cap() {
        let _snap = Snap::on();
        let mut model = Model::default();
        let flood: Vec<Notification> = (1..=50).map(|i| make(i, "spam", "x", "y", NOTIFICATION_URGENCY_NORMAL)).collect();
        model.sync(&flood);
        assert_eq!(model.entries().len(), 50, "every notification is kept while it lives");
        let mut canvas = RecordingCanvas::default();
        let layout = layout_notifications(&mut canvas, &model, &ViewState::default(), cfg::MAX_STACK_HEIGHT);
        assert!(layout.height <= cfg::MAX_STACK_HEIGHT && layout.cards[0].id == 50, "the stack keeps to its height, newest first");
        assert_eq!(input_boxes(&layout, &ViewState::default()).len(), layout.cards.len(), "input covers only the cards drawn");
        model.sync(&flood[45..]);
        assert_eq!(model.entries().len(), 5, "expired ones leave at once on a static backend");
    }
}
