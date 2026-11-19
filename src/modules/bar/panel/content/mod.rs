//! What each bar panel shows.

pub mod battery_panel;
pub mod bluetooth_panel;
pub mod brightness_panel;
pub mod date_panel;
pub mod media_panel;
pub mod network_panel;
pub mod resource_panel;
pub mod time_panel;
pub mod tray_panel;
pub mod volume_panel;

use std::cell::RefCell;
use std::rc::Rc;

use super::core::panel_set::{PanelId, PanelSet};
use crate::service::audio::AudioService;
use crate::service::battery::BatteryService;
use crate::service::bluetooth::BluetoothService;
use crate::service::brightness::BrightnessService;
use crate::service::media::MediaService;
use crate::service::network::NetworkService;
use crate::service::telemetry::Telemetry;
use crate::service::tray::TrayService;

/// The services the panels read; a missing one leaves its panel out.
#[derive(Clone, Default)]
pub struct PanelSources {
    pub tray: Option<Rc<TrayService>>,
    pub telemetry: Option<Rc<RefCell<Telemetry>>>,
    pub network: Option<Rc<NetworkService>>,
    pub bluetooth: Option<Rc<BluetoothService>>,
    pub audio: Option<Rc<AudioService>>,
    pub media: Option<Rc<MediaService>>,
    pub battery: Option<Rc<BatteryService>>,
    pub brightness: Option<Rc<BrightnessService>>,
    pub output: String,
}

/// Adds every panel whose service is there.
pub fn add_panels(set: &mut PanelSet, sources: &PanelSources) {
    if let Some(tray) = &sources.tray {
        set.add(PanelId::Tray, Box::new(tray_panel::TrayPanel::new(tray.clone())));
    }
    if let Some(telemetry) = &sources.telemetry {
        set.add(PanelId::Resource, Box::new(resource_panel::ResourcePanel::new(telemetry.clone())));
    }
    if let Some(network) = &sources.network {
        set.add(PanelId::Network, Box::new(network_panel::NetworkPanel::new(network.clone())));
    }
    if let Some(bluetooth) = &sources.bluetooth {
        set.add(PanelId::Bluetooth, Box::new(bluetooth_panel::BluetoothPanel::new(bluetooth.clone())));
    }
    if let Some(audio) = &sources.audio {
        set.add(PanelId::Volume, Box::new(volume_panel::VolumePanel::new(audio.clone())));
    }
    if let Some(battery) = &sources.battery {
        set.add(PanelId::Battery, Box::new(battery_panel::BatteryPanel::new(battery.clone())));
    }
    if let Some(media) = &sources.media {
        set.add(PanelId::Media, Box::new(media_panel::MediaPanel::new(media.clone())));
    }
    if let Some(brightness) = &sources.brightness {
        set.add(PanelId::Brightness, Box::new(brightness_panel::BrightnessPanel::new(brightness.clone(), &sources.output)));
    }
    set.add(PanelId::Time, Box::new(time_panel::TimePanel::default()));
    set.add(PanelId::Date, Box::new(date_panel::DatePanel::default()));
}
