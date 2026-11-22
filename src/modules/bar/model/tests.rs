use std::rc::Rc;
use std::time::{Duration, Instant};

use super::*;
use crate::base::animation;
use crate::render::recording_canvas::RecordingCanvas;
use crate::service::battery::BatteryStatus;
use crate::service::bluetooth::BluetoothStatus;
use crate::service::compositor::{CompositorClient, CompositorState, MonitorWorkspaces, NullCompositor, Workspace};

struct InstantAnimations;

impl InstantAnimations {
    fn on() -> InstantAnimations {
        animation::set_instant(true);
        InstantAnimations
    }
}

impl Drop for InstantAnimations {
    fn drop(&mut self) {
        animation::set_instant(false);
    }
}

fn ws(id: i32, occupied: bool) -> Workspace {
    Workspace { id, name: id.to_string(), occupied }
}

fn make_state() -> CompositorState {
    let mut state = CompositorState::default();
    state.by_monitor.insert("DP-1".into(), MonitorWorkspaces { active_id: 2, workspaces: vec![ws(1, true), ws(2, true), ws(3, false)] });
    state.clients.push(CompositorClient { address: "0x1".into(), window_class: "kitty".into(), workspace_id: 2, ..Default::default() });
    state
}

fn make_sources(state: CompositorState) -> (Sources, Rc<NullCompositor>) {
    let compositor = Rc::new(NullCompositor::default());
    compositor.set_state(state);
    let sources = Sources {
        audio: None,
        battery: None,
        bluetooth: None,
        network: None,
        brightness: None,
        compositor: compositor.clone(),
        tray_occupied: Some(Rc::new(|| true)),
        output: "DP-1".into(),
        resource: false,
        workspace_slots: 0,
    };
    (sources, compositor)
}

fn style(s: BarStyle, islands: bool) -> StyleSpec {
    style_spec(s, islands)
}

#[test]
fn bar_glyphs() {
    assert!(wifi_glyph(90) == icons::WIFI && wifi_glyph(10) == icons::WIFI0, "wifi glyph bands");
    let mut status = BatteryStatus { present: true, percent: 40, charging: true, ..Default::default() };
    assert_eq!(battery_glyph(&status), icons::BATTERY_CHARGING, "charging glyph");
    status = BatteryStatus { present: true, percent: 40, ..Default::default() };
    assert!(battery_glyph(&status) == icons::BATTERY2 && battery_label(&status) == "40%", "discharging glyph and label");
    status = BatteryStatus { present: true, percent: 100, full: true, ..Default::default() };
    assert!(battery_glyph(&status) == icons::PLUGGED_IN && battery_label(&status) == "Plugged in", "full glyph and label");
}

#[test]
fn bar_text() {
    let bt = |present, powered, connected, device: &str| BluetoothStatus { present, powered, connected, device: device.into(), scanning: false };
    assert_eq!(bluetooth_glyph(&bt(true, false, true, "")), icons::BLUETOOTH_OFF, "unpowered is off");
    assert_eq!(bluetooth_glyph(&bt(true, true, true, "")), icons::BLUETOOTH_CONNECTED, "a connected device shows connected");
    assert!(bluetooth_label(&BluetoothStatus::default()).is_empty(), "no adapter has no label");
    assert_eq!(bluetooth_label(&bt(true, false, false, "")), "Disconnected");
    assert_eq!(bluetooth_label(&bt(true, true, false, "")), "Idle");
    assert_eq!(bluetooth_label(&bt(true, true, true, "Buds")), "Buds");
}

