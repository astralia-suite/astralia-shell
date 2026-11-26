//! Wi-Fi networks by state and signal, the error and Ethernet banners, and the password and confirm dialogs.

use std::collections::HashMap;
use std::rc::Rc;

use crate::base::input::{KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::widgets::{self, DeviceRow};
use crate::render::canvas::{Canvas, FontFamily, NO_IMAGE, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::marquee::MarqueeTextState;
use crate::render::text_field::{TextFieldResult, TextFieldState, TextFieldTypeAnim, text_field_handle_key, text_field_type_anim_clear, text_field_type_anim_sync, text_field_utf8_len, text_field_wipe};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics, with_alpha};
use crate::service::network::{NetworkInfo, NetworkMap, NetworkService, network_visible_count};

const WIFI: i32 = 1;
const RESCAN: i32 = 2;
const DISMISS_ERROR: i32 = 3;
const CONNECT: i32 = 4;
const FORGET: i32 = 5;
const CANCEL: i32 = 6;
const CONFIRM: i32 = 7;
const OPEN_PORTAL: i32 = 8;

const ANIM_OWNER: u64 = 10000;
const ECHO_FILE: &str = "polkit/electro.png";
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };
const CAPTIVE_BACKGROUND: Color = Color { r: 1.0, g: 0.76, b: 0.03, a: 0.15 };
const CAPTIVE_FOREGROUND: Color = Color { r: 1.0, g: 0.7569, b: 0.0275, a: 1.0 };

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NetworkRowKind {
    Error,
    Ethernet,
    NoAdapter,
    Disabled,
    Scanning,
    SectionConnected,
    SectionKnown,
    SectionAvailable,
    Network,
    Spacer,
}

#[derive(Clone, Copy, Debug)]
pub struct NetworkRow<'a> {
    pub kind: NetworkRowKind,
    pub height: f32,
    pub info: Option<&'a NetworkInfo>,
}

pub struct NetworkRowsInput<'a> {
    pub networks: &'a NetworkMap,
    pub has_error: bool,
    pub ethernet_connected: bool,
    pub wifi_available: bool,
    pub wifi_enabled: bool,
}

fn signal_band(percent: i32) -> i32 {
    match percent {
        p if p > 75 => 3,
        p if p > 50 => 2,
        p if p > 25 => 1,
        _ => 0,
    }
}

pub fn network_signal_glyph(percent: i32) -> &'static str {
    match signal_band(percent) {
        3 => icons::WIFI,
        2 => icons::WIFI2,
        1 => icons::WIFI1,
        _ => icons::WIFI0,
    }
}

fn secured(info: &NetworkInfo) -> bool {
    !info.security.is_empty() && info.security != "--"
}

/// `text` cut to `max` characters, the last one an ellipsis.
fn elide(text: &str, max: usize) -> String {
    if text.chars().count() <= max { text.to_string() } else { text.chars().take(max - 1).chain(['\u{2026}']).collect() }
}

pub fn network_rows<'a>(input: &NetworkRowsInput<'a>) -> Vec<NetworkRow<'a>> {
    let row = |kind, height| NetworkRow { kind, height, info: None };
    let mut rows = Vec::new();
    if input.has_error {
        rows.push(row(NetworkRowKind::Error, cfg::NETWORK_BANNER));
    }
    if input.ethernet_connected {
        rows.push(row(NetworkRowKind::Ethernet, cfg::NETWORK_ETHERNET));
    }
    if !input.wifi_available {
        rows.push(row(NetworkRowKind::NoAdapter, cfg::NETWORK_STATE));
    } else if !input.wifi_enabled {
        rows.push(row(NetworkRowKind::Disabled, cfg::NETWORK_STATE));
    }
    if input.wifi_available && input.wifi_enabled && network_visible_count(input.networks) == 0 {
        rows.push(row(NetworkRowKind::Scanning, cfg::NETWORK_SCANNING));
    }
    let (mut connected, mut saved, mut available) = (Vec::new(), Vec::new(), Vec::new());
    for info in input.networks.values() {
        if info.connected {
            connected.push(info);
        } else if info.existing && info.in_range {
            saved.push(info);
        } else if !info.existing {
            available.push(info);
        }
    }
    for (section, gap, mut list) in [
        (NetworkRowKind::SectionConnected, cfg::NETWORK_SECTION_FIRST, connected),
        (NetworkRowKind::SectionKnown, cfg::NETWORK_SECTION, saved),
        (NetworkRowKind::SectionAvailable, cfg::NETWORK_SECTION, available),
    ] {
        if list.is_empty() {
            continue;
        }
        rows.push(row(section, gap));
        list.sort_by(|a, b| signal_band(b.signal).cmp(&signal_band(a.signal)).then(b.signal.cmp(&a.signal)));
        rows.extend(list.into_iter().map(|info| NetworkRow { kind: NetworkRowKind::Network, height: cfg::DEVICE_ROW_HEIGHT, info: Some(info) }));
    }
    rows.push(row(NetworkRowKind::Spacer, cfg::TRAILING_SPACER));
    rows
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Dialog {
    None,
    Password,
    Disconnect,
    Forget,
}

