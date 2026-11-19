//! Bluetooth devices by state, the power toggle, and the pairing and confirm dialogs.

use std::rc::Rc;

use crate::base::input::{KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::widgets::{self, DeviceRow};
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::text_field::{TextFieldResult, TextFieldState, text_field_backspace, text_field_handle_key, text_field_utf8_len};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics, with_alpha};
use crate::service::bluetooth::{AgentRequestKind, BluetoothDevice, BluetoothService, BluetoothStatus, bluetooth_is_connected, bluetooth_is_nearby, bluetooth_is_paired};

const POWER: i32 = 1;
const DEVICE: i32 = 2;
const FORGET: i32 = 3;
const CANCEL: i32 = 4;
const CONFIRM: i32 = 5;
const AGENT_YES: i32 = 6;
const AGENT_CANCEL: i32 = 7;

const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const PASSKEY_MAX: usize = 6;
const PIN_MAX: usize = 16;

fn input_kind(kind: AgentRequestKind) -> bool {
    matches!(kind, AgentRequestKind::InputPasskey | AgentRequestKind::InputPin)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BluetoothRowKind {
    Message,
    Section,
    Device,
}

#[derive(Clone, Debug)]
pub struct BluetoothRow<'a> {
    pub kind: BluetoothRowKind,
    pub height: f32,
    pub text: &'a str,
    pub device: Option<&'a BluetoothDevice>,
}

pub fn bluetooth_rows<'a>(status: &BluetoothStatus, devices: &'a [BluetoothDevice]) -> Vec<BluetoothRow<'a>> {
    let message = |text, height| BluetoothRow { kind: BluetoothRowKind::Message, height, text, device: None };
    if !status.present {
        return vec![message("No adapter found", cfg::EMPTY_HEIGHT)];
    }
    if !status.powered {
        return vec![message("Bluetooth is off", cfg::EMPTY_HEIGHT)];
    }
    let mut rows = Vec::new();
    let mut bucket = |title: &'a str, belongs: fn(&BluetoothDevice) -> bool| {
        let mut first = true;
        for d in devices.iter().filter(|d| belongs(d)) {
            if std::mem::take(&mut first) {
                rows.push(BluetoothRow { kind: BluetoothRowKind::Section, height: cfg::SECTION_HEIGHT, text: title, device: None });
            }
            rows.push(BluetoothRow { kind: BluetoothRowKind::Device, height: cfg::DEVICE_ROW_HEIGHT, text: &d.name, device: Some(d) });
        }
    };
    bucket("Connected", bluetooth_is_connected);
    bucket("Paired", bluetooth_is_paired);
    if status.scanning {
        bucket("Nearby", bluetooth_is_nearby);
    }
    if rows.is_empty() {
        rows.push(message(if status.scanning { "Searching for devices\u{2026}" } else { "No paired devices" }, cfg::EMPTY_HEIGHT));
    }
    rows.push(message("", cfg::TRAILING_SPACER));
    rows
}

pub struct BluetoothPanel {
    base: ContentBase,
    bluetooth: Rc<BluetoothService>,
    code: TextFieldState,
    dialog_path: String,
    dialog_action: i32,
}

