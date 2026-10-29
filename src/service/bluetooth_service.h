#pragma once

#include <cstdint>
#include <functional>
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

struct BluetoothAgentRequest {
    enum class Kind { confirm, display_passkey, display_pin, input_passkey, input_pin } kind;
    std::string path;
    std::string name;
    std::string code;
    unsigned entered = 0;
};

bool bluetooth_agent_allows(const std::string &request_path, const std::string &pairing_path,
                            bool known);
std::string bluetooth_passkey_text(uint32_t passkey);

BluetoothDeviceKind bluetooth_classify_icon(const std::string &bluez_icon_name);
BluetoothDeviceKind bluetooth_classify_class(uint32_t class_of_device);
BluetoothSnapshot bluetooth_parse(const BluetoothObjects &objects,
                                  const std::set<std::string> &busy);
std::vector<StatusMessage> bluetooth_changes(const BluetoothStatus &prev,
                                             const BluetoothStatus &next);

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
    explicit BluetoothService(Bus &bus);

    const BluetoothStatus &status() const { return status_; }
    const std::vector<BluetoothDevice> &devices() const { return devices_; }

    void set_powered(bool powered);
    void start_discovery();
    void stop_discovery();
    void connect(const std::string &path);
    void disconnect(const std::string &path);
    void pair(const std::string &path);
    void forget(const std::string &path);

    const std::optional<BluetoothAgentRequest> &agent_request() const { return agent_request_; }
    void agent_answer(bool accept);
    void agent_input(const std::string &text);
    void cancel_pairing();

    Signal<> changed;
    Signal<const StatusMessage &> messages;

  private:
    void request_refresh();
    void apply(const BluetoothObjects &objects);
    sdbus::IProxy *proxy(const std::string &path);
    void call_device(const std::string &path, const char *method, bool busy,
                     std::function<void(bool)> done = {});
    void call_adapter(const char *method);
    void export_agent();
    void register_agent();
    void trust(const std::string &path);
    void hold(BluetoothAgentRequest request, std::function<void(bool, const std::string &)> reply);
    void finish_agent(bool accept, const std::string &text = {});
    std::string device_name(const std::string &path) const;
    bool device_known(const std::string &path) const;

    Bus &bus_;
    BluetoothStatus status_;
    std::vector<BluetoothDevice> devices_;
    std::set<std::string> busy_;
    std::string adapter_path_;
    bool initialized_ = false;
    bool refresh_in_flight_ = false;
    bool refresh_again_ = false;
    std::unique_ptr<sdbus::IProxy> root_;
    std::unique_ptr<sdbus::IProxy> manager_;
    std::unique_ptr<sdbus::IObject> agent_;
    std::map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    std::string pairing_path_;
    std::optional<BluetoothAgentRequest> agent_request_;
    std::function<void(bool, const std::string &)> agent_reply_;
    bool agent_registered_ = false;
    sdbus::Slot match_;
    sdbus::Slot owner_match_;
    sdbus::Slot refresh_reply_;
};

} // namespace astralia