pub struct NetworkPanel {
    base: ContentBase,
    network: Rc<NetworkService>,
    dialog: Dialog,
    ssid: String,
    password: TextFieldState,
    password_anim: TextFieldTypeAnim,
    marquees: HashMap<String, MarqueeTextState>,
}

impl NetworkPanel {
    pub fn new(network: Rc<NetworkService>) -> NetworkPanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        network.changed.connect(move |_| notify());
        NetworkPanel { base, network, dialog: Dialog::None, ssid: String::new(), password: TextFieldState::default(), password_anim: TextFieldTypeAnim::default(), marquees: HashMap::new() }
    }

    /// Switches the dialog, wiping whatever password was typed.
    fn open_dialog(&mut self, dialog: Dialog, ssid: &str) {
        text_field_wipe(&mut self.password.text);
        self.password.preedit.clear();
        text_field_type_anim_clear(&mut self.password_anim, &mut self.base.animations, ANIM_OWNER);
        self.dialog = dialog;
        self.ssid = ssid.to_string();
    }

    fn submit(&mut self) {
        if text_field_utf8_len(&self.password.text) < cfg::NETWORK_PASSWORD_MIN as usize {
            return;
        }
        self.network.connect(&self.ssid, &self.password.text);
        self.open_dialog(Dialog::None, "");
    }

    fn paint_network(&mut self, canvas: &mut dyn Canvas, r: Rect, info: &NetworkInfo, index: i32, paint: &mut PanelPaint) {
        let p = palette();
        let portal = info.connected && (self.network.connectivity() == "portal" || self.network.status().portal);
        let busy = self.network.connecting_to() == info.ssid;
        let pick = |captive: Color, connected: Color, other: Color| if portal { captive } else if info.connected { connected } else { other };
        let mut row = DeviceRow {
            glyph: network_signal_glyph(info.signal),
            title: &info.ssid,
            subtitle: if portal { "Sign in required" } else if secured(info) { &info.security } else { "Open" },
            background: if portal {
                CAPTIVE_BACKGROUND
            } else if info.connected {
                p.accent_alpha25
            } else if busy {
                p.accent_alpha12
            } else {
                p.text_alpha06
            },
            glyph_color: pick(CAPTIVE_FOREGROUND, p.accent, p.text_dim),
            title_color: pick(CAPTIVE_FOREGROUND, p.accent, p.text),
            subtitle_color: if portal { CAPTIVE_FOREGROUND } else { p.text_dim },
            connected: info.connected,
            busy,
            can_forget: info.existing && !info.connected && !busy,
            marquee: Some(self.marquees.entry(info.ssid.clone()).or_default()),
            animations: Some(&mut self.base.animations),
        };
        widgets::device_row(canvas, r, &mut row, paint, CONNECT, FORGET, index);
        if portal {
            paint.region(r, OPEN_PORTAL);
        }
    }

    fn paint_password(&mut self, canvas: &mut dyn Canvas, r: Rect, paint: &mut PanelPaint) {
        let p = palette();
        widgets::dialog_top(canvas, r, &self.ssid, paint, CANCEL);
        let x = r.x + cfg::PADDING;
        let width = r.w - 2.0 * cfg::PADDING;
        let mut y = r.y + cfg::PADDING + cfg::DIALOG_SPACER;
        let field = Rect { x, y, w: width, h: cfg::NETWORK_FIELD_HEIGHT };
        canvas.rounded(field, metrics::RADIUS_WIDGET, p.text_alpha08, 1.0, p.text_alpha15);
        const DOT: f32 = 8.0;
        const GAP: f32 = 4.0;
        let cy = field.y + field.h / 2.0;
        let count = text_field_utf8_len(&self.password.text);
        if count == 0 && self.password.preedit.is_empty() {
            widgets::centered_text(canvas, "Password\u{2026}", &TEXT_STYLE, field, p.text_dim);
        } else {
            let echo = canvas.image(ECHO_FILE, DOT as i32);
            let visible = count.min(((width - 2.0 * cfg::ROW_ICON_GAP + GAP) / (DOT + GAP)).max(1.0) as usize);
            let row = visible as f32 * DOT + visible.saturating_sub(1) as f32 * GAP;
            let dx = field.x + (field.w - row) / 2.0;
            let first = count - visible;
            for i in 0..visible {
                let anim = self.password_anim.chars.get(first + i);
                let scale = anim.map_or(1.0, |a| a.scale.get());
                let slide = anim.map_or(0.0, |a| a.slide_x.get());
                let size = DOT * scale;
                let d = Rect { x: dx + i as f32 * (DOT + GAP) + (DOT - size) / 2.0 + slide, y: cy - size / 2.0, w: size, h: size };
                if echo != NO_IMAGE {
                    canvas.draw_image(echo, d, WHITE);
                } else {
                    canvas.rounded(d, size / 2.0, p.text, 0.0, Color::default());
                }
            }
            if !self.password.preedit.is_empty() {
                let size = canvas.measure(&self.password.preedit, &TEXT_STYLE);
                canvas.text(&self.password.preedit, &TEXT_STYLE, field.x + (field.w - size.w) / 2.0, field.y + field.h + 10.0, p.text_dim);
            }
        }
        y += cfg::NETWORK_FIELD_HEIGHT + cfg::ROW_GAP;
        let ready = count >= cfg::NETWORK_PASSWORD_MIN as usize;
        let button_w = canvas.measure("Connect", &TEXT_STYLE).w + cfg::DIALOG_BUTTON_PAD;
        let button = Rect { x: x + (width - button_w) / 2.0, y, w: button_w, h: cfg::DIALOG_BUTTON };
        canvas.rounded(button, metrics::RADIUS_WIDGET, with_alpha(p.accent, if ready { 1.0 } else { 0.4 }), 0.0, Color::default());
        widgets::centered_text(canvas, "Connect", &TEXT_STYLE, button, p.text);
        if ready {
            paint.region(button, CONFIRM);
        }
    }
}

