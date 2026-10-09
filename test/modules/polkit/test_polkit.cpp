#include <string>

#include "check.h"
#include "render/recording_canvas.h"

#include "config/polkit_config.h"

#include "core/animation.h"

#include "modules/polkit/layout.h"
#include "modules/polkit/view.h"

namespace {

using astralia::PolkitKey;
using astralia::PolkitModel;
using astralia::PolkitPrompt;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

KeyEvent key(KeyKind kind, std::string text = "") {
    KeyEvent event;
    event.kind = kind;
    event.text = std::move(text);
    return event;
}

PolkitPrompt prompt(bool needs_input, std::string info = "", bool is_error = false) {
    return {"Install software", needs_input, std::move(info), is_error};
}

void check_polkit_model() {
    Instant instant;
    PolkitModel model;
    int closed = 0;
    model.on_closed = [&] { ++closed; };

    test::check(!model.open() && !model.sync(false), "starts closed and ignores a close");
    test::check(model.sync(true) && model.open() && model.card_scale() == 1.0f, "opens at full size when instant");
    test::check(!model.sync(true), "a second open is not a change");

    model.set_prompt(prompt(true));
    test::check(model.key(key(KeyKind::Enter)) == PolkitKey::none, "enter with no password does nothing");
    test::check(model.key(key(KeyKind::Text, "h")) == PolkitKey::changed && model.key(key(KeyKind::Text, "é")) == PolkitKey::changed, "typing changes the field");
    test::check(model.password() == "hé" && model.dot_anim().chars.size() == 2, "two characters and two dots");
    test::check(model.key(key(KeyKind::Backspace)) == PolkitKey::changed && model.password() == "h" && model.dot_anim().chars.size() == 1, "backspace removes a whole character");
    test::check(model.key(key(KeyKind::Up)) == PolkitKey::none, "arrows are ignored");
    test::check(model.key(key(KeyKind::Enter)) == PolkitKey::submit, "enter submits");
    test::check(model.take_password() == "h" && model.password().empty() && model.dot_anim().chars.empty(), "taking the password empties the field");
    test::check(model.key(key(KeyKind::Escape)) == PolkitKey::cancel, "escape cancels");

    model.set_prompt(prompt(true, "Sorry, try again.", true));
    test::check(model.error() == astralia::polkit_config::error_text && !model.show_info(), "a failed attempt shows the error, not the info line");
    model.key(key(KeyKind::Text, "x"));
    test::check(model.error().empty(), "typing clears the error");
    model.set_prompt(prompt(true, "Sorry, try again.", true));
    test::check(model.error().empty(), "the same failure does not raise the error twice");
    model.set_prompt(prompt(true, "Touch the sensor"));
    test::check(model.show_info() && model.error().empty(), "plain info is shown");
    model.set_prompt(prompt(true, "Sorry, try again.", true));
    test::check(model.error() == astralia::polkit_config::error_text, "a new failure raises it again");

    test::check(model.sync(false) && !model.open() && closed == 1, "closing reports once");
    test::check(model.password().empty() && model.error().empty(), "closing wipes the field");

    model.sync(true);
    model.key(key(KeyKind::Text, "a"));
    model.reset();
    test::check(!model.open() && model.password().empty() && model.card_scale() == 0.0f && closed == 1, "reset closes silently");
}

void check_polkit_animation() {
    PolkitModel model;
    model.sync(true);
    test::check(model.open() && model.animating() && model.card_scale() == 0.0f, "opening animates from zero");
    model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    test::check(model.card_scale() == 1.0f && !model.animating(), "opening finishes");
    int closed = 0;
    model.on_closed = [&] { ++closed; };
    model.sync(false);
    test::check(model.open() && model.closing() && closed == 0, "closing keeps the card until the animation ends");
    test::check(!model.sync(false), "closing twice is not a change");
    model.sync(true);
    test::check(!model.closing(), "pending again during the close reopens");
    model.reset();
}

void check_polkit_view() {
    using Kind = test::Op::Kind;
    namespace cfg = astralia::polkit_config;
    PolkitModel idle;
    test::RecordingCanvas none;
    astralia::paint_polkit(none, idle, 1920, 1080);
    test::check(none.ops.empty(), "a closed model draws nothing");

    Instant instant;
    PolkitModel model;
    model.sync(true);
    model.set_prompt(prompt(true));
    test::RecordingCanvas canvas;
    astralia::paint_polkit(canvas, model, 1920, 1080);
    test::check(canvas.ops.front().kind == Kind::begin_group && canvas.ops.back().kind == Kind::end_group, "everything sits in one card group");
    const test::Op &group = canvas.ops.front();
    double card_h = astralia::polkit_card_height(false);
    test::check(group.box.w == cfg::card_width && group.box.h == card_h && group.value == 1.0f, "card size and scale");
    test::check(group.box.x == (1920 - cfg::card_width) / 2 && group.box.y == static_cast<float>((1080 - card_h) / 2), "card is centered");
    test::check(canvas.count(Kind::rounded) == 2 && canvas.count(Kind::image) == 0, "card and field, no dots yet");
    bool placeholder = false;
    for (const test::Op &op : canvas.ops) {
        placeholder = placeholder || (op.kind == Kind::text && op.text == cfg::password_placeholder);
    }
    test::check(placeholder, "empty field shows the placeholder");

    for (char c : std::string("abc")) {
        model.key(key(KeyKind::Text, std::string(1, c)));
    }
    test::RecordingCanvas typed;
    astralia::paint_polkit(typed, model, 1920, 1080);
    test::check(typed.count(Kind::image) == 3, "one echo image per character");
    test::check(typed.ops[typed.ops.size() - 2].box.w == cfg::dot_size, "dots have the configured size");

    for (int i = 0; i < 100; ++i) {
        model.key(key(KeyKind::Text, "z"));
    }
    test::RecordingCanvas many;
    astralia::paint_polkit(many, model, 1920, 1080);
    test::check(many.count(Kind::image) == 25, "dots are limited to what fits");

    test::RecordingCanvas fallback;
    fallback.image_id = astralia::ui::no_image;
    astralia::paint_polkit(fallback, model, 1920, 1080);
    test::check(fallback.count(Kind::image) == 0 && fallback.count(Kind::rounded) == 27, "missing echo art falls back to round dots");

    model.set_prompt(prompt(false, "Touch the sensor"));
    test::RecordingCanvas waiting;
    astralia::paint_polkit(waiting, model, 1920, 1080);
    bool authenticating = false;
    bool info = false;
    for (const test::Op &op : waiting.ops) {
        authenticating = authenticating || (op.kind == Kind::text && op.text == cfg::authenticating_text);
        info = info || (op.kind == Kind::text && op.text == "Touch the sensor");
    }
    test::check(authenticating && info && waiting.ops.front().box.h == astralia::polkit_card_height(true), "waiting state shows the status and the info line");
}

} // namespace

void check_polkit() {
    check_polkit_model();
    check_polkit_animation();
    check_polkit_view();
}
