#include <chrono>
#include <cmath>

#include "check.h"
#include "ui/recording_canvas.h"

#include "config/icons.h"

#include "core/animation.h"

#include "modules/osd/model.h"
#include "modules/osd/view.h"

namespace {

using astralia::OsdKind;
using astralia::OsdModel;
using namespace std::chrono_literals;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

void check_osd_model() {
    auto t0 = OsdModel::Clock::now();
    OsdModel model(t0);
    test::check(!model.show(OsdKind::volume, 40, false, t0 + 500ms) && !model.visible(), "show is ignored during the ready delay");
    {
        Instant instant;
        test::check(model.show(OsdKind::volume, 140, false, t0 + 1000ms), "show works once ready");
        test::check(model.visible() && model.percent() == 100 && model.opacity() == 1.0f, "percent is clamped and the card is opaque");
        test::check(model.hide_at() == t0 + 3000ms, "hides two seconds after the last change");
        test::check(model.until_hide(t0 + 2000ms) == 1000ms && model.until_hide(t0 + 4000ms) == 0ms, "until_hide counts down and floors at zero");
        test::check(model.label() == "100%", "label is the percent");

        model.show(OsdKind::volume, 30, true, t0 + 1200ms);
        test::check(model.label() == "muted" && model.muted() && model.icon_mix() == 1.0f && model.bar_fill() == 0.3f, "muted label and icon mix");

        model.tick(t0 + 1500ms);
        test::check(model.visible(), "stays visible before the deadline");
        model.tick(t0 + 3300ms);
        test::check(!model.visible() && model.opacity() == 0.0f, "hides after the deadline");
        test::check(model.until_hide(t0 + 3400ms) == std::chrono::hours(1), "idle model never wakes the loop");
    }

    OsdModel animated(t0);
    animated.show(OsdKind::brightness, 20, false, t0 + 1000ms);
    test::check(animated.animating() && animated.opacity() == 0.0f, "fade in starts from transparent");
    animated.tick(OsdModel::Clock::now() + 1s);
    test::check(animated.opacity() == 1.0f && !animated.animating(), "fade in finishes");
    animated.hide();
    animated.hide();
    animated.tick(OsdModel::Clock::now() + 2s);
    test::check(!animated.visible() && !animated.animating(), "double hide completes once");
    test::check(animated.show(OsdKind::brightness, 20, false, t0 + 1000ms) && animated.visible(), "a hidden card shows again");
}

void check_osd_glyphs() {
    namespace icon = astralia::icon;
    OsdModel model(OsdModel::Clock::now() - 10s);
    auto now = OsdModel::Clock::now();
    Instant instant;
    model.show(OsdKind::volume, 0, false, now);
    test::check(model.glyph() == icon::volume_empty, "zero volume");
    model.show(OsdKind::volume, 30, false, now);
    test::check(model.glyph() == icon::volume_low, "low volume");
    model.show(OsdKind::volume, 80, false, now);
    test::check(model.glyph() == icon::volume_high, "high volume");
    model.show(OsdKind::volume, 80, true, now);
    test::check(model.glyph() == icon::volume_mute, "muted volume");
    model.show(OsdKind::mic, 80, true, now);
    test::check(model.glyph() == icon::mic_off, "muted mic");
    model.show(OsdKind::mic, 80, false, now);
    test::check(model.glyph() == icon::mic_on, "live mic");
    model.show(OsdKind::brightness, 20, false, now);
    test::check(model.glyph() == icon::brightness_down, "dim brightness");
    model.show(OsdKind::brightness, 90, false, now);
    test::check(model.glyph() == icon::brightness_up, "bright brightness");
}

void check_osd_view() {
    using Kind = test::Op::Kind;
    OsdModel hidden(OsdModel::Clock::now());
    test::RecordingCanvas blank;
    astralia::paint_osd(blank, hidden);
    test::check(blank.ops.size() == 1 && blank.ops.front().kind == Kind::opacity && blank.ops.front().value == 0.0f, "a transparent card draws nothing");

    Instant instant;
    OsdModel model(OsdModel::Clock::now() - 10s);
    model.show(OsdKind::volume, 50, false, OsdModel::Clock::now());
    test::RecordingCanvas canvas;
    astralia::paint_osd(canvas, model);
    test::check(canvas.ops.front().kind == Kind::opacity && canvas.ops.front().value == 1.0f, "opacity comes first");
    test::check(canvas.count(Kind::rounded) == 3, "card, track and fill");
    const test::Op &card = canvas.ops[1];
    test::check(card.box.w == 300 && card.box.h == 50 && card.radius == 25 && card.border_width > 0, "card geometry");
    const test::Op &track = canvas.ops[3];
    const test::Op &fill = canvas.ops[4];
    test::check(track.box.h == 6 && std::fabs(fill.box.w - track.box.w / 2.0f) < 1e-3f, "fill is half the track at fifty percent");
    test::check(track.box.x == 25 + 18 + 10, "track starts after the icon");
    const test::Op &label = canvas.ops.back();
    test::check(label.kind == Kind::text && label.text == "50%" && std::fabs(label.box.x + 27 - (300 - 25)) < 1e-3f, "label is right aligned");

    model.show(OsdKind::volume, 0, false, OsdModel::Clock::now());
    test::RecordingCanvas empty;
    astralia::paint_osd(empty, model);
    test::check(empty.count(Kind::rounded) == 2, "no fill at zero");
}

} // namespace

void check_osd() {
    check_osd_model();
    check_osd_glyphs();
    check_osd_view();
}
