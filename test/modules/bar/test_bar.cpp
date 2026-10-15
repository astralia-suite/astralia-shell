#include <chrono>

#include "check.h"
#include "render/recording_canvas.h"

#include "config/bar_layout.h"
#include "render/icons.h"

#include "core/animation.h"

#include "modules/bar/frame.h"
#include "modules/bar/model.h"
#include "modules/bar/style.h"
#include "modules/bar/view.h"

namespace {

using astralia::BarActionKind;
using astralia::BarItem;
using astralia::BarModel;
using astralia::BarSources;
using astralia::BarStyle;
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

BarSources make_sources(const astralia::CompositorState &state) {
    BarSources sources;
    sources.compositor = [&state]() -> const astralia::CompositorState & { return state; };
    sources.output = "DP-1";
    return sources;
}

void check_bar_style() {
    using astralia::bar_style_resolve;
    using astralia::bar_style_spec;
    test::check(bar_style_resolve(BarStyle::islands, false) == BarStyle::continuous, "islands falls back to continuous when unsupported");
    test::check(bar_style_resolve(BarStyle::islands, true) == BarStyle::islands && bar_style_resolve(BarStyle::okinami, false) == BarStyle::okinami, "supported styles are kept");
    test::check(bar_style_spec(BarStyle::okinami, false).has_rail() && !bar_style_spec(BarStyle::continuous, false).has_rail(), "only okinami has a rail");
    test::check(bar_style_spec(BarStyle::islands, true).has_pill_bg() && !bar_style_spec(BarStyle::continuous, true).has_pill_bg(), "only islands has pill backgrounds");

    using astralia::bar_autohide_geometry;
    auto shown = bar_autohide_geometry(false, false, 40, 10, 0);
    test::check(shown.height == 40 && shown.margin_top == 10 && shown.exclusive_zone == 40, "autohide off keeps the bar shown");
    for (BarStyle pill : {BarStyle::islands, BarStyle::continuous}) {
        const astralia::BarStyleSpec &spec = bar_style_spec(pill, true);
        test::check(spec.shadow == astralia::metrics::shadow_offset && spec.shadow_dx == astralia::metrics::shadow_offset && spec.radius == astralia::metrics::radius_card, "the bar shadow and corners match every other component");
    }
    test::check(bar_style_spec(BarStyle::okinami, true).island_radius == astralia::metrics::radius_card && bar_style_spec(BarStyle::okinami, true).fillet_radius == astralia::metrics::radius_card && bar_style_spec(BarStyle::okinami, true).shadow == 0.0f, "okinami uses the card radius and no shadow");
    auto hugged = bar_autohide_geometry(false, false, 40, 10, 30);
    test::check(hugged.height == 70 && hugged.exclusive_zone == 40, "hug radius grows the surface only");
    auto revealed = bar_autohide_geometry(true, false, 40, 10, 30);
    test::check(revealed.height == 80 && revealed.margin_top == 0 && revealed.exclusive_zone == 0, "revealed autohide reserves nothing");
    auto collapsed = bar_autohide_geometry(true, true, 40, 10, 30);
    test::check(collapsed.height == 1 && collapsed.margin_top == 0, "collapsed autohide is a strip");

    const astralia::BarStyleSpec &okinami = bar_style_spec(BarStyle::okinami, false);
    astralia::BarFrame frame = astralia::bar_frame(okinami, 300.0f, astralia::IslandSpan{500.0f, 700.0f}, 900.0f, 1000.0f);
    test::check(frame.islands.size() == 3 && frame.fillets.size() == 4, "three islands and four fillets");
    test::check(frame.islands[0].outer_x == -okinami.island_radius && frame.islands[2].outer_x + frame.islands[2].outer_width == 1000.0f + okinami.island_radius, "flush islands extend past the edges");
    test::check(frame.islands[1].inner_x == 500.0f + okinami.border_width, "inner edge is inset by the border");
    astralia::BarFrame none = astralia::bar_frame(okinami, std::nullopt, std::nullopt, std::nullopt, 400.0f);
    test::check(none.islands.empty() && none.fillets.empty(), "no groups, no islands");
}

void check_bar_glyphs() {
    test::check(std::string(astralia::bar_wifi_glyph(90)) == astralia::icon::wifi && std::string(astralia::bar_wifi_glyph(10)) == astralia::icon::wifi0, "wifi glyph bands");
    astralia::BatteryStatus status{true, 40, true, false, false, 0};
    test::check(std::string(astralia::bar_battery_glyph(status)) == astralia::icon::battery_charging, "charging glyph");
    status = {true, 40, false, false, false, 0};
    test::check(std::string(astralia::bar_battery_glyph(status)) == astralia::icon::battery2 && astralia::bar_battery_label(status) == "40%", "discharging glyph and label");
    status = {true, 100, false, true, false, 0};
    test::check(std::string(astralia::bar_battery_glyph(status)) == astralia::icon::plugged_in && astralia::bar_battery_label(status) == "Plugged in", "full glyph and label");
}

void check_bar_text() {
    using namespace astralia;
    test::check(std::string(bar_bluetooth_glyph({true, false, true, ""})) == icon::bluetooth_off, "unpowered is off");
    test::check(std::string(bar_bluetooth_glyph({true, true, true, ""})) == icon::bluetooth_connected, "a connected device shows connected");
    test::check(bar_bluetooth_label({}).empty(), "no adapter has no label");
    test::check(bar_bluetooth_label({true, false, false, ""}) == "Disconnected", "unpowered label");
    test::check(bar_bluetooth_label({true, true, false, ""}) == "Idle", "powered idle label");
    test::check(bar_bluetooth_label({true, true, true, "Buds"}) == "Buds", "device name label");

    auto at = [](std::chrono::nanoseconds offset) {
        using namespace std::chrono;
        return sys_days{2026y / September / 27} + 12h + offset;
    };
    test::check(ms_until_next_second(at(0s)) == 1s, "on the boundary waits a full second");
    test::check(ms_until_next_second(at(250ms)) == 750ms, "mid-second waits the remainder");
    test::check(ms_until_next_second(at(999ms)) == 1ms, "last millisecond waits 1 ms");
    test::check(ms_until_next_second(at(999500us)) == 1ms, "sub-millisecond remainder rounds up");
    test::check(ms_until_next_second(at(500us)) == 1s, "just past the boundary rounds up to a full second");
}

void check_bar_layout() {
    Instant instant;
    astralia::CompositorState state = make_state();
    auto now = BarModel::Clock::now();
    BarModel model(make_sources(state), now);
    test::RecordingCanvas canvas;
    model.set_style(astralia::bar_style_spec(BarStyle::continuous, false));
    model.set_clock_label("Fri");
    model.refresh(now);
    model.layout(canvas, 1000.0f);

    const astralia::BarItemView &logout = model.item(BarItem::logout);
    test::check(logout.visible && logout.box.x == 10.0f && logout.box.w == 38.0f && logout.box.h == 40.0f, "the logout pill starts after the island padding");
    test::check(!model.item(BarItem::battery).visible && !model.item(BarItem::bluetooth).visible && !model.item(BarItem::resource).visible, "items without a source are hidden");
    test::check(model.item(BarItem::tray).visible && model.item(BarItem::network).visible == false, "the tray is always shown, the network needs its service");

    const astralia::BarItemView &media = model.item(BarItem::media);
    const astralia::BarItemView &clock = model.item(BarItem::clock);
    test::check(media.box.x + media.box.w + 10.0f == clock.box.x, "center pills are one gap apart");
    float center = (media.box.x + clock.box.x + clock.box.w) / 2.0f;
    test::check(center == 500.0f, "the center group is centered");
    test::check(model.item(BarItem::tray).box.x + model.item(BarItem::tray).box.w == 990.0f, "the right group ends at the island padding");
    test::check(!model.layout().left_end && !model.layout().center && !model.layout().right_start, "continuous has no islands");
    test::check(!model.layout().dividers.empty(), "continuous has dividers");

    test::check(model.workspaces().size() == 3 && model.workspaces()[1].box.w == 24.0f && model.workspaces()[0].box.w == 12.0f, "the active workspace pill is twice as wide");
    test::check(model.workspaces()[0].box.x == logout.box.x + logout.box.w + 10.0f + 10.0f, "workspaces follow the logout pill");
    test::check(model.layout().workspaces.w > 0.0f && model.layout().overview.w > 0.0f, "the row has an overview button");
    test::check(model.dock().size() == 1 && model.layout().dock.w == 22.0f + 20.0f, "one dock icon in a padded capsule");

    model.set_style(astralia::bar_style_spec(BarStyle::okinami, false));
    model.layout(canvas, 1000.0f);
    test::check(model.layout().left_end && model.layout().center && model.layout().right_start, "okinami has three islands");
    test::check(model.frame().islands.size() == 3, "the frame follows the layout");
    test::check(model.item(BarItem::logout).box.x == 6.0f, "okinami uses the island padding");
}

void check_bar_workspace_slots() {
    Instant instant;
    astralia::CompositorState state = make_state();
    state.focused_monitor = "DP-1";
    state.by_monitor["HDMI-1"].active_id = 5;
    state.by_monitor["HDMI-1"].workspaces = {{5, "5", true}};
    auto now = BarModel::Clock::now();
    BarSources sources = make_sources(state);
    sources.workspace_slots = 10;
    BarModel model(sources, now);
    model.refresh(now);
    test::check(model.workspaces().size() == 10, "persistent slots always show ten pills");
    test::check(model.workspaces()[1].active && !model.workspaces()[4].active, "the output's own workspace is active");
    test::check(model.workspaces()[0].occupied && model.workspaces()[4].occupied && !model.workspaces()[2].occupied, "occupied covers every output");
    BarSources unknown = make_sources(state);
    unknown.workspace_slots = 10;
    unknown.output = "unknown";
    BarModel fallback(unknown, now);
    fallback.refresh(now);
    test::check(fallback.workspaces()[1].active, "an unknown output follows the focused one");
}

void check_bar_interaction() {
    Instant instant;
    astralia::CompositorState state = make_state();
    auto now = BarModel::Clock::now();
    BarModel model(make_sources(state), now);
    test::RecordingCanvas canvas;
    model.set_style(astralia::bar_style_spec(BarStyle::continuous, false));
    model.set_clock_label("Fri 2026-10-09");
    model.refresh(now);
    model.layout(canvas, 1000.0f);

    const astralia::BarItemView &clock = model.item(BarItem::clock);
    float collapsed = clock.box.w;
    double cx = clock.box.x + 5;
    astralia::BarAction action = model.press(cx, 20);
    test::check(action.kind == BarActionKind::toggle && action.item == BarItem::clock, "clicking an item toggles it");
    model.hover(cx, 20.0, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(BarItem::clock).expand == 1.0f && model.item(BarItem::clock).box.w > collapsed, "hover expands the label");
    model.hover(std::nullopt, std::nullopt, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(BarItem::clock).box.w == collapsed, "leaving collapses it");

    model.set_open(BarItem::tray, now);
    model.layout(canvas, 1000.0f);
    test::check(model.item(BarItem::tray).expand == 1.0f && model.open() == BarItem::tray, "an open panel pins its pill");
    model.set_open(std::nullopt, now);
    test::check(model.until_idle(now) <= 80ms && model.until_idle(now) > 0ms, "a closed panel lingers briefly");
    test::check(model.item(BarItem::tray).expand == 1.0f, "the pill stays open during the linger");
    model.tick(now + 200ms);
    test::check(model.item(BarItem::tray).expand == 0.0f && model.until_idle(now + 200ms) == 1h, "the linger ends and the model goes idle");

    const astralia::BarWorkspace &second = model.workspaces()[1];
    action = model.press(second.box.x + 2, 20);
    test::check(action.kind == BarActionKind::workspace && action.workspace == 2, "clicking a workspace pill focuses it");
    action = model.press(model.layout().overview.x + 2, 20);
    test::check(action.kind == BarActionKind::overview, "the overview button opens the overview");
    test::check(model.clickable(second.box.x, 20) && !model.clickable(330.0, 20), "clickable follows the hit test");

    test::RecordingCanvas painted;
    model.layout(painted, 1000.0f);
    astralia::paint_bar(painted, model);
    test::check(painted.count(test::Op::Kind::text) >= 4 && painted.count(test::Op::Kind::rect) >= 1, "painting draws glyphs and dividers");
    test::check(painted.count(test::Op::Kind::begin_group) == painted.count(test::Op::Kind::end_group), "groups are balanced");
}

} // namespace