#[test]
fn bar_layout() {
    let _instant = InstantAnimations::on();
    let now = Instant::now();
    let (sources, _c) = make_sources(make_state());
    let mut model = Model::new(sources, now);
    let mut canvas = RecordingCanvas::default();
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);

    let logout = model.item(Item::Logout).clone();
    assert!(logout.visible && logout.rect.x == 10.0 && logout.rect.w == 38.0 && logout.rect.h == 40.0, "the logout pill starts after the island padding: {:?}", logout.rect);
    assert!(!model.item(Item::Battery).visible && !model.item(Item::Bluetooth).visible && !model.item(Item::Resource).visible, "items without a source are hidden");
    assert!(model.item(Item::Tray).visible && !model.item(Item::Network).visible, "an occupied tray is shown, the network needs its service");
    let media = model.item(Item::Media).rect;
    let clock = model.item(Item::Time).rect;
    let window = model.item(Item::ActiveWindow);
    assert!(window.visible && window.rect.x == logout.rect.x + logout.rect.w + 10.0 && media.x == window.rect.x + window.rect.w + 10.0, "the active window and media follow the logout pill");
    assert_eq!(model.item(Item::Tray).rect.x, media.x + media.w + 10.0, "the tray follows media");
    let info = model.item(Item::WorkspaceInfo).rect;
    assert_eq!(model.layout_ref().workspaces.x + model.layout_ref().workspaces.w + 10.0, info.x, "the workspace info follows the workspaces");
    assert_eq!(info.x + info.w + 10.0, clock.x, "the time pill follows the workspace info");
    let date = model.item(Item::Date).rect;
    assert_eq!(clock.x + clock.w + 10.0, date.x, "the date pill follows the time pill");
    let center = (model.layout_ref().workspaces.x + date.x + date.w) / 2.0;
    assert!((center - 500.0).abs() <= 1.0, "the center group is centered");
    assert!(model.layout_ref().left_end.is_none() && model.layout_ref().center.is_none() && model.layout_ref().right_start.is_none(), "continuous has no islands");
    assert!(!model.layout_ref().dividers.is_empty(), "continuous has dividers");
    assert!(model.workspaces().len() == 3 && model.workspaces()[1].rect.w == 24.0 && model.workspaces()[0].rect.w == 12.0, "the active workspace pill is twice as wide");
    assert!(model.layout_ref().workspaces.w > 0.0);
    assert!(model.dock().len() == 1 && model.layout_ref().dock.w == 18.0 + 20.0, "one dock icon in a padded capsule");
    assert!(model.layout_ref().dock.x > date.x + date.w, "the dock opens the right region");

    model.set_style(&style(BarStyle::Okinami, false));
    model.layout(&mut canvas, 1000.0);
    assert!(model.layout_ref().left_end.is_some() && model.layout_ref().center.is_some() && model.layout_ref().right_start.is_some(), "okinami has three islands");
    assert_eq!(model.frame().islands.len(), 3, "the frame follows the layout");
    assert_eq!(model.item(Item::Logout).rect.x, 6.0, "okinami uses the island padding");

    model.set_style(&style(BarStyle::Pop, true));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    let window = model.item(Item::ActiveWindow).rect;
    let media = model.item(Item::Media).rect;
    let clock = model.item(Item::Time).clone();
    let date = model.item(Item::Date).clone();
    let logout = model.item(Item::Logout).rect;
    assert!(logout.x == 0.0 && window.x == logout.x + logout.w + 10.0 && media.x == window.x + window.w + 10.0 && model.item(Item::Tray).rect.x == media.x + media.w + 10.0, "pop left zone is logout, active window, media, tray");
    assert_eq!(model.press(window.x as f64 + 2.0, 20.0).kind, ActionKind::Overview, "the window widget opens the overview");
    assert!(model.item(Item::Logout).rect.w > 0.0 && model.layout_ref().dock.w > 0.0, "pop shows logout and the dock");
    assert!(date.rect.x == clock.rect.x + clock.rect.w + 10.0 && ((model.layout_ref().workspaces.x + date.rect.x + date.rect.w) / 2.0 - 500.0).abs() <= 1.0, "pop center zone is centered");
    let w = model.workspaces();
    assert!(w[1].rect.w == 42.0 && w[0].rect.w == 36.0 && w[1].rect.x == w[0].rect.x + 36.0 + bar_layout::POP_BADGE_GAP, "pop workspaces are separate badges, the active one wider");
    assert!(model.layout_ref().dividers.is_empty() && model.layout_ref().left_end.is_none(), "pop has no dividers or islands");
    assert!(clock.expand == 1.0 && clock.rect.w > clock.icon_w + 2.0 * 13.0 && date.rect.w == date.icon_w + 26.0, "the time stays expanded while other items retract to their icon");
    assert!(model.zone(Item::Media) == PanelAnchor::Left && model.zone(Item::Tray) == PanelAnchor::Left && model.zone(Item::Time) == PanelAnchor::Center && model.zone(Item::Date) == PanelAnchor::Center, "panels follow the zone their widget is in");
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    assert!(model.zone(Item::Media) == PanelAnchor::Left && model.zone(Item::Tray) == PanelAnchor::Left && model.zone(Item::Time) == PanelAnchor::Center && model.zone(Item::Resource) == PanelAnchor::Right, "the other styles share pop's zones");
}

