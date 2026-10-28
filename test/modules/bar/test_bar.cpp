#include <chrono>

#include "config/bar_layout.h"

#include "render/icons.h"

#include "core/animation.h"

#include "core/wall_clock.h"

#include "modules/bar/model/model.h"
#include "modules/bar/paint/frame.h"
#include "modules/bar/paint/style.h"
#include "modules/bar/paint/view.h"

#include "check.h"
#include "render/recording_canvas.h"

namespace {

using astralia::BarStyle;
using astralia::bar::ActionKind;
using astralia::bar::Item;
using astralia::bar::Model;
using astralia::bar::Sources;
using namespace std::chrono_literals;
namespace icon = astralia::icon;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

astralia::CompositorState make_state() {
    astralia::CompositorState state;
    astralia::MonitorWorkspaces monitor;
    monitor.active_id = 2;
    monitor.workspaces = {{1, "1", true}, {2, "2", true}, {3, "3", false}};
    state.by_monitor["DP-1"] = monitor;
    astralia::CompositorClient client;
    client.address = "0x1";
    client.window_class = "kitty";
    client.workspace_id = 2;
    state.clients.push_back(client);
    return state;
}

astralia::bar::Sources make_sources(const astralia::CompositorState &state) {
    astralia::bar::Sources sources;
    sources.compositor = [&state]() -> const astralia::CompositorState & { return state; };
    sources.output = "DP-1";
    return sources;
}

void check_bar_style() {
    using astralia::bar::style_resolve;
    using astralia::bar::style_spec;
    test::check(astralia::bar::style_resolve(BarStyle::continuous, false) == BarStyle::continuous &&
                    astralia::bar::style_resolve(BarStyle::okinami, false) == BarStyle::okinami,
                "supported styles are kept");
    test::check(astralia::bar::style_spec(BarStyle::okinami, false).has_rail() &&
                    !astralia::bar::style_spec(BarStyle::continuous, false).has_rail(),
                "only okinami has a rail");
    test::check(astralia::bar::style_spec(BarStyle::pop, true).has_pill_bg() &&
                    !astralia::bar::style_spec(BarStyle::continuous, true).has_pill_bg(),
                "only pop has pill backgrounds");
    test::check(style_resolve(BarStyle::pop, false) == BarStyle::continuous &&
                    style_resolve(BarStyle::pop, true) == BarStyle::pop,
                "pop falls back to continuous when unsupported");
    test::check(style_spec(BarStyle::pop, true).has_pill_bg() &&
                    !style_spec(BarStyle::pop, true).has_rail() &&
                    !style_spec(BarStyle::pop, true).continuous &&
                    style_spec(BarStyle::pop, true).shadow_dx == 0.0f,
                "pop is a pill style with a straight shadow");

    using astralia::bar::autohide_geometry;
    auto shown = astralia::bar::autohide_geometry(false, false, 40, 10, 0);
    test::check(shown.height == 40 && shown.margin_top == 10 && shown.exclusive_zone == 40,
                "autohide off keeps the bar shown");
    for (BarStyle pill : {BarStyle::continuous, BarStyle::pop}) {
        const astralia::bar::StyleSpec &spec = astralia::bar::style_spec(pill, true);
        test::check(spec.shadow == astralia::metrics::shadow_offset && spec.shadow_dx == 0.0f &&
                        spec.radius == astralia::bar_layout::pop_radius,
                    "continuous takes the straight shadow and corner radius of pop");
    }
    test::check(astralia::bar::style_spec(BarStyle::okinami, true).island_radius ==
                        astralia::metrics::radius_card &&
                    astralia::bar::style_spec(BarStyle::okinami, true).fillet_radius ==
                        astralia::metrics::radius_card &&
                    astralia::bar::style_spec(BarStyle::okinami, true).shadow == 0.0f,
                "okinami uses the card radius and no shadow");
    auto hugged = astralia::bar::autohide_geometry(false, false, 40, 10, 30);
    test::check(hugged.height == 70 && hugged.exclusive_zone == 40,
                "hug radius grows the surface only");
    auto revealed = astralia::bar::autohide_geometry(true, false, 40, 10, 30);
    test::check(revealed.height == 80 && revealed.margin_top == 0 && revealed.exclusive_zone == 0,
                "revealed autohide reserves nothing");
    auto collapsed = astralia::bar::autohide_geometry(true, true, 40, 10, 30);
    test::check(collapsed.height == 1 && collapsed.margin_top == 0,
                "collapsed autohide is a strip");

    const astralia::bar::StyleSpec &okinami = astralia::bar::style_spec(BarStyle::okinami, false);
    astralia::bar::Frame frame = astralia::bar::bar_frame(
        okinami, 300.0f, astralia::bar::IslandSpan{500.0f, 700.0f}, 900.0f, 1000.0f);
    test::check(frame.islands.size() == 3 && frame.fillets.size() == 4,
                "three islands and four fillets");
    test::check(frame.islands[0].outer_x == -okinami.island_radius &&
                    frame.islands[2].outer_x + frame.islands[2].outer_width ==
                        1000.0f + okinami.island_radius,
                "flush islands extend past the edges");
    test::check(frame.islands[1].inner_x == 500.0f + okinami.border_width,
                "inner edge is inset by the border");
    astralia::bar::Frame none =
        astralia::bar::bar_frame(okinami, std::nullopt, std::nullopt, std::nullopt, 400.0f);
    test::check(none.islands.empty() && none.fillets.empty(), "no groups, no islands");
}

void check_bar_glyphs() {
    test::check(std::string(astralia::bar::wifi_glyph(90)) == astralia::icon::wifi &&
                    std::string(astralia::bar::wifi_glyph(10)) == astralia::icon::wifi0,
                "wifi glyph bands");
    astralia::BatteryStatus status{true, 40, true, false, false, 0};
    test::check(std::string(astralia::bar::battery_glyph(status)) ==
                    astralia::icon::battery_charging,
                "charging glyph");
    status = {true, 40, false, false, false, 0};
    test::check(std::string(astralia::bar::battery_glyph(status)) == astralia::icon::battery2 &&
                    astralia::bar::battery_label(status) == "40%",
                "discharging glyph and label");
    status = {true, 100, false, true, false, 0};
    test::check(std::string(astralia::bar::battery_glyph(status)) == astralia::icon::plugged_in &&
                    astralia::bar::battery_label(status) == "Plugged in",
                "full glyph and label");
}

void check_bar_text() {
    using namespace astralia;
    test::check(std::string(astralia::bar::bluetooth_glyph({true, false, true, ""})) ==
                    icon::bluetooth_off,
                "unpowered is off");
    test::check(std::string(astralia::bar::bluetooth_glyph({true, true, true, ""})) ==
                    icon::bluetooth_connected,
                "a connected device shows connected");
    test::check(astralia::bar::bluetooth_label({}).empty(), "no adapter has no label");
    test::check(astralia::bar::bluetooth_label({true, false, false, ""}) == "Disconnected",
                "unpowered label");
    test::check(astralia::bar::bluetooth_label({true, true, false, ""}) == "Idle",
                "powered idle label");
    test::check(astralia::bar::bluetooth_label({true, true, true, "Buds"}) == "Buds",
                "device name label");

    auto at = [](std::chrono::nanoseconds offset) {
        using namespace std::chrono;
        return sys_days{2026y / September / 27} + 12h + offset;
    };
    test::check(astralia::ms_until_next_second(at(0s)) == 1s,
                "on the boundary waits a full second");
    test::check(astralia::ms_until_next_second(at(250ms)) == 750ms,
                "mid-second waits the remainder");
    test::check(astralia::ms_until_next_second(at(999ms)) == 1ms, "last millisecond waits 1 ms");
    test::check(astralia::ms_until_next_second(at(999500us)) == 1ms,
                "sub-millisecond remainder rounds up");
    test::check(astralia::ms_until_next_second(at(500us)) == 1s,
                "just past the boundary rounds up to a full second");
}

void check_bar_layout() {
    Instant instant;
    astralia::CompositorState state = make_state();
    auto now = astralia::bar::Model::Clock::now();
    astralia::bar::Model model(make_sources(state), now);
    test::RecordingCanvas canvas;
    model.set_style(astralia::bar::style_spec(BarStyle::continuous, false));
    model.refresh(now);
    model.layout(canvas, 1000.0f);

    const astralia::bar::ItemView &logout = model.item(astralia::bar::Item::logout);
    test::check(logout.visible && logout.box.x == 10.0f && logout.box.w == 38.0f &&
                    logout.box.h == 40.0f,
                "the logout pill starts after the island padding");
    test::check(!model.item(astralia::bar::Item::battery).visible &&
                    !model.item(astralia::bar::Item::bluetooth).visible &&
                    !model.item(astralia::bar::Item::resource).visible,
                "items without a source are hidden");
    test::check(model.item(astralia::bar::Item::tray).visible &&
                    model.item(astralia::bar::Item::network).visible == false,
                "the tray is always shown, the network needs its service");

    const astralia::bar::ItemView &media = model.item(astralia::bar::Item::media);
    const astralia::bar::ItemView &clock = model.item(astralia::bar::Item::time);
    const astralia::bar::ItemView &classic_window = model.item(astralia::bar::Item::active_window);
    test::check(classic_window.visible &&
                    classic_window.box.x == logout.box.x + logout.box.w + 10.0f &&
                    media.box.x == classic_window.box.x + classic_window.box.w + 10.0f,
                "the active window and media follow the logout pill");
    test::check(model.item(astralia::bar::Item::tray).box.x == media.box.x + media.box.w + 10.0f,
                "the tray follows media");
    test::check(model.layout().workspaces.x + model.layout().workspaces.w + 10.0f == clock.box.x,
                "the time pill follows the workspaces");
    const astralia::bar::ItemView &classic_date = model.item(astralia::bar::Item::date);
    test::check(clock.box.x + clock.box.w + 10.0f == classic_date.box.x,
                "the date pill follows the time pill");
    float center = (model.layout().workspaces.x + classic_date.box.x + classic_date.box.w) / 2.0f;
    test::check(std::abs(center - 500.0f) <= 1.0f, "the center group is centered");
    test::check(!model.layout().left_end && !model.layout().center && !model.layout().right_start,
                "continuous has no islands");
    test::check(!model.layout().dividers.empty(), "continuous has dividers");

    test::check(model.workspaces().size() == 3 && model.workspaces()[1].box.w == 24.0f &&
                    model.workspaces()[0].box.w == 12.0f,
                "the active workspace pill is twice as wide");
    test::check(model.layout().workspaces.w > 0.0f, "the row has workspace pills");
    test::check(model.dock().size() == 1 && model.layout().dock.w == 18.0f + 20.0f,
                "one dock icon in a padded capsule");
    test::check(model.layout().dock.x > classic_date.box.x + classic_date.box.w,
                "the dock opens the right region");

    model.set_style(astralia::bar::style_spec(BarStyle::okinami, false));
    model.layout(canvas, 1000.0f);
    test::check(model.layout().left_end && model.layout().center && model.layout().right_start,
                "okinami has three islands");
    test::check(model.frame().islands.size() == 3, "the frame follows the layout");
    test::check(model.item(astralia::bar::Item::logout).box.x == 6.0f,
                "okinami uses the island padding");

    using astralia::bar::Item;
    model.set_style(astralia::bar::style_spec(BarStyle::pop, true));
    model.refresh(now);
    model.layout(canvas, 1000.0f);
    const astralia::bar::ItemView &window = model.item(Item::active_window);
    const astralia::bar::ItemView &pop_media = model.item(Item::media);
    const astralia::bar::ItemView &pop_clock = model.item(Item::time);
    const astralia::bar::ItemView &date = model.item(Item::date);
    const astralia::bar::ItemView &pop_logout = model.item(Item::logout);
    test::check(pop_logout.visible && pop_logout.box.x == 0.0f && window.visible &&
                    window.box.x == pop_logout.box.x + pop_logout.box.w + 10.0f &&
                    pop_media.box.x == window.box.x + window.box.w + 10.0f &&
                    model.item(Item::tray).box.x == pop_media.box.x + pop_media.box.w + 10.0f,
                "pop left zone is logout, active window, media, tray");
    test::check(model.press(window.box.x + 2, 20).kind == astralia::bar::ActionKind::overview,
                "the window widget opens the overview");
    test::check(model.item(Item::logout).box.w > 0.0f && model.layout().dock.w > 0.0f,
                "pop shows logout and the dock");
    test::check(date.box.x == pop_clock.box.x + pop_clock.box.w + 10.0f &&
                    std::abs((model.layout().workspaces.x + date.box.x + date.box.w) / 2.0f -
                             500.0f) <= 1.0f,
                "pop center zone is centered: workspaces, clock, date");
    test::check(model.workspaces()[1].box.w == 42.0f && model.workspaces()[0].box.w == 36.0f &&
                    model.workspaces()[1].box.x ==
                        model.workspaces()[0].box.x + 36.0f + astralia::bar_layout::pop_badge_gap,
                "pop workspaces are separate badges, the active one wider");
    test::check(model.layout().dividers.empty() && !model.layout().left_end,
                "pop has no dividers or islands");
    test::check(pop_clock.expand == 1.0f && pop_clock.box.w > pop_clock.icon_w + 2.0f * 13.0f &&
                    date.box.w == date.icon_w + 26.0f,
                "the time stays expanded while other items retract to their icon until hovered");
    using astralia::bar::PanelAnchor;
    test::check(model.zone(Item::media) == PanelAnchor::left &&
                    model.zone(Item::tray) == PanelAnchor::left &&
                    model.zone(Item::time) == PanelAnchor::center &&
                    model.zone(Item::date) == PanelAnchor::center,
                "panels follow the zone their widget is in");
    model.set_style(astralia::bar::style_spec(BarStyle::continuous, false));
    model.refresh(now);
    model.layout(canvas, 1000.0f);
    test::check(model.zone(Item::media) == PanelAnchor::left &&
                    model.zone(Item::tray) == PanelAnchor::left &&
                    model.zone(Item::time) == PanelAnchor::center &&
                    model.zone(Item::resource) == PanelAnchor::right,
                "the other styles share pop's zones");
}

void check_bar_dock_slide() {
    astralia::CompositorState state = make_state();
    state.clients[0].at = {0.0, 0.0};
    astralia::CompositorClient second = state.clients[0];
    second.address = "0x2";
    second.at = {100.0, 0.0};
    state.clients.push_back(second);
    auto now = astralia::bar::Model::Clock::now();
    astralia::bar::Model model(make_sources(state), now);
    test::RecordingCanvas canvas;
    model.set_style(astralia::bar::style_spec(BarStyle::continuous, false));
    model.refresh(now);
    model.layout(canvas, 1000.0f);
    test::check(model.dock().size() == 2 && model.dock()[1].x == 28.0f,
                "the second dock icon starts one slot over");

    state.clients.erase(state.clients.begin());
    model.refresh(now);
    model.tick(now + std::chrono::milliseconds(100));
    model.layout(canvas, 1000.0f);
    float mid = model.dock()[0].x;
    test::check(model.dock().size() == 1 && mid > 0.0f && mid < 28.0f,
                "the icon after a removed one slides up instead of snapping");
    float mid_w = model.layout().dock.w;
    test::check(mid_w > 38.0f && mid_w < 66.0f, "the dock capsule shrinks with the slide");
    model.refresh(now + std::chrono::milliseconds(110));
    model.tick(now + std::chrono::milliseconds(400));
    model.layout(canvas, 1000.0f);
    test::check(model.dock()[0].x == 0.0f && model.layout().dock.w == 38.0f,
                "the slide settles on the new slot");

    astralia::CompositorClient third = state.clients[0];
    third.address = "0x3";
    third.at = {200.0, 0.0};
    state.clients.push_back(third);
    model.refresh(now + std::chrono::milliseconds(420));
    model.layout(canvas, 1000.0f);
    test::check(model.dock().size() == 2 && model.dock()[1].appear < 1.0f,
                "a new dock icon fades in");
}

void check_bar_workspace_slots() {
    Instant instant;
    astralia::CompositorState state = make_state();
    state.focused_monitor = "DP-1";
    state.by_monitor["HDMI-1"].active_id = 5;
    state.by_monitor["HDMI-1"].workspaces = {{5, "5", true}};
    auto now = astralia::bar::Model::Clock::now();
    astralia::bar::Sources sources = make_sources(state);
    sources.workspace_slots = 5;
    astralia::bar::Model model(sources, now);
    model.refresh(now);
    test::check(model.workspaces().size() == 5, "persistent slots always show five pills");
    test::check(model.workspaces()[1].active && !model.workspaces()[4].active,
                "the output's own workspace is active");
    test::check(model.workspaces()[0].occupied && model.workspaces()[4].occupied &&
                    !model.workspaces()[2].occupied,
                "occupied covers every output");
    astralia::bar::Sources unknown = make_sources(state);
    unknown.workspace_slots = 5;
    unknown.output = "unknown";
    astralia::bar::Model fallback(unknown, now);
    fallback.refresh(now);
    test::check(fallback.workspaces()[1].active, "an unknown output follows the focused one");
}

void check_bar_interaction() {
    Instant instant;
    astralia::CompositorState state = make_state();
    auto now = astralia::bar::Model::Clock::now();
    astralia::bar::Model model(make_sources(state), now);
    test::RecordingCanvas canvas;
    model.set_style(astralia::bar::style_spec(BarStyle::continuous, false));
    model.refresh(now);
    model.layout(canvas, 1000.0f);

    test::check(model.item(astralia::bar::Item::time).expand == 1.0f, "the time never retracts");
    const astralia::bar::ItemView &clock = model.item(astralia::bar::Item::date);
    float collapsed = clock.box.w;
    double cx = clock.box.x + 5;
    astralia::bar::Action action = model.press(cx, 20);
    test::check(action.kind == astralia::bar::ActionKind::toggle &&
                    action.item == astralia::bar::Item::date,
                "clicking an item toggles it");
    model.hover(cx, 20.0, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(astralia::bar::Item::date).expand == 1.0f &&
                    model.item(astralia::bar::Item::date).box.w > collapsed,
                "hover expands the label");
    model.hover(std::nullopt, std::nullopt, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(astralia::bar::Item::date).box.w == collapsed, "leaving collapses it");

    model.set_open(astralia::bar::Item::tray, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(astralia::bar::Item::tray).expand == 1.0f &&
                    model.open() == astralia::bar::Item::tray,
                "an open panel pins its pill");
    model.set_open(std::nullopt, now);
    test::check(model.until_idle(now) <= 80ms && model.until_idle(now) > 0ms,
                "a closed panel lingers briefly");
    test::check(model.item(astralia::bar::Item::tray).expand == 1.0f,
                "the pill stays open during the linger");
    model.tick(now + 200ms);
    test::check(model.item(astralia::bar::Item::tray).expand == 0.0f &&
                    model.until_idle(now + 200ms) == 1h,
                "the linger ends and the model goes idle");

    const astralia::bar::BarWorkspace &second = model.workspaces()[1];
    action = model.press(second.box.x + 2, 20);
    test::check(action.kind == astralia::bar::ActionKind::workspace && action.workspace == 2,
                "clicking a workspace pill focuses it");
    test::check(model.clickable(second.box.x, 20) && !model.clickable(330.0, 20),
                "clickable follows the hit test");

    test::RecordingCanvas painted;
    model.layout(painted, 1000.0f);
    astralia::bar::paint(painted, model);
    test::check(painted.count(test::Op::Kind::text) >= 4 &&
                    painted.count(test::Op::Kind::rect) >= 1,
                "painting draws glyphs and dividers");
    test::check(painted.count(test::Op::Kind::begin_group) ==
                    painted.count(test::Op::Kind::end_group),
                "groups are balanced");
}

} // namespace