void check_bar_frame() {
    using astralia::bar_style_spec;
    using test::Op;
    const astralia::BarStyleSpec &okinami = bar_style_spec(BarStyle::okinami, false);
    astralia::BarFrame frame = astralia::bar_frame(okinami, 300.0f, astralia::IslandSpan{500.0f, 700.0f}, 900.0f, 1000.0f);

    test::RecordingCanvas base;
    astralia::bar_frame_base(base, okinami, frame, 1000.0f, 40.0f);
    test::check(base.count(Op::Kind::rounded) == 2 * frame.islands.size() && base.count(Op::Kind::rect) == 2, "the base draws the rail and each island twice, outline then fill");
    test::check(base.count(Op::Kind::erase) == 2 && base.ops.back().kind == Op::Kind::erase && base.ops.back().value == 0.0f, "the base ends with erase off");

    test::RecordingCanvas overlay;
    astralia::bar_frame_overlay(overlay, okinami, frame, 1000.0f, 40.0f, 0.0f, true, true);
    test::check(overlay.count(Op::Kind::fillet) == 2 * frame.fillets.size(), "each fillet draws an outline and an inner cutout");
    test::check(overlay.ops.back().kind == Op::Kind::erase && overlay.ops.back().value == 0.0f, "the overlay ends with erase off");
    const Op &outer = overlay.ops.front();
    test::check(outer.kind == Op::Kind::fillet && outer.box.w == okinami.fillet_radius && outer.box.h == okinami.fillet_radius && outer.box.y == okinami.rail_height, "the outer fillet is a square of the fillet radius under the rail");

    test::RecordingCanvas hugged;
    astralia::bar_frame_overlay(hugged, okinami, frame, 1000.0f, 40.0f, 12.0f, true, true);
    test::check(hugged.count(Op::Kind::fillet) == overlay.count(Op::Kind::fillet) + 4, "hug corners add an outline and a cutout per flush side");

    test::RecordingCanvas pill;
    astralia::bar_frame_overlay(pill, bar_style_spec(BarStyle::continuous, false), frame, 1000.0f, 40.0f, 12.0f, true, true);
    test::check(pill.ops.empty(), "a style without a rail has no overlay");
}

void check_bar() {
    check_bar_style();
    check_bar_frame();
    check_bar_glyphs();
    check_bar_text();
    check_bar_layout();
    check_bar_workspace_slots();
    check_bar_interaction();
}
