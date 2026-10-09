#include <chrono>

#include "check.h"
#include "ui/recording_canvas.h"

#include "config/bar_layout.h"
#include "config/icons.h"

#include "core/animation.h"

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
    auto hugged = bar_autohide_geometry(false, false, 40, 10, 30);
    test::check(hugged.height == 70 && hugged.exclusive_zone == 40, "hug radius grows the surface only");
    auto revealed = bar_autohide_geometry(true, false, 40, 10, 30);
    test::check(revealed.height == 80 && revealed.margin_top == 0 && revealed.exclusive_zone == 0, "revealed autohide reserves nothing");
    auto collapsed = bar_autohide_geometry(true, true, 40, 10, 30);
    test::check(collapsed.height == 1 && collapsed.margin_top == 0, "collapsed autohide is a strip");

    const astralia::BarStyleSpec &okinami = bar_style_spec(BarStyle::okinami, false);
    astralia::BarFrame frame = astralia::bar_frame(okinami, 300.0f, astralia::IslandSpan{500.0f, 700.0f}, 900.0f, 1000.0f);
    test::check(frame.islands.size() == 3 && frame.fillets.size() == 4, "three islands and four fillets");
    test::check(frame.islands[0].outer_x == -16.0f && frame.islands[2].outer_x + frame.islands[2].outer_width == 1016.0f, "flush islands extend past the edges");
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

void check_bar() {
    check_bar_style();
    check_bar_glyphs();
    check_bar_text();
    check_bar_layout();
    check_bar_workspace_slots();
    check_bar_interaction();
}
