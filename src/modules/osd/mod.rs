//! The per-monitor volume, microphone and brightness card.

pub mod model;
pub mod view;
pub mod wayland;

#[cfg(test)]
mod tests {
    use std::time::{Duration, Instant};

    use super::model::{Kind, Model};
    use super::view::paint;
    use crate::base::animation;
    use crate::render::icons;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};
    use crate::render::tokens::metrics;

    fn ms(n: u64) -> Duration {
        Duration::from_millis(n)
    }

    /// Tweens snap while alive.
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

    #[test]
    fn osd_model() {
        let t0 = Instant::now();
        let mut model = Model::new(t0);
        assert!(!model.show(Kind::Volume, 40, false, t0 + ms(500)) && !model.visible(), "show is ignored during the ready delay");
        {
            let _snap = Snap::on();
            assert!(model.show(Kind::Volume, 140, false, t0 + ms(1000)), "show works once ready");
            assert!(model.visible() && model.percent() == 100 && model.opacity() == 1.0, "percent is clamped and the card is opaque");
            assert_eq!(model.hide_at(), t0 + ms(3000), "hides two seconds after the last change");
            assert!(model.until_hide(t0 + ms(2000)) == ms(1000) && model.until_hide(t0 + ms(4000)) == ms(0), "until_hide counts down and floors at zero");
            assert_eq!(model.label(), "100%", "label is the percent");

            model.show(Kind::Volume, 30, true, t0 + ms(1200));
            assert!(
                model.label() == "muted" && model.muted() && model.icon_mix() == 1.0 && model.bar_fill() == 0.3,
                "muted label and icon mix"
            );

            model.tick(t0 + ms(1500));
            assert!(model.visible(), "stays visible before the deadline");
            model.tick(t0 + ms(3300));
            assert!(!model.visible() && model.opacity() == 0.0, "hides after the deadline");
            assert_eq!(model.until_hide(t0 + ms(3400)), Duration::from_secs(3600), "idle model never wakes the loop");
        }

        let mut animated = Model::new(t0);
        animated.show(Kind::Brightness, 20, false, t0 + ms(1000));
        assert!(animated.animating() && animated.opacity() == 0.0, "fade in starts from transparent");
        animated.tick(Instant::now() + Duration::from_secs(1));
        assert!(animated.opacity() == 1.0 && !animated.animating(), "fade in finishes");
        animated.hide();
        animated.hide();
        animated.tick(Instant::now() + Duration::from_secs(2));
        assert!(!animated.visible() && !animated.animating(), "double hide completes once");
        assert!(animated.show(Kind::Brightness, 20, false, t0 + ms(1000)) && animated.visible(), "a hidden card shows again");
    }

    #[test]
    fn osd_glyphs() {
        let now = Instant::now();
        let mut model = Model::new(now - Duration::from_secs(10));
        let _snap = Snap::on();
        let cases = [
            (Kind::Volume, 0, false, icons::VOLUME_EMPTY, "zero volume"),
            (Kind::Volume, 30, false, icons::VOLUME_LOW, "low volume"),
            (Kind::Volume, 80, false, icons::VOLUME_HIGH, "high volume"),
            (Kind::Volume, 80, true, icons::VOLUME_MUTE, "muted volume"),
            (Kind::Mic, 80, true, icons::MIC_OFF, "muted mic"),
            (Kind::Mic, 80, false, icons::MIC_ON, "live mic"),
            (Kind::Brightness, 20, false, icons::BRIGHTNESS_DOWN, "dim brightness"),
            (Kind::Brightness, 90, false, icons::BRIGHTNESS_UP, "bright brightness"),
        ];
        for (kind, percent, muted, glyph, what) in cases {
            model.show(kind, percent, muted, now);
            assert_eq!(model.glyph(), glyph, "{what}");
        }
    }

    #[test]
    fn osd_view() {
        let hidden = Model::new(Instant::now());
        let mut blank = RecordingCanvas::default();
        paint(&mut blank, &hidden);
        assert!(
            blank.ops.len() == 1 && blank.ops[0].kind == OpKind::Opacity && blank.ops[0].value == 0.0,
            "a transparent card draws nothing"
        );

        let _snap = Snap::on();
        let mut model = Model::new(Instant::now() - Duration::from_secs(10));
        model.show(Kind::Volume, 50, false, Instant::now());
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &model);
        assert!(canvas.ops[0].kind == OpKind::Opacity && canvas.ops[0].value == 1.0, "opacity comes first");
        let shadow = metrics::SHADOW_OFFSET;
        assert_eq!(canvas.count(OpKind::Rounded), 4, "shadow, card, track and fill");
        let card = &canvas.ops[2];
        assert!(
            card.rect.w == 300.0 && card.rect.h == 50.0 - shadow && card.radius == metrics::RADIUS_CARD && card.border_width > 0.0,
            "card geometry"
        );
        let (track, fill) = (&canvas.ops[4], &canvas.ops[5]);
        assert!(
            track.rect.h == 6.0 && (fill.rect.w - (track.rect.w / 2.0 - metrics::BORDER_THIN)).abs() < 1e-3,
            "fill is half the track at fifty percent, inside the outline"
        );
        assert_eq!(track.rect.x, 25.0 + 18.0 + 10.0, "track starts after the icon");
        let label = canvas.ops.last().unwrap();
        assert!(
            label.kind == OpKind::Text && label.text == "50%" && (label.rect.x + 27.0 - (300.0 - 25.0)).abs() < 1e-3,
            "label is right aligned"
        );

        model.show(Kind::Volume, 0, false, Instant::now());
        let mut empty = RecordingCanvas::default();
        paint(&mut empty, &model);
        assert_eq!(empty.count(OpKind::Rounded), 3, "no fill at zero");
    }
}
