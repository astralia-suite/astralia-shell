#include <cmath>
#include <cstdint>
#include <vector>

#include "core/log.h"

#include "service/battery_service.h"

namespace astralia {

namespace {

const std::string upower_service = "org.freedesktop.UPower";
const std::string manager_path = "/org/freedesktop/UPower";
const std::string display_path = "/org/freedesktop/UPower/devices/DisplayDevice";
const std::string manager_interface = "org.freedesktop.UPower";
const std::string device_interface = "org.freedesktop.UPower.Device";
const std::string properties_interface = "org.freedesktop.DBus.Properties";

using Changed = const std::string &;
using Changes = const std::map<std::string, sdbus::Variant> &;
using Invalidated = const std::vector<std::string> &;

} // namespace

BatteryStatus battery_parse_status(const DbusProperties &properties) {
    BatteryStatus next;
    next.present = dbus_value<bool>(properties, "IsPresent").value_or(false);
    if (!next.present) {
        return next;
    }
    next.percent = static_cast<int>(std::lround(dbus_value<double>(properties, "Percentage").value_or(0)));
    uint32_t state = dbus_value<uint32_t>(properties, "State").value_or(0);
    next.charging = state == upower_state_charging;
    next.full = state == upower_state_fully_charged;
    next.pending = state == upower_state_pending_charge;
    next.seconds_left = static_cast<int>(
        dbus_value<int64_t>(properties, next.charging ? "TimeToFull" : "TimeToEmpty").value_or(0));
    return next;
}

BatteryDevice battery_parse_device(const std::string &path, const DbusProperties &properties) {
    BatteryDevice device;
    device.path = path;
    device.type = dbus_value<uint32_t>(properties, "Type").value_or(0);
    device.present = dbus_value<bool>(properties, "IsPresent").value_or(false);
    device.state = dbus_value<uint32_t>(properties, "State").value_or(0);
    device.percent = static_cast<int>(std::lround(dbus_value<double>(properties, "Percentage").value_or(0)));
    device.time_to_empty_s = static_cast<int>(dbus_value<int64_t>(properties, "TimeToEmpty").value_or(0));
    device.time_to_full_s = static_cast<int>(dbus_value<int64_t>(properties, "TimeToFull").value_or(0));
    device.native_path = dbus_value<std::string>(properties, "NativePath").value_or("");
    return device;
}

BatteryService::BatteryService(SystemBus &bus) : bus_(bus) {
    manager_ = bus_.proxy(upower_service, manager_path);
    display_ = bus_.proxy(upower_service, display_path);
    if (!manager_ || !display_) {
        return;
    }
    display_->uponSignal("PropertiesChanged")
        .onInterface(properties_interface)
        .call([this](Changed, Changes, Invalidated) { refresh_display(); });
    manager_->uponSignal("PropertiesChanged")
        .onInterface(properties_interface)
        .call([this](Changed, Changes, Invalidated) { refresh_manager(); });
    manager_->uponSignal("DeviceAdded")
        .onInterface(manager_interface)
        .call([this](const sdbus::ObjectPath &path) { add_device(path); });
    manager_->uponSignal("DeviceRemoved")
        .onInterface(manager_interface)
        .call([this](const sdbus::ObjectPath &path) { remove_device(path); });
    refresh_display();
    refresh_manager();
    try {
        enumerate_reply_ = manager_->callMethodAsync("EnumerateDevices")
                               .onInterface(manager_interface)
                               .uponReplyInvoke(
                                   [this](std::optional<sdbus::Error> error,
                                          std::vector<sdbus::ObjectPath> paths) {
                                       if (error) {
                                           log::error("upower: EnumerateDevices failed: {}",
                                                      error->getMessage());
                                           return;
                                       }
                                       for (const sdbus::ObjectPath &path : paths) {
                                           add_device(path);
                                       }
                                   },
                                   sdbus::return_slot);
    } catch (const sdbus::Error &error) {
        log::error("upower: EnumerateDevices dispatch failed: {}", error.getMessage());
    }
}

void BatteryService::refresh_display() {
    display_reply_ = dbus_get_all_async(*display_, device_interface, [this](const DbusProperties &properties) {
        BatteryStatus next = battery_parse_status(properties);
        if (next == status_) {
            return;
        }
        status_ = next;
        changed.emit();
    });
}

void BatteryService::refresh_manager() {
    manager_reply_ = dbus_get_all_async(*manager_, manager_interface, [this](const DbusProperties &properties) {
        bool next = dbus_value<bool>(properties, "OnBattery").value_or(false);
        if (next == on_battery_) {
            return;
        }
        on_battery_ = next;
        changed.emit();
    });
}

void BatteryService::add_device(const std::string &path) {
    if (tracked_.contains(path)) {
        return;
    }
    Tracked entry;
    entry.device.path = path;
    entry.proxy = bus_.proxy(upower_service, path);
    if (!entry.proxy) {
        return;
    }
    entry.proxy->uponSignal("PropertiesChanged")
        .onInterface(properties_interface)
        .call([this, path](Changed, Changes, Invalidated) { refresh_device(path); });
    tracked_.emplace(path, std::move(entry));
    refresh_device(path);
}

void BatteryService::remove_device(const std::string &path) {
    if (tracked_.erase(path) > 0) {
        publish_devices();
    }
}

void BatteryService::refresh_device(const std::string &path) {
    auto it = tracked_.find(path);
    if (it == tracked_.end()) {
        return;
    }
    it->second.reply = dbus_get_all_async(*it->second.proxy, device_interface, [this, path](const DbusProperties &properties) {
        auto found = tracked_.find(path);
        if (found == tracked_.end()) {
            return;
        }
        BatteryDevice next = battery_parse_device(path, properties);
        if (next == found->second.device) {
            return;
        }
        found->second.device = next;
        publish_devices();
    });
}

void BatteryService::publish_devices() {
    std::vector<BatteryDevice> next;
    for (const auto &[path, entry] : tracked_) {
        next.push_back(entry.device);
    }
    if (next == devices_) {
        return;
    }
    devices_ = std::move(next);
    changed.emit();
}

} // namespace astralia
