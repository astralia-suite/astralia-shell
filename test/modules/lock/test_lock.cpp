#include <algorithm>

#include "check.h"
#include "render/recording_canvas.h"

#include "config/lock_config.h"

#include "modules/lock/model.h"
#include "modules/lock/view.h"

namespace {

using astralia::LockKey;
using astralia::LockModel;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

KeyEvent text(const char *value) {
    KeyEvent event;
    event.kind = KeyKind::Text;
    event.text = value;
    return event;
}

KeyEvent kind(KeyKind value) {
    KeyEvent event;
    event.kind = value;
    return event;
}

} // namespace

void check_lock() {
    LockModel model;
    test::check(model.key(kind(KeyKind::Enter)) == LockKey::none, "an empty password does not submit");
    test::check(model.key(text("a")) == LockKey::changed && model.key(text("b")) == LockKey::changed && model.password() == "ab", "typing edits the password");
    test::check(model.key(kind(KeyKind::Backspace)) == LockKey::changed && model.password() == "a", "backspace removes a character");
    test::check(model.key(kind(KeyKind::Escape)) == LockKey::changed && model.password().empty(), "escape clears the password");

    model.key(text("pw"));
    test::check(model.key(kind(KeyKind::Enter)) == LockKey::submit, "enter submits a password");
    std::string password = model.begin_auth();
    uint64_t generation = model.generation();
    test::check(password == "pw" && model.authenticating(), "begin_auth hands over the password");
    test::check(model.key(kind(KeyKind::Enter)) == LockKey::none, "no second submit while authenticating");
    test::check(!model.finish_auth(generation - 1, true) && model.authenticating(), "a stale result is ignored");
    test::check(!model.finish_auth(generation, false) && model.failed() && model.password().empty() && !model.authenticating(), "a wrong password fails and clears");
    test::check(model.until_fail_clear(LockModel::Clock::now()) > LockModel::Clock::duration::zero(), "the failure has a clear deadline");
    test::check(!model.tick(LockModel::Clock::now()) && model.failed(), "the failure holds before its deadline");
    test::check(model.tick(LockModel::Clock::now() + std::chrono::seconds(10)) && !model.failed(), "the failure clears after its deadline");
    model.key(text("x"));
    model.begin_auth();
    test::check(model.finish_auth(model.generation(), true), "a correct password unlocks");
    model.reset();
    test::check(model.password().empty() && !model.authenticating(), "reset clears the state");

    test::RecordingCanvas canvas;
    astralia::LockMotion motion;
    astralia::LockInfo info;
    info.user = "user";
    info.hour = "12";
    info.minute = "34";
    info.date = "FRI 2026-10-09";
    paint_lock(canvas, model, motion, info, 2560.0f, 1440.0f);
    auto has = [&](const char *value) {
        return std::ranges::any_of(canvas.ops, [&](const test::Op &op) { return op.text == value; });
    };
    test::check(has("12") && has("34") && has(info.date.c_str()) && has(kLockPlaceholderText), "the view draws the clock, date and placeholder");
    test::check(model.hits().pill_button.w > 0.0f, "the view reports the submit button");
    model.key(text("z"));
    model.begin_auth();
    model.finish_auth(model.generation(), false);
    canvas.ops.clear();
    paint_lock(canvas, model, motion, info, 2560.0f, 1440.0f);
    test::check(has(kLockFailText), "the view shows the failure text");
}
