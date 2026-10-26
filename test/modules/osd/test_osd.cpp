#include <chrono>
#include <cmath>

#include "render/icons.h"

#include "core/animation.h"

#include "modules/osd/model.h"
#include "modules/osd/view.h"

#include "check.h"
#include "render/recording_canvas.h"

namespace {

using astralia::osd::Model;
using astralia::osd::OsdKind;
using namespace std::chrono_literals;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

void check_osd_model() {
    auto t0 = astralia::osd::Model::Clock::now();
    astralia::osd::Model model(t0);
    test::check(!model.show(astralia::osd::OsdKind::volume, 40, false, t0 + 500ms) &&
                    !model.visible(),
                "show is ignored during the ready delay");
    {
        Instant instant;
        test::check(model.show(astralia::osd::OsdKind::volume, 140, false, t0 + 1000ms),
                    "show works once ready");
        test::check(model.visible() && model.percent() == 100 && model.opacity() == 1.0f,
                    "percent is clamped and the card is opaque");
        test::check(model.hide_at() == t0 + 3000ms, "hides two seconds after the last change");
        test::check(model.until_hide(t0 + 2000ms) == 1000ms && model.until_hide(t0 + 4000ms) == 0ms,
                    "until_hide counts down and floors at zero");
        test::check(model.label() == "100%", "label is the percent");

        model.show(astralia::osd::OsdKind::volume, 30, true, t0 + 1200ms);
        test::check(model.label() == "muted" && model.muted() && model.icon_mix() == 1.0f &&
                        model.bar_fill() == 0.3f,
                    "muted label and icon mix");

        model.tick(t0 + 1500ms);
        test::check(model.visible(), "stays visible before the deadline");
        model.tick(t0 + 3300ms);
        test::check(!model.visible() && model.opacity() == 0.0f, "hides after the deadline");
        test::check(model.until_hide(t0 + 3400ms) == std::chrono::hours(1),
                    "idle model never wakes the loop");
    }

    astralia::osd::Model animated(t0);
    animated.show(astralia::osd::OsdKind::brightness, 20, false, t0 + 1000ms);
    test::check(animated.animating() && animated.opacity() == 0.0f,
                "fade in starts from transparent");
    animated.tick(astralia::osd::Model::Clock::now() + 1s);
    test::check(animated.opacity() == 1.0f && !animated.animating(), "fade in finishes");
    animated.hide();
    animated.hide();
    animated.tick(astralia::osd::Model::Clock::now() + 2s);
    test::check(!animated.visible() && !animated.animating(), "double hide completes once");
    test::check(animated.show(astralia::osd::OsdKind::brightness, 20, false, t0 + 1000ms) &&
                    animated.visible(),
                "a hidden card shows again");
}

void check_osd_glyphs() {
    namespace icon = astralia::icon;
    astralia::osd::Model model(astralia::osd::Model::Clock::now() - 10s);
    auto now = astralia::osd::Model::Clock::now();
    Instant instant;
    model.show(astralia::osd::OsdKind::volume, 0, false, now);
    test::check(model.glyph() == icon::volume_empty, "zero volume");
    model.show(astralia::osd::OsdKind::volume, 30, false, now);
    test::check(model.glyph() == icon::volume_low, "low volume");
    model.show(astralia::osd::OsdKind::volume, 80, false, now);
    test::check(model.glyph() == icon::volume_high, "high volume");
    model.show(astralia::osd::OsdKind::volume, 80, true, now);
    test::check(model.glyph() == icon::volume_mute, "muted volume");
    model.show(astralia::osd::OsdKind::mic, 80, true, now);
    test::check(model.glyph() == icon::mic_off, "muted mic");
    model.show(astralia::osd::OsdKind::mic, 80, false, now);
    test::check(model.glyph() == icon::mic_on, "live mic");
    model.show(astralia::osd::OsdKind::brightness, 20, false, now);
    test::check(model.glyph() == icon::brightness_down, "dim brightness");
    model.show(astralia::osd::OsdKind::brightness, 90, false, now);
    test::check(model.glyph() == icon::brightness_up, "bright brightness");
}

void check_osd_view() {
    using Kind = test::Op::Kind;
    astralia::osd::Model hidden(astralia::osd::Model::Clock::now());
    test::RecordingCanvas blank;
    astralia::osd::paint(blank, hidden);
    test::check(blank.ops.size() == 1 && blank.ops.front().kind == Kind::opacity &&
                    blank.ops.front().value == 0.0f,
                "a transparent card draws nothing");

    Instant instant;
    astralia::osd::Model model(astralia::osd::Model::Clock::now() - 10s);
    model.show(astralia::osd::OsdKind::volume, 50, false, astralia::osd::Model::Clock::now());
    test::RecordingCanvas canvas;
    astralia::osd::paint(canvas, model);
    test::check(canvas.ops.front().kind == Kind::opacity && canvas.ops.front().value == 1.0f,
                "opacity comes first");
    constexpr float shadow = astralia::metrics::shadow_offset;
    test::check(canvas.count(Kind::rounded) == 4, "shadow, card, track and fill");
    const test::Op &card = canvas.ops[2];
    test::check(card.box.w == 300 && card.box.h == 50 - shadow &&
                    card.radius == astralia::metrics::radius_card && card.border_width > 0,
                "card geometry");
    const test::Op &track = canvas.ops[4];
    const test::Op &fill = canvas.ops[5];
    test::check(track.box.h == 6 &&
                    std::fabs(fill.box.w - (track.box.w / 2.0f - astralia::metrics::border_thin)) <
                        1e-3f,
                "fill is half the track at fifty percent, inside the outline");
    test::check(track.box.x == 25 + 18 + 10, "track starts after the icon");
    const test::Op &label = canvas.ops.back();
    test::check(label.kind == Kind::text && label.text == "50%" &&
                    std::fabs(label.box.x + 27 - (300 - 25)) < 1e-3f,
                "label is right aligned");

    model.show(astralia::osd::OsdKind::volume, 0, false, astralia::osd::Model::Clock::now());
    test::RecordingCanvas empty;
    astralia::osd::paint(empty, model);
    test::check(empty.count(Kind::rounded) == 3, "no fill at zero");
}

} // namespace

void check_osd() {
    check_osd_model();
    check_osd_glyphs();
    check_osd_view();
}