impl BluetoothPanel {
    pub fn new(bluetooth: Rc<BluetoothService>) -> BluetoothPanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        bluetooth.changed.connect(move |_| notify());
        BluetoothPanel { base, bluetooth, code: TextFieldState::default(), dialog_path: String::new(), dialog_action: 0 }
    }

    fn dialog_device(&self) -> Option<BluetoothDevice> {
        if self.dialog_path.is_empty() {
            return None;
        }
        self.bluetooth.devices().iter().find(|d| d.path == self.dialog_path).cloned()
    }

    fn submit_code(&mut self) {
        self.bluetooth.agent_input(&self.code.text);
        self.code = TextFieldState::default();
    }

    fn paint_field(&mut self, canvas: &mut dyn Canvas, r: Rect, name: &str, kind: AgentRequestKind, paint: &mut PanelPaint) {
        let p = palette();
        widgets::dialog_top(canvas, r, name, paint, AGENT_CANCEL);
        let x = r.x + cfg::PADDING;
        let width = r.w - 2.0 * cfg::PADDING;
        let mut y = r.y + cfg::PADDING + cfg::DIALOG_SPACER;
        let field = Rect { x, y, w: width, h: cfg::NETWORK_FIELD_HEIGHT };
        canvas.rounded(field, metrics::RADIUS_WIDGET, p.text_alpha08, 1.0, p.text_alpha15);
        if self.code.text.is_empty() {
            let hint = if kind == AgentRequestKind::InputPasskey { "Passkey\u{2026}" } else { "PIN\u{2026}" };
            widgets::centered_text(canvas, hint, &TEXT_STYLE, field, p.text_dim);
        } else {
            widgets::centered_text(canvas, &self.code.text, &TEXT_STYLE, field, p.text);
        }
        y += cfg::NETWORK_FIELD_HEIGHT + cfg::ROW_GAP;
        let ready = !self.code.text.is_empty();
        let button_w = canvas.measure("Pair", &TEXT_STYLE).w + cfg::DIALOG_BUTTON_PAD;
        let button = Rect { x: x + (width - button_w) / 2.0, y, w: button_w, h: cfg::DIALOG_BUTTON };
        canvas.rounded(button, metrics::RADIUS_WIDGET, with_alpha(p.accent, if ready { 1.0 } else { 0.4 }), 0.0, Color::default());
        widgets::centered_text(canvas, "Pair", &TEXT_STYLE, button, p.text);
        if ready {
            paint.region(button, AGENT_YES);
        }
    }
}

