#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

#include "core/dbus.h"
#include "core/signal.h"

namespace astralia {

inline constexpr uint32_t upower_type_battery = 2;
inline constexpr uint32_t upower_type_monitor = 4;
inline constexpr uint32_t upower_state_charging = 1;
inline constexpr uint32_t upower_state_discharging = 2;
inline constexpr uint32_t upower_state_fully_charged = 4;
inline constexpr uint32_t upower_state_pending_charge = 5;

struct BatteryStatus {
    bool present = false;
    int percent = 0;
    bool charging = false;
    bool full = false;
    bool pending = false;
    int seconds_left = 0;

    bool operator==(const BatteryStatus &) const = default;
};

struct BatteryDevice {
    std::string path;
    std::string native_path;
    uint32_t type = 0;
    bool present = false;
    uint32_t state = 0;
    int percent = 0;
    int time_to_empty_s = 0;
    int time_to_full_s = 0;

    bool is_battery() const { return type == upower_type_battery || type == upower_type_monitor; }
    bool operator==(const BatteryDevice &) const = default;
};

BatteryStatus battery_parse_status(const DbusProperties &properties);
BatteryDevice battery_parse_device(const std::string &path, const DbusProperties &properties);

class BatteryService {
  public:
    explicit BatteryService(SystemBus &bus);

    const BatteryStatus &status() const { return status_; }
    const std::vector<BatteryDevice> &devices() const { return devices_; }
    bool on_battery() const { return on_battery_; }

    Signal<> changed;

  private:
    struct Tracked {
        BatteryDevice device;
        std::unique_ptr<sdbus::IProxy> proxy;
        sdbus::Slot reply;
    };

    void refresh_display();
    void refresh_manager();
    void refresh_device(const std::string &path);
    void add_device(const std::string &path);
    void remove_device(const std::string &path);
    void publish_devices();

    SystemBus &bus_;
    BatteryStatus status_;
    bool on_battery_ = false;
    std::vector<BatteryDevice> devices_;
    std::unique_ptr<sdbus::IProxy> manager_;
    std::unique_ptr<sdbus::IProxy> display_;
    std::map<std::string, Tracked> tracked_;
    sdbus::Slot display_reply_;
    sdbus::Slot manager_reply_;
    sdbus::Slot enumerate_reply_;
};

} // namespace astralia