#[test]
fn bar_dock_slide() {
    let mut state = make_state();
    state.clients[0].at = [0.0, 0.0];
    let mut second = state.clients[0].clone();
    second.address = "0x2".into();
    second.at = [100.0, 0.0];
    state.clients.push(second);
    let now = Instant::now();
    let (sources, compositor) = make_sources(state.clone());
    let mut model = Model::new(sources, now);
    let mut canvas = RecordingCanvas::default();
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    assert!(model.dock().len() == 2 && model.dock()[1].x == 28.0, "the second dock icon starts one slot over");

    state.clients.remove(0);
    compositor.set_state(state.clone());
    model.refresh(now);
    model.tick(now + Duration::from_millis(100));
    model.layout(&mut canvas, 1000.0);
    let mid = model.dock()[0].x;
    assert!(model.dock().len() == 1 && mid > 0.0 && mid < 28.0, "the icon after a removed one slides up instead of snapping: {mid}");
    let mid_w = model.layout_ref().dock.w;
    assert!(mid_w > 38.0 && mid_w < 66.0, "the dock capsule shrinks with the slide: {mid_w}");
    model.refresh(now + Duration::from_millis(110));
    model.tick(now + Duration::from_millis(400));
    model.layout(&mut canvas, 1000.0);
    assert!(model.dock()[0].x == 0.0 && model.layout_ref().dock.w == 38.0, "the slide settles on the new slot");

    let mut third = state.clients[0].clone();
    third.address = "0x3".into();
    third.at = [200.0, 0.0];
    state.clients.push(third);
    compositor.set_state(state);
    model.refresh(now + Duration::from_millis(420));
    model.layout(&mut canvas, 1000.0);
    assert!(model.dock().len() == 2 && model.dock()[1].appear < 1.0, "a new dock icon fades in");
}

#[test]
fn bar_workspace_slots() {
    let _instant = InstantAnimations::on();
    let mut state = make_state();
    state.focused_monitor = "DP-1".into();
    state.by_monitor.insert("HDMI-1".into(), MonitorWorkspaces { active_id: 5, workspaces: vec![ws(5, true)] });
    let now = Instant::now();
    let (mut sources, _c) = make_sources(state.clone());
    sources.workspace_slots = 5;
    let mut model = Model::new(sources, now);
    model.refresh(now);
    assert_eq!(model.workspaces().len(), 5, "persistent slots always show five pills");
    assert!(model.workspaces()[1].active && !model.workspaces()[4].active, "the output's own workspace is active");
    assert!(model.workspaces()[0].occupied && model.workspaces()[4].occupied && !model.workspaces()[2].occupied, "occupied covers every output");
    let (mut unknown, _c2) = make_sources(state);
    unknown.workspace_slots = 5;
    unknown.output = "unknown".into();
    let mut fallback = Model::new(unknown, now);
    fallback.refresh(now);
    assert!(fallback.workspaces()[1].active, "an unknown output follows the focused one");
}

#[test]
fn bar_empty_tray() {
    let now = Instant::now();
    let (mut sources, _c) = make_sources(make_state());
    sources.tray_occupied = Some(Rc::new(|| false));
    let mut model = Model::new(sources, now);
    model.refresh(now);
    assert!(!model.item(Item::Tray).visible, "an empty tray is hidden");
    let (mut bare, _c2) = make_sources(make_state());
    bare.tray_occupied = None;
    let mut unsourced = Model::new(bare, now);
    unsourced.refresh(now);
    assert!(!unsourced.item(Item::Tray).visible, "a tray without a source is hidden");
}

