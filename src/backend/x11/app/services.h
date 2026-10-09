#pragma once

#include <memory>

#include "core/dbus.h"

#include "service/audio_service.h"
#include "service/battery_service.h"
#include "service/bluetooth_service.h"
#include "service/brightness_service.h"
#include "service/compositor_service.h"
#include "service/media_service.h"
#include "service/network_service.h"
#include "service/notification_service.h"
#include "service/polkit_service.h"
#include "service/settings_service.h"
#include "service/tray_service.h"
#include "service/user_service.h"

#include "core/event_loop.h"
#include "core/x_connection.h"
#include "service/x11/output_service.h"

namespace astralia {

struct Services {
    Services(XConnection &x, EventLoop &loop);
    Services(const Services &) = delete;
    Services &operator=(const Services &) = delete;

    SystemBus system;
    SystemBus session;
    std::unique_ptr<Compositor> compositor;
    NetworkService network;
    BluetoothService bluetooth;
    BatteryService battery;
    BrightnessService brightness;
    NotificationService notifications;
    PolkitService polkit;
    AudioService audio;
    MediaService media;
    TrayService tray;
    OutputService outputs;
    SettingsService settings;
    UserService user;
};

} // namespace astralia
