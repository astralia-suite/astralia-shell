#include <chrono>
#include <string>
#include <vector>

#include "check.h"
#include "render/recording_canvas.h"

#include "config/notification_config.h"

#include "core/animation.h"

#include "modules/notification/model.h"
#include "modules/notification/view.h"

namespace {

namespace cfg = astralia::notification_config;
using astralia::Notification;
using astralia::NotificationLayout;
using astralia::NotificationModel;
using astralia::NotificationViewState;
using namespace std::chrono_literals;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

Notification make(uint32_t id, std::string app, std::string summary = "", std::string body = "", uint8_t urgency = astralia::notification_urgency_normal) {
    Notification n;
    n.id = id;
    n.app = std::move(app);
    n.summary = std::move(summary);
    n.body = std::move(body);
    n.urgency = urgency;
    n.timeout = 5000ms;
    return n;
}

void check_card_height() {
    test::check(astralia::notification_card_height(20.0f, 0.0f, 0.0f) == 60.0f, "card height with the app name only");
    test::check(astralia::notification_card_height(20.0f, 24.0f, 0.0f) == 94.0f, "card height with a title");
    test::check(astralia::notification_card_height(20.0f, 24.0f, 40.0f) == 144.0f, "card height with all fields");
    test::check(astralia::notification_card_height(6.0f, 0.0f, 0.0f) == 46.0f, "the dot sets the minimum header");
}

void check_instant_sync() {
    Instant instant;
    NotificationModel model;
    std::vector<Notification> list{make(1, "mail", "Hello", "World")};
    model.sync(list);
    test::check(model.entries().size() == 1 && model.entries()[0].opacity == 1.0f && model.entries()[0].slide_offset == 0.0f, "an instant entry appears settled");
    test::check(!model.entries()[0].timed && !model.animating(), "static backends keep no countdown");

    list.push_back(make(2, "chat", "Ping"));
    model.sync(list);
    test::check(model.entries().size() == 2 && model.entries()[0].id == 2 && model.entries()[1].id == 1, "the newest entry comes first");

    list[0].body = "Changed";
    model.sync(list);
    test::check(model.entries()[1].body == "Changed" && model.entries().size() == 2, "same id updates in place");

    list.erase(list.begin());
    model.sync(list);
    test::check(model.entries().size() == 1 && model.entries()[0].id == 2, "a dismissed entry leaves at once");
    model.sync({});
    test::check(model.entries().empty(), "an empty list empties the model");
}

void check_animated_sync() {
    NotificationModel animated;
    animated.sync({make(7, "a", "s")});
    test::check(animated.entries()[0].timed && animated.entries()[0].opacity == 0.0f && animated.entries()[0].slide_offset == cfg::slide_offset && animated.animating(), "an animated entry slides and fades in");
    animated.tick(std::chrono::steady_clock::now() + 10s);
    test::check(animated.entries()[0].opacity == 1.0f && animated.entries()[0].slide_offset == 0.0f && animated.entries()[0].progress == 0.0f, "the entry settles and the countdown ends");
    animated.sync({});
    test::check(animated.entries().size() == 1 && animated.entries()[0].exiting, "an animated entry exits before it is removed");
    animated.sync({});
    animated.tick(std::chrono::steady_clock::now() + 20s);
    test::check(animated.entries().empty(), "the exit finishes");
}

void check_model_sync() {
    check_instant_sync();
    check_animated_sync();
}

void check_view_state() {
    Instant instant;
    NotificationViewState view;
    test::check(view.local_opacity(5) == 1.0f && !view.fading(5), "unknown ids are fully visible");
    test::check(view.hide_locally(5) && view.fading(5) && view.local_opacity(5) == 0.0f, "hiding fades the card out");
    test::check(!view.hide_locally(5), "hiding twice is a no-op");
    test::check(view.set_hover(5) && view.hovered() == 5 && !view.set_hover(5) && view.set_hover(std::nullopt) && view.hovered() == 0, "hover changes are reported once");

    NotificationModel model;
    model.sync({make(9, "a")});
    view.forget_missing(model.entries());
    test::check(!view.fading(5), "forgotten ids stop fading");
}

void check_layout() {
    Instant instant;
    NotificationModel model;
    NotificationViewState view;
    test::RecordingCanvas canvas;

    NotificationLayout empty = astralia::layout_notifications(canvas, model, view, 480);
    test::check(empty.cards.empty() && empty.height == 0.0f, "no entries, no layout");

    model.sync({make(1, "a"), make(2, "b", "title", "body")});
    NotificationLayout layout = astralia::layout_notifications(canvas, model, view, 480);
    test::check(layout.cards.size() == 2 && layout.cards[0].entry->id == 2, "newest card is laid out first");
    float newest = astralia::notification_card_height(20, 20, 20);
    float oldest = astralia::notification_card_height(20, 0, 0);
    test::check(layout.cards[0].height == newest && layout.cards[1].height == oldest, "heights follow the measured text");
    test::check(layout.height == newest + oldest + cfg::gap, "stack height adds the gap");
    test::check(layout.cards[0].y == layout.height - newest && layout.cards[1].y == 0.0f, "the newest card sits at the bottom of the stack");

    NotificationModel tall;
    tall.sync({make(1, "a", "x", "y"), make(2, "b", "x", "y"), make(3, "c", "x", "y"), make(4, "d", "x", "y"), make(5, "e", "x", "y")});
    NotificationLayout fit = astralia::layout_notifications(canvas, tall, view, 480);
    test::check(fit.cards.size() < 5 && fit.height <= 480 && fit.cards[0].entry->id == 5, "older cards beyond the limit are dropped");
    NotificationLayout one = astralia::layout_notifications(canvas, tall, view, 10);
    test::check(one.cards.size() == 1 && one.cards[0].entry->id == 5, "the newest card always shows");

    view.hide_locally(2);
    NotificationLayout hidden = astralia::layout_notifications(canvas, model, view, 480);
    test::check(hidden.cards.size() == 1 && hidden.cards[0].entry->id == 1, "a locally closed card takes no space");
}

void check_close_hit() {
    Instant instant;
    NotificationModel model;
    NotificationViewState view;
    test::RecordingCanvas canvas;
    model.sync({make(1, "a"), make(2, "b")});
    NotificationLayout layout = astralia::layout_notifications(canvas, model, view, 480);
    float x = cfg::card_width - 10;
    test::check(astralia::notification_close_at(layout, view, x, layout.cards[0].y + 5) == 2u, "the newest card's close button hits");
    test::check(astralia::notification_close_at(layout, view, x, layout.cards[1].y + 5) == 1u, "the older card's close button hits");
    test::check(!astralia::notification_close_at(layout, view, 200, layout.cards[0].y + 5), "the card body hits nothing");
    test::check(!astralia::notification_close_at(layout, view, x, layout.cards[0].y + cfg::close_hit + 1), "below the button hits nothing");
    test::check(astralia::notification_close_boxes(layout, view).size() == 2, "one box per live card");
    view.hide_locally(2);
    test::check(!astralia::notification_close_at(layout, view, x, layout.cards[0].y + 5), "a fading card cannot be closed again");
}

void check_paint() {
    using Kind = test::Op::Kind;
    Instant instant;
    NotificationModel model;
    NotificationViewState view;
    test::RecordingCanvas canvas;
    model.sync({make(1, "mail", "Hello", "World", astralia::notification_urgency_critical)});
    NotificationLayout layout = astralia::layout_notifications(canvas, model, view, 480);
    canvas.ops.clear();
    astralia::paint_notifications(canvas, view, layout);
    test::check(canvas.count(Kind::rounded) == 2, "card and urgency dot");
    test::check(canvas.count(Kind::text) == 4, "app, title, body and the close glyph");
    test::check(canvas.count(Kind::begin_group) == 0, "no countdown group on a static backend");
    test::check(canvas.ops[0].box.w == cfg::card_width && canvas.ops[0].box.h == layout.cards[0].height, "the card spans the stack width");
    test::check(canvas.ops[1].color.r > 0.9f && canvas.ops[1].color.g < 0.4f, "a critical card uses the critical colour");
}

void check_paint_animated() {
    using Kind = test::Op::Kind;
    NotificationViewState view;
    test::RecordingCanvas canvas;
    NotificationModel animated;
    animated.sync({make(1, "mail")});
    animated.tick(std::chrono::steady_clock::now() + std::chrono::milliseconds(300));
    NotificationLayout l2 = astralia::layout_notifications(canvas, animated, view, 480);
    canvas.ops.clear();
    astralia::paint_notifications(canvas, view, l2);
    test::check(canvas.count(Kind::begin_group) == 2 && canvas.count(Kind::end_group) == 2, "animated cards draw a countdown track and fill");
}

} // namespace

void check_notification_module() {
    check_card_height();
    check_model_sync();
    check_view_state();
    check_layout();
    check_close_hit();
    check_paint();
    check_paint_animated();
}