#[test]
fn bar_interaction() {
    let _instant = InstantAnimations::on();
    let now = Instant::now();
    let (sources, _c) = make_sources(make_state());
    let mut model = Model::new(sources, now);
    let mut canvas = RecordingCanvas::default();
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);

    assert_eq!(model.item(Item::Time).expand, 1.0, "the time never retracts");
    let collapsed = model.item(Item::Date).rect.w;
    let cx = model.item(Item::Date).rect.x as f64 + 5.0;
    let action = model.press(cx, 20.0);
    assert!(action.kind == ActionKind::Toggle && action.item == Some(Item::Date), "clicking an item toggles it");
    model.hover(Some(cx), Some(20.0), now);
    model.layout(&mut canvas, 1000.0);
    assert!(model.item(Item::Date).expand == 1.0 && model.item(Item::Date).rect.w > collapsed, "hover expands the label");
    model.hover(None, None, now);
    model.layout(&mut canvas, 1000.0);
    assert_eq!(model.item(Item::Date).rect.w, collapsed, "leaving collapses it");

    model.set_open(Some(Item::Tray), now);
    model.layout(&mut canvas, 1000.0);
    assert!(model.item(Item::Tray).expand == 1.0 && model.open() == Some(Item::Tray), "an open panel pins its pill");
    model.set_open(None, now);
    let idle = model.until_idle(now);
    assert!(idle <= Duration::from_millis(80) && idle > Duration::ZERO, "a closed panel lingers briefly");
    assert_eq!(model.item(Item::Tray).expand, 1.0, "the pill stays open during the linger");
    model.tick(now + Duration::from_millis(200));
    assert!(model.item(Item::Tray).expand == 0.0 && model.until_idle(now + Duration::from_millis(200)) == Duration::from_secs(3600), "the linger ends and the model goes idle");

    assert_eq!(model.item(Item::WorkspaceInfo).label, "1 window", "the active workspace's windows are counted");
    let info = model.item(Item::WorkspaceInfo).rect;
    assert_eq!(model.press(info.x as f64 + 5.0, 20.0).kind, ActionKind::Overview, "the workspace info opens the overview");
    let mut state = make_state();
    state.clients.push(CompositorClient { address: "0x2".into(), workspace_id: 2, ..Default::default() });
    state.clients.push(CompositorClient { address: "0x3".into(), workspace_id: 1, ..Default::default() });
    _c.set_state(state);
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    assert_eq!(model.item(Item::WorkspaceInfo).label, "2 windows", "other workspaces' windows are left out");

    model.set_expand_all(true);
    model.tick(now);
    assert!(model.item(Item::Date).expand == 1.0 && model.item(Item::Tray).expand == 1.0, "expand all keeps every label out without hover");
    model.set_expand_all(false);
    model.tick(now);
    assert_eq!(model.item(Item::Date).expand, 0.0, "switching it off retracts them again");

    let second = model.workspaces()[1].rect;
    let action = model.press(second.x as f64 + 2.0, 20.0);
    assert!(action.kind == ActionKind::Workspace && action.workspace == 2, "clicking a workspace pill focuses it");
    assert!(model.clickable(second.x as f64, 20.0) && !model.clickable(330.0, 20.0), "clickable follows the hit test");
}

#[test]
fn model_without_services() {
    let _instant = InstantAnimations::on();
    let now = Instant::now();
    let (mut sources, _c) = make_sources(make_state());
    sources.tray_occupied = None;
    let mut model = Model::new(sources, now);
    let mut canvas = RecordingCanvas::default();
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    for item in [Item::Tray, Item::Resource, Item::Network, Item::Bluetooth, Item::Volume, Item::Brightness, Item::Battery] {
        assert!(!model.item(item).visible, "{item:?} has no source and is hidden");
    }
    for x in (0..1000).step_by(5) {
        let action = model.press(x as f64, 20.0);
        if let Some(item) = action.item {
            assert!(model.item(item).visible, "{item:?} is pressable only when shown");
        }
    }
    assert!(model.layout_ref().status.w == 0.0, "an empty status pill takes no room");
}

#[test]
fn model_without_compositor() {
    let _instant = InstantAnimations::on();
    let now = Instant::now();
    let (sources, _c) = make_sources(CompositorState::default());
    let mut model = Model::new(sources, now);
    let mut canvas = RecordingCanvas::default();
    model.set_style(&style(BarStyle::Continuous, false));
    model.refresh(now);
    model.layout(&mut canvas, 1000.0);
    assert!(model.workspaces().is_empty() && model.dock().is_empty(), "no workspaces and no dock");
    assert!(model.layout_ref().workspaces.w == 0.0 && model.layout_ref().dock.w == 0.0);
    for x in (0..1000).step_by(2) {
        assert_ne!(model.press(x as f64, 20.0).kind, ActionKind::Workspace, "no workspace click at {x}");
    }
}