impl PanelContent for NetworkPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Network"
    }
    fn tint(&self) -> Color {
        palette().pop_c
    }
    fn opened(&mut self) {
        self.open_dialog(Dialog::None, "");
        self.network.start_watch();
    }
    fn closed(&mut self) {
        self.open_dialog(Dialog::None, "");
        self.network.stop_watch();
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        let networks = self.network.networks();
        let input = NetworkRowsInput { networks: &networks, has_error: !self.network.last_error().is_empty(), ethernet_connected: self.network.ethernet_connected(), wifi_available: self.network.wifi_available(), wifi_enabled: self.network.wifi_enabled() };
        let rows = network_rows(&input);
        rows.iter().map(|r| r.height).sum::<f32>() + cfg::LIST_SPACING * rows.len().saturating_sub(1) as f32
    }
    fn paint_header(&mut self, canvas: &mut dyn Canvas, area: Rect, paint: &mut PanelPaint) {
        if !self.network.wifi_available() {
            return;
        }
        let toggle = Rect { x: area.x + area.w - cfg::TOGGLE_WIDTH, y: area.y + (area.h - cfg::TOGGLE_HEIGHT) / 2.0, w: cfg::TOGGLE_WIDTH, h: cfg::TOGGLE_HEIGHT };
        widgets::toggle(canvas, toggle, self.network.wifi_enabled());
        paint.region(toggle, WIFI);
        let size = cfg::CLOSE_BUTTON;
        let scan = Rect { x: toggle.x - cfg::ROW_GAP - size, y: area.y + (area.h - size) / 2.0, w: size, h: size };
        widgets::icon_button(canvas, scan, icons::REFRESH, if self.network.scanning() { palette().accent } else { palette().text });
        paint.region(scan, RESCAN);
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint) {
        let p = palette();
        let network = self.network.clone();
        let networks = network.networks();
        let animations = &mut self.base.animations;
        self.marquees.retain(|ssid, marquee| {
            let keep = networks.contains_key(ssid);
            if !keep {
                animations.cancel_owner(marquee.owner);
            }
            keep
        });
        let last_error = network.last_error();
        let input = NetworkRowsInput { networks: &networks, has_error: !last_error.is_empty(), ethernet_connected: network.ethernet_connected(), wifi_available: network.wifi_available(), wifi_enabled: network.wifi_enabled() };
        let mut y = view.y - scroll;
        for (i, row) in network_rows(&input).into_iter().enumerate() {
            if i > 0 {
                y += cfg::LIST_SPACING;
            }
            let r = Rect { x: view.x, y, w: view.w, h: row.height };
            y += row.height;
            if r.y + r.h < view.y || r.y > view.y + view.h {
                continue;
            }
            match row.kind {
                NetworkRowKind::Error => {
                    canvas.rounded(r, metrics::RADIUS_WIDGET, p.critical_alpha15, 0.0, Color::default());
                    let glyph = canvas.measure(icons::ALERT_TRIANGLE, &ICON_STYLE);
                    let mut text_x = r.x + cfg::ROW_ICON_GAP;
                    canvas.text(icons::ALERT_TRIANGLE, &ICON_STYLE, text_x, r.y + (r.h - glyph.h) / 2.0, p.critical);
                    text_x += glyph.w + cfg::ROW_GAP;
                    widgets::text_in_row(canvas, &elide(&last_error, cfg::NETWORK_ERROR_CHARS as usize), &TEXT_STYLE, text_x, r, p.critical);
                    let size = cfg::NETWORK_BANNER_CLOSE;
                    let close = Rect { x: r.x + r.w - cfg::ROW_ICON_GAP - size, y: r.y + (r.h - size) / 2.0, w: size, h: size };
                    widgets::centered_text(canvas, icons::CLOSE, &ICON_STYLE, close, p.critical);
                    paint.region(close, DISMISS_ERROR);
                }
                NetworkRowKind::Ethernet => {
                    canvas.rounded(r, metrics::RADIUS_WIDGET, p.accent_alpha19, 0.0, Color::default());
                    widgets::centered_text(canvas, "Ethernet", &TEXT_STYLE, r, p.text);
                }
                NetworkRowKind::NoAdapter => widgets::centered_text(canvas, "No Wi-Fi adapter found", &TEXT_STYLE, r, p.text_dim),
                NetworkRowKind::Disabled => widgets::centered_text(canvas, "WiFi is disabled", &TEXT_STYLE, r, p.text_dim),
                NetworkRowKind::Scanning => widgets::centered_text(canvas, "Scanning\u{2026}", &TEXT_STYLE, r, p.text_dim),
                NetworkRowKind::SectionConnected | NetworkRowKind::SectionKnown | NetworkRowKind::SectionAvailable => {
                    let label = match row.kind {
                        NetworkRowKind::SectionConnected => "Connected",
                        NetworkRowKind::SectionKnown => "Known",
                        _ => "Available",
                    };
                    let size = canvas.measure(label, &TEXT_STYLE);
                    canvas.text(label, &TEXT_STYLE, r.x, r.y + r.h - size.h, p.text_dim);
                }
                NetworkRowKind::Network => {
                    if let Some(info) = row.info {
                        let index = networks.keys().position(|k| *k == info.ssid).map_or(-1, |i| i as i32);
                        self.paint_network(canvas, r, info, index, paint);
                    }
                }
                NetworkRowKind::Spacer => {}
            }
        }
    }
    fn dialog_height(&mut self) -> f32 {
        match self.dialog {
            Dialog::None => 0.0,
            Dialog::Password => cfg::FIELD_DIALOG_HEIGHT,
            Dialog::Disconnect | Dialog::Forget => cfg::CONFIRM_DIALOG_HEIGHT,
        }
    }
    fn paint_dialog(&mut self, canvas: &mut dyn Canvas, r: Rect, paint: &mut PanelPaint) {
        match self.dialog {
            Dialog::Disconnect | Dialog::Forget => {
                let leaving = self.dialog == Dialog::Disconnect;
                let (prompt, label) = if leaving { ("Disconnect?", "Disconnect") } else { ("Forget?", "Forget") };
                widgets::confirm_dialog(canvas, r, &self.ssid, prompt, label, paint, CANCEL, CONFIRM);
            }
            Dialog::Password => self.paint_password(canvas, r, paint),
            Dialog::None => {}
        }
    }
    fn dismiss_dialog(&mut self) -> bool {
        if self.dialog == Dialog::None {
            return false;
        }
        self.open_dialog(Dialog::None, "");
        true
    }
    fn wants_text(&self) -> bool {
        self.dialog == Dialog::Password
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        match region.id {
            WIFI => self.network.set_wifi_enabled(!self.network.wifi_enabled()),
            RESCAN => self.network.scan(),
            DISMISS_ERROR => self.network.clear_error(),
            OPEN_PORTAL => self.network.open_portal(),
            CONNECT | FORGET => {
                let Some(info) = usize::try_from(region.a).ok().and_then(|i| self.network.networks().values().nth(i).cloned()) else { return true };
                if region.id == FORGET {
                    self.open_dialog(Dialog::Forget, &info.ssid);
                } else if info.connected {
                    self.open_dialog(Dialog::Disconnect, &info.ssid);
                } else if info.existing || !secured(&info) {
                    self.network.connect(&info.ssid, "");
                } else {
                    self.open_dialog(Dialog::Password, &info.ssid);
                }
            }
            CANCEL => self.open_dialog(Dialog::None, ""),
            CONFIRM => {
                if self.dialog == Dialog::Password {
                    self.submit();
                } else {
                    let (ssid, dialog) = (self.ssid.clone(), self.dialog);
                    self.open_dialog(Dialog::None, "");
                    match dialog {
                        Dialog::Disconnect => self.network.disconnect(&ssid),
                        Dialog::Forget => self.network.forget(&ssid),
                        _ => {}
                    }
                }
            }
            _ => return false,
        }
        true
    }
    fn key(&mut self, event: &KeyEvent) -> bool {
        if self.dialog == Dialog::Password {
            match text_field_handle_key(&mut self.password, event) {
                TextFieldResult::Committed => self.submit(),
                TextFieldResult::Cancelled => self.open_dialog(Dialog::None, ""),
                TextFieldResult::Changed => text_field_type_anim_sync(&mut self.password_anim, &mut self.base.animations, ANIM_OWNER, &self.password.text),
                TextFieldResult::None => {}
            }
            return true;
        }
        event.kind == KeyKind::Escape && self.dismiss_dialog()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service::network::{NetworkInfo, NetworkMap};

    #[test]
    fn network_rows_order() {
        let mut map = NetworkMap::new();
        let mut add = |ssid: &str, signal: i32, connected: bool, existing: bool, in_range: bool| {
            map.insert(ssid.into(), NetworkInfo { ssid: ssid.into(), signal, connected, existing, in_range, security: "WPA2".into() });
        };
        add("Home", 80, true, true, true);
        add("Cafe", 60, false, false, true);
        add("Hotel", 90, false, false, true);
        add("Far", 20, false, true, false);
        add("Office", 40, false, true, true);

        let input = |networks, has_error, ethernet_connected, wifi_available, wifi_enabled| NetworkRowsInput { networks, has_error, ethernet_connected, wifi_available, wifi_enabled };
        let rows = network_rows(&input(&map, false, false, true, true));
        let order: Vec<&str> = rows.iter().filter(|r| r.kind == NetworkRowKind::Network).filter_map(|r| r.info).map(|i| i.ssid.as_str()).collect();
        assert_eq!(order, ["Home", "Office", "Hotel", "Cafe"], "connected, then known in range, then available by signal; out of range saved ones are hidden");
        assert!(rows.first().is_some_and(|r| r.kind == NetworkRowKind::SectionConnected) && rows.last().is_some_and(|r| r.kind == NetworkRowKind::Spacer), "sections open the list and a spacer closes it");

        let empty = NetworkMap::new();
        let rows = network_rows(&input(&empty, true, true, true, true));
        assert!(rows[0].kind == NetworkRowKind::Error && rows[1].kind == NetworkRowKind::Ethernet && rows[2].kind == NetworkRowKind::Scanning, "banners come first, then the scanning state");
        assert_eq!(network_rows(&input(&empty, false, false, false, false))[0].kind, NetworkRowKind::NoAdapter, "no adapter");
        assert_eq!(network_rows(&input(&empty, false, false, true, false))[0].kind, NetworkRowKind::Disabled, "wifi disabled");
        assert_ne!(network_signal_glyph(90), network_signal_glyph(10), "signal glyphs differ by band");
    }
}
