#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include <string>
#include <vector>

#include "core/dbus.h"
#include "core/signal.h"

#include "service/network_service.h"

namespace astralia {

enum class BluetoothDeviceKind {
    unknown,
    headset,
    headphones,
    speaker,
    mouse,
    keyboard,
    phone,
    computer,
    gamepad,
    watch,
    tv,
};

struct BluetoothStatus {
    bool present = false;
    bool powered = false;
    bool connected = false;
    std::string device;
    bool scanning = false;
    bool operator==(const BluetoothStatus &) const = default;
};

struct BluetoothDevice {
    std::string path;
    std::string name;
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    bool connecting = false;
    int battery = -1;
    std::string address;
    BluetoothDeviceKind kind = BluetoothDeviceKind::unknown;
    bool operator==(const BluetoothDevice &) const = default;
};

using BluetoothObjects =
    std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>;

struct BluetoothSnapshot {
    BluetoothStatus status;
    std::vector<BluetoothDevice> devices;
    std::string adapter_path;
};

BluetoothDeviceKind bluetooth_classify_icon(const std::string &bluez_icon_name);
BluetoothDeviceKind bluetooth_classify_class(uint32_t class_of_device);
BluetoothSnapshot bluetooth_parse(const BluetoothObjects &objects, const std::set<std::string> &busy);
std::vector<StatusMessage> bluetooth_changes(const BluetoothStatus &prev, const BluetoothStatus &next);

inline bool bluetooth_is_connected(const BluetoothDevice &d) { return d.connected; }
inline bool bluetooth_is_paired(const BluetoothDevice &d) {
    return !d.connected && (d.paired || d.trusted);
}
inline bool bluetooth_is_nearby(const BluetoothDevice &d) {
    return !d.connected && !d.paired && !d.trusted;
}

namespace rfkill {

std::optional<unsigned> read_sysfs_uint(const std::string &path);
std::optional<std::string> read_sysfs_string(const std::string &path);
bool bluetooth_hard_blocked();
bool set_bluetooth_soft_blocked(bool blocked);

} // namespace rfkill

class BluetoothService {
  public:
    explicit BluetoothService(SystemBus &bus);

    const BluetoothStatus &status() const { return status_; }
    const std::vector<BluetoothDevice> &devices() const { return devices_; }

    void set_powered(bool powered);
    void start_discovery();
    void stop_discovery();
    void connect(const std::string &path);
    void disconnect(const std::string &path);
    void pair(const std::string &path);
    void forget(const std::string &path);

    Signal<> changed;
    Signal<const StatusMessage &> messages;

  private:
    void request_refresh();
    void apply(const BluetoothObjects &objects);
    sdbus::IProxy *proxy(const std::string &path);
    void call_device(const std::string &path, const char *method, bool busy);
    void call_adapter(const char *method);

    SystemBus &bus_;
    BluetoothStatus status_;
    std::vector<BluetoothDevice> devices_;
    std::set<std::string> busy_;
    std::string adapter_path_;
    bool initialized_ = false;
    bool refresh_in_flight_ = false;
    bool refresh_again_ = false;
    std::unique_ptr<sdbus::IProxy> root_;
    std::map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    sdbus::Slot match_;
    sdbus::Slot refresh_reply_;
};

} // namespace astralia