impl PanelContent for BluetoothPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Bluetooth"
    }
    fn tint(&self) -> Color {
        palette().accent
    }
    fn opened(&mut self) {
        self.dialog_path.clear();
        self.code = TextFieldState::default();
        self.bluetooth.start_discovery();
    }
    fn closed(&mut self) {
        self.dialog_path.clear();
        self.code = TextFieldState::default();
        self.bluetooth.cancel_pairing();
        self.bluetooth.stop_discovery();
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        let devices = self.bluetooth.devices();
        let rows = bluetooth_rows(&self.bluetooth.status(), &devices);
        rows.iter().map(|r| r.height).sum::<f32>() + cfg::LIST_SPACING * rows.len().saturating_sub(1) as f32
    }
    fn paint_header(&mut self, canvas: &mut dyn Canvas, area: Rect, paint: &mut PanelPaint) {
        let status = self.bluetooth.status().clone();
        if !status.present {
            return;
        }
        let toggle = Rect { x: area.x + area.w - cfg::TOGGLE_WIDTH, y: area.y + (area.h - cfg::TOGGLE_HEIGHT) / 2.0, w: cfg::TOGGLE_WIDTH, h: cfg::TOGGLE_HEIGHT };
        widgets::toggle(canvas, toggle, status.powered);
        paint.region(toggle, POWER);
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint) {
        let p = palette();
        let status = self.bluetooth.status().clone();
        let devices = self.bluetooth.devices();
        let mut y = view.y - scroll;
        for (i, row) in bluetooth_rows(&status, &devices).into_iter().enumerate() {
            if i > 0 {
                y += cfg::LIST_SPACING;
            }
            let r = Rect { x: view.x, y, w: view.w, h: row.height };
            y += row.height;
            if r.y + r.h < view.y || r.y > view.y + view.h {
                continue;
            }
            let d = match (row.kind, row.device) {
                (BluetoothRowKind::Message, _) => {
                    if !row.text.is_empty() {
                        widgets::centered_text(canvas, row.text, &TEXT_STYLE, r, p.text_dim);
                    }
                    continue;
                }
                (BluetoothRowKind::Section, _) => {
                    let size = canvas.measure(row.text, &TEXT_STYLE);
                    canvas.text(row.text, &TEXT_STYLE, r.x, r.y + r.h - size.h, p.text_dim);
                    continue;
                }
                (BluetoothRowKind::Device, Some(d)) => d,
                (BluetoothRowKind::Device, None) => continue,
            };
            let subtitle = if d.connecting {
                "Connecting\u{2026}".to_string()
            } else if d.battery >= 0 {
                format!("{}%", d.battery)
            } else {
                String::new()
            };
            let mut device_row = DeviceRow {
                glyph: icons::BLUETOOTH_DEVICE,
                title: &d.name,
                subtitle: &subtitle,
                background: if d.connected {
                    p.accent_alpha25
                } else if d.connecting {
                    p.accent_alpha12
                } else {
                    p.text_alpha06
                },
                glyph_color: if d.connected { p.text } else { p.text_dim },
                title_color: if d.connected { p.accent } else { p.text },
                subtitle_color: p.text_dim,
                connected: d.connected,
                busy: d.connecting,
                can_forget: (d.paired || d.trusted) && !d.connected && !d.connecting,
                marquee: None,
                animations: None,
            };
            let index = devices.iter().position(|x| x.path == d.path).map_or(-1, |i| i as i32);
            widgets::device_row(canvas, r, &mut device_row, paint, DEVICE, FORGET, index);
        }
    }
    fn wants_text(&self) -> bool {
        self.bluetooth.agent_request().is_some_and(|r| input_kind(r.kind))
    }
    fn cursor(&self) -> Rect {
        Rect { x: self.width() / 2.0, y: cfg::PADDING + cfg::DIALOG_SPACER + (cfg::NETWORK_FIELD_HEIGHT - 14.0) / 2.0, w: 1.5, h: 14.0 }
    }
    fn dialog_height(&mut self) -> f32 {
        if let Some(request) = self.bluetooth.agent_request() {
            return if input_kind(request.kind) { cfg::FIELD_DIALOG_HEIGHT } else { cfg::PAIRING_DIALOG_HEIGHT };
        }
        self.code = TextFieldState::default();
        if !self.dialog_path.is_empty() && self.dialog_device().is_none() {
            self.dialog_path.clear();
        }
        if self.dialog_path.is_empty() { 0.0 } else { cfg::CONFIRM_DIALOG_HEIGHT }
    }
    fn paint_dialog(&mut self, canvas: &mut dyn Canvas, r: Rect, paint: &mut PanelPaint) {
        if let Some(request) = self.bluetooth.agent_request() {
            match request.kind {
                AgentRequestKind::Confirm => widgets::pairing_dialog(canvas, r, &request.name, &request.code, "Does this match the device?", paint, AGENT_CANCEL, AGENT_YES),
                AgentRequestKind::DisplayPasskey => {
                    let mut hint = "Type it on the device".to_string();
                    if request.entered > 0 {
                        hint += &format!(" ({}/{PASSKEY_MAX})", request.entered);
                    }
                    widgets::pairing_dialog(canvas, r, &request.name, &request.code, &hint, paint, AGENT_CANCEL, 0);
                }
                AgentRequestKind::DisplayPin => widgets::pairing_dialog(canvas, r, &request.name, &request.code, "Type this PIN on the device", paint, AGENT_CANCEL, 0),
                kind => self.paint_field(canvas, r, &request.name, kind, paint),
            }
            return;
        }
        let Some(target) = self.dialog_device() else { return };
        let disconnecting = self.dialog_action == DEVICE;
        let (prompt, label) = if disconnecting { ("Disconnect?", "Disconnect") } else { ("Forget?", "Forget") };
        widgets::confirm_dialog(canvas, r, &target.name, prompt, label, paint, CANCEL, CONFIRM);
    }
    fn dismiss_dialog(&mut self) -> bool {
        if self.bluetooth.agent_request().is_some() {
            self.code = TextFieldState::default();
            self.bluetooth.agent_answer(false);
            return true;
        }
        if self.dialog_path.is_empty() {
            return false;
        }
        self.dialog_path.clear();
        true
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        let device_at = |index: i32| usize::try_from(index).ok().and_then(|i| self.bluetooth.devices().get(i).cloned());
        match region.id {
            POWER => {
                let powered = self.bluetooth.status().powered;
                self.bluetooth.set_powered(!powered);
            }
            DEVICE => {
                let Some(d) = device_at(region.a).filter(|d| !d.connecting) else { return true };
                if d.connected {
                    self.dialog_action = DEVICE;
                    self.dialog_path = d.path;
                } else if d.paired || d.trusted {
                    self.bluetooth.connect(&d.path);
                } else {
                    self.bluetooth.pair(&d.path);
                }
            }
            FORGET => {
                if let Some(d) = device_at(region.a) {
                    self.dialog_action = FORGET;
                    self.dialog_path = d.path;
                }
            }
            AGENT_YES => {
                if self.wants_text() {
                    self.submit_code();
                } else {
                    self.bluetooth.agent_answer(true);
                }
            }
            AGENT_CANCEL => return self.dismiss_dialog(),
            CANCEL => self.dialog_path.clear(),
            CONFIRM => {
                let path = std::mem::take(&mut self.dialog_path);
                if self.dialog_action == DEVICE {
                    self.bluetooth.disconnect(&path);
                } else {
                    self.bluetooth.forget(&path);
                }
            }
            _ => return false,
        }
        true
    }
    fn key(&mut self, event: &KeyEvent) -> bool {
        let Some(request) = self.bluetooth.agent_request().filter(|r| input_kind(r.kind)) else {
            return event.kind == KeyKind::Escape && self.dismiss_dialog();
        };
        let passkey = request.kind == AgentRequestKind::InputPasskey;
        if event.kind == KeyKind::Preedit {
            return true;
        }
        let mut typed = event.clone();
        if passkey && typed.kind == KeyKind::Text {
            typed.text.retain(|c| c.is_ascii_digit());
            if typed.text.is_empty() {
                return true;
            }
        }
        match text_field_handle_key(&mut self.code, &typed) {
            TextFieldResult::Committed => self.submit_code(),
            TextFieldResult::Cancelled => return self.dismiss_dialog(),
            TextFieldResult::Changed => {
                while text_field_utf8_len(&self.code.text) > if passkey { PASSKEY_MAX } else { PIN_MAX } {
                    text_field_backspace(&mut self.code.text);
                }
            }
            TextFieldResult::None => {}
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{connect_to, test_bus};
    use crate::service::bluetooth::{AgentRequestKind, BluetoothAgentRequest};
    use crate::service::runtime;
    use calloop::EventLoop;
    use futures_lite::future::block_on;

    #[test]
    fn pairing_dialog_follows_agent() {
        let Some(bus) = test_bus() else { return };
        let client = block_on(connect_to(bus.address()));
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let service = BluetoothService::start(&client);
        let mut panel = BluetoothPanel::new(service.clone());
        assert_eq!(panel.dialog_height(), 0.0, "no request, no dialog");
        service.hold_request(BluetoothAgentRequest { kind: AgentRequestKind::Confirm, path: "/org/bluez/hci0/dev_X".into(), name: "Buds".into(), code: "123456".into(), entered: 0 });
        assert_eq!(panel.dialog_height(), cfg::PAIRING_DIALOG_HEIGHT, "a confirmation request opens the pairing dialog");
        service.hold_request(BluetoothAgentRequest { kind: AgentRequestKind::InputPin, path: "/org/bluez/hci0/dev_X".into(), name: "Buds".into(), code: String::new(), entered: 0 });
        assert_eq!(panel.dialog_height(), cfg::FIELD_DIALOG_HEIGHT, "an input request opens the field dialog");
        assert!(panel.wants_text());
        service.cancel_pairing();
        assert_eq!(panel.dialog_height(), 0.0, "cancelling the pairing closes it");
    }
}