void check_bar_frame() {
    using astralia::bar::style_spec;
    using test::Op;
    const astralia::bar::StyleSpec &okinami = astralia::bar::style_spec(BarStyle::okinami, false);
    astralia::bar::Frame frame = astralia::bar::bar_frame(
        okinami, 300.0f, astralia::bar::IslandSpan{500.0f, 700.0f}, 900.0f, 1000.0f);

    test::RecordingCanvas base;
    astralia::bar::frame_base(base, okinami, frame, 1000.0f, 40.0f);
    test::check(base.count(Op::Kind::rounded) == 2 * frame.islands.size() &&
                    base.count(Op::Kind::rect) == 2,
                "the base draws the rail and each island twice, outline then fill");
    test::check(base.count(Op::Kind::erase) == 2 && base.ops.back().kind == Op::Kind::erase &&
                    base.ops.back().value == 0.0f,
                "the base ends with erase off");

    test::RecordingCanvas overlay;
    astralia::bar::frame_overlay(overlay, okinami, frame, 1000.0f, 40.0f, 0.0f, true, true);
    test::check(overlay.count(Op::Kind::fillet) == 2 * frame.fillets.size(),
                "each fillet draws an outline and an inner cutout");
    test::check(overlay.ops.back().kind == Op::Kind::erase && overlay.ops.back().value == 0.0f,
                "the overlay ends with erase off");
    const Op &outer = overlay.ops.front();
    test::check(outer.kind == Op::Kind::fillet && outer.box.w == okinami.fillet_radius &&
                    outer.box.h == okinami.fillet_radius && outer.box.y == okinami.rail_height,
                "the outer fillet is a square of the fillet radius under the rail");

    test::RecordingCanvas hugged;
    astralia::bar::frame_overlay(hugged, okinami, frame, 1000.0f, 40.0f, 12.0f, true, true);
    test::check(hugged.count(Op::Kind::fillet) == overlay.count(Op::Kind::fillet) + 4,
                "hug corners add an outline and a cutout per flush side");

    test::RecordingCanvas pill;
    astralia::bar::frame_overlay(pill, astralia::bar::style_spec(BarStyle::continuous, false),
                                 frame, 1000.0f, 40.0f, 12.0f, true, true);
    test::check(pill.ops.empty(), "a style without a rail has no overlay");
}

void check_bar() {
    check_bar_style();
    check_bar_frame();
    check_bar_glyphs();
    check_bar_text();
    check_bar_layout();
    check_bar_dock_slide();
    check_bar_workspace_slots();
    check_bar_interaction();
}
