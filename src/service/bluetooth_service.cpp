#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/rfkill.h>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "core/log.h"

#include "service/bluetooth_service.h"

namespace astralia {

namespace {

using Properties = std::map<std::string, sdbus::Variant>;

const std::string bluez = "org.bluez";
const std::string adapter_interface = "org.bluez.Adapter1";
const std::string device_interface = "org.bluez.Device1";
const std::string battery_interface = "org.bluez.Battery1";
const std::string object_manager_interface = "org.freedesktop.DBus.ObjectManager";
const std::string agent_manager_interface = "org.bluez.AgentManager1";
const std::string agent_interface = "org.bluez.Agent1";

constexpr const char *agent_path = "/org/astralia_shell/BluetoothAgent";
constexpr const char *agent_capability = "KeyboardDisplay";
constexpr const char *rejected_error = "org.bluez.Error.Rejected";
constexpr const char *already_exists_error = "org.bluez.Error.AlreadyExists";
constexpr const char *owner_changed_rule =
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
    "member='NameOwnerChanged',arg0='org.bluez'";
constexpr size_t passkey_digits = 6;
constexpr size_t pin_max_chars = 16;

sdbus::Error rejection(const char *why = "Rejected") {
    return sdbus::Error(sdbus::Error::Name(rejected_error), why);
}

template <typename T>
std::optional<T> property(const Properties &properties, const std::string &name) {
    auto it = properties.find(name);
    if (it == properties.end() || !it->second.containsValueOfType<T>()) {
        return std::nullopt;
    }
    return it->second.get<T>();
}

bool flag(const Properties &properties, const std::string &name) {
    return property<bool>(properties, name).value_or(false);
}

std::string connected_device(const BluetoothStatus &status) {
    return status.connected ? status.device : std::string();
}

struct RfkillEntry {
    bool soft = false;
    bool hard = false;
};

std::vector<RfkillEntry> rfkill_bluetooth_entries() {
    std::vector<RfkillEntry> entries;
    DIR *dir = opendir("/sys/class/rfkill");
    if (!dir) {
        return entries;
    }
    while (dirent *ent = readdir(dir)) {
        std::string name = ent->d_name;
        if (!name.starts_with("rfkill")) {
            continue;
        }
        std::string base = "/sys/class/rfkill/" + name + "/";
        auto index = rfkill::read_sysfs_uint(base + "index");
        auto type = rfkill::read_sysfs_string(base + "type");
        if (!index || !type || *type != "bluetooth") {
            continue;
        }
        entries.push_back({rfkill::read_sysfs_uint(base + "soft").value_or(0) != 0,
                           rfkill::read_sysfs_uint(base + "hard").value_or(0) != 0});
    }
    closedir(dir);
    return entries;
}

} // namespace

namespace rfkill {

std::optional<unsigned> read_sysfs_uint(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "r");
    if (!file) {
        return std::nullopt;
    }
    unsigned value = 0;
    bool ok = std::fscanf(file, "%u", &value) == 1;
    std::fclose(file);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> read_sysfs_string(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "r");
    if (!file) {
        return std::nullopt;
    }
    char buffer[32]{};
    bool ok = std::fscanf(file, "%31s", buffer) == 1;
    std::fclose(file);
    if (!ok) {
        return std::nullopt;
    }
    return std::string(buffer);
}

bool bluetooth_hard_blocked() {
    for (const RfkillEntry &entry : rfkill_bluetooth_entries()) {
        if (entry.hard) {
            return true;
        }
    }
    return false;
}

bool set_bluetooth_soft_blocked(bool blocked) {
    std::vector<RfkillEntry> entries = rfkill_bluetooth_entries();
    if (entries.empty()) {
        return false;
    }
    bool already = true;
    for (const RfkillEntry &entry : entries) {
        if (entry.hard) {
            return false;
        }
        if (entry.soft != blocked) {
            already = false;
        }
    }
    if (already) {
        return true;
    }
    int fd = open("/dev/rfkill", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        log::error("rfkill: open /dev/rfkill: {}", std::strerror(errno));
        return false;
    }
    rfkill_event event{};
    event.type = RFKILL_TYPE_BLUETOOTH;
    event.op = RFKILL_OP_CHANGE_ALL;
    event.soft = blocked ? 1 : 0;
    ssize_t written = 0;
    do {
        written = write(fd, &event, sizeof event);
    } while (written < 0 && errno == EINTR);
    int write_errno = errno;
    close(fd);
    if (written != static_cast<ssize_t>(sizeof event)) {
        log::error("rfkill: write /dev/rfkill: {}",
                   written < 0 ? std::strerror(write_errno) : "short write");
        return false;
    }
    return true;
}

} // namespace rfkill

bool bluetooth_agent_allows(const std::string &request_path, const std::string &pairing_path,
                            bool known) {
    return known || (!pairing_path.empty() && request_path == pairing_path);
}

std::string bluetooth_passkey_text(uint32_t passkey) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%06u", passkey % 1000000u);
    return buffer;
}

BluetoothDeviceKind bluetooth_classify_icon(const std::string &name) {
    if (name == "audio-headset") {
        return BluetoothDeviceKind::headset;
    }
    if (name == "audio-headphones") {
        return BluetoothDeviceKind::headphones;
    }
    if (name == "audio-card" || name == "audio-speakers") {
        return BluetoothDeviceKind::speaker;
    }
    if (name == "input-mouse") {
        return BluetoothDeviceKind::mouse;
    }
    if (name == "input-keyboard") {
        return BluetoothDeviceKind::keyboard;
    }
    if (name == "input-gaming") {
        return BluetoothDeviceKind::gamepad;
    }
    if (name == "phone") {
        return BluetoothDeviceKind::phone;
    }
    if (name == "computer") {
        return BluetoothDeviceKind::computer;
    }
    if (name == "video-display") {
        return BluetoothDeviceKind::tv;
    }
    return BluetoothDeviceKind::unknown;
}

BluetoothDeviceKind bluetooth_classify_class(uint32_t class_of_device) {
    uint32_t major = (class_of_device >> 8) & 0x1F;
    uint32_t minor = (class_of_device >> 2) & 0x3F;
    switch (major) {
    case 0x01:
        return BluetoothDeviceKind::computer;
    case 0x02:
        return BluetoothDeviceKind::phone;
    case 0x04:
        switch (minor) {
        case 0x01:
        case 0x02:
            return BluetoothDeviceKind::headset;
        case 0x06:
            return BluetoothDeviceKind::headphones;
        case 0x05:
        case 0x07:
            return BluetoothDeviceKind::speaker;
        case 0x0A:
        case 0x0B:
            return BluetoothDeviceKind::tv;
        default:
            return BluetoothDeviceKind::headphones;
        }
    case 0x05:
        switch (minor & 0x0F) {
        case 0x01:
            return BluetoothDeviceKind::keyboard;
        case 0x02:
            return BluetoothDeviceKind::mouse;
        default:
            return BluetoothDeviceKind::unknown;
        }
    case 0x07:
        return BluetoothDeviceKind::watch;
    case 0x08:
        return BluetoothDeviceKind::gamepad;
    default:
        return BluetoothDeviceKind::unknown;
    }
}

BluetoothSnapshot bluetooth_parse(const BluetoothObjects &objects,
                                  const std::set<std::string> &busy) {
    BluetoothSnapshot snapshot;
    for (const auto &[path, interfaces] : objects) {
        if (auto it = interfaces.find(adapter_interface);
            it != interfaces.end() && !snapshot.status.present) {
            snapshot.status.present = true;
            snapshot.status.powered = flag(it->second, "Powered");
            snapshot.status.scanning = flag(it->second, "Discovering");
            snapshot.adapter_path = path;
        }
        auto it = interfaces.find(device_interface);
        if (it == interfaces.end()) {
            continue;
        }
        BluetoothDevice device;
        device.path = path;
        device.address = property<std::string>(it->second, "Address").value_or("");
        device.name = property<std::string>(it->second, "Alias").value_or("");
        if (device.name.empty()) {
            device.name = property<std::string>(it->second, "Name").value_or("");
        }
        if (device.name.empty()) {
            device.name = device.address.empty() ? "Unknown Device" : device.address;
        }
        device.paired = flag(it->second, "Paired");
        device.trusted = flag(it->second, "Trusted");
        device.connected = flag(it->second, "Connected");
        device.connecting = !device.connected && busy.contains(device.path);
        device.kind =
            bluetooth_classify_icon(property<std::string>(it->second, "Icon").value_or(""));
        if (device.kind == BluetoothDeviceKind::unknown) {
            device.kind =
                bluetooth_classify_class(property<uint32_t>(it->second, "Class").value_or(0));
        }
        if (auto battery = interfaces.find(battery_interface);
            device.connected && battery != interfaces.end()) {
            if (auto percent = property<uint8_t>(battery->second, "Percentage")) {
                device.battery = *percent;
            }
        }
        if (device.connected && !snapshot.status.connected) {
            snapshot.status.connected = true;
            snapshot.status.device = device.name;
        }
        snapshot.devices.push_back(std::move(device));
    }
    return snapshot;
}

std::vector<StatusMessage> bluetooth_changes(const BluetoothStatus &prev,
                                             const BluetoothStatus &next) {
    std::string was = connected_device(prev);
    std::string now = connected_device(next);
    if (now == was && prev.connected == next.connected) {
        return {};
    }
    if (next.connected) {
        return {{"Connected", "Connected to " + now}};
    }
    return {{"Disconnected", "Disconnected from " + was}};
}

BluetoothService::BluetoothService(Bus &bus) : bus_(bus), root_(bus_.proxy(bluez, "/")) {
    if (!root_) {
        return;
    }
    match_ = bus_.add_match("type='signal',sender='org.bluez'", [this] { request_refresh(); });
    owner_match_ = bus_.add_match(owner_changed_rule, [this] {
        agent_registered_ = false;
        request_refresh();
    });
    export_agent();
    request_refresh();
}

void BluetoothService::request_refresh() {
    if (!root_) {
        return;
    }
    if (refresh_in_flight_) {
        refresh_again_ = true;
        return;
    }
    refresh_in_flight_ = true;
    refresh_again_ = false;
    try {
        refresh_reply_ =
            root_->callMethodAsync("GetManagedObjects")
                .onInterface(object_manager_interface)
                .uponReplyInvoke(
                    [this](std::optional<sdbus::Error> error, BluetoothObjects objects) {
                        refresh_in_flight_ = false;
                        if (error) {
                            log::error("bluetooth: GetManagedObjects failed: {}",
                                       error->getMessage());
                        } else {
                            apply(objects);
                        }
                        if (refresh_again_) {
                            request_refresh();
                        }
                    },
                    sdbus::return_slot);
    } catch (const sdbus::Error &error) {
        refresh_in_flight_ = false;
        log::error("bluetooth: GetManagedObjects dispatch failed: {}", error.getMessage());
    }
}

void BluetoothService::apply(const BluetoothObjects &objects) {
    BluetoothSnapshot next = bluetooth_parse(objects, busy_);
    adapter_path_ = next.adapter_path;
    if (next.status.present) {
        register_agent();
    }
    std::erase_if(proxies_, [&](const auto &entry) {
        return entry.first != adapter_path_ &&
               std::ranges::none_of(
                   next.devices, [&](const BluetoothDevice &d) { return d.path == entry.first; });
    });
    bool first = !initialized_;
    initialized_ = true;
    if (next.status == status_ && next.devices == devices_) {
        return;
    }
    BluetoothStatus prev = status_;
    status_ = next.status;
    devices_ = std::move(next.devices);
    if (!first) {
        for (const StatusMessage &message : bluetooth_changes(prev, status_)) {
            messages.emit(message);
        }
    }
    changed.emit();
}

sdbus::IProxy *BluetoothService::proxy(const std::string &path) {
    if (path.empty()) {
        return nullptr;
    }
    auto &slot = proxies_[path];
    if (!slot) {
        slot = bus_.proxy(bluez, path);
    }
    return slot.get();
}

void BluetoothService::call_device(const std::string &path, const char *method, bool busy,
                                   std::function<void(bool)> done) {
    sdbus::IProxy *device = proxy(path);
    if (device == nullptr) {
        return;
    }
    if (busy) {
        busy_.insert(path);
        for (BluetoothDevice &known : devices_) {
            if (known.path == path && !known.connected) {
                known.connecting = true;
            }
        }
        changed.emit();
    }
    try {
        device->callMethodAsync(method)
            .onInterface(device_interface)
            .uponReplyInvoke([this, path, method, done](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("bluetooth: {} failed: {}", method, error->getMessage());
                }
                if (done) {
                    done(!error);
                }
                busy_.erase(path);
                request_refresh();
            });
    } catch (const sdbus::Error &error) {
        busy_.erase(path);
        log::error("bluetooth: {} dispatch failed: {}", method, error.getMessage());
        if (done) {
            done(false);
        }
        request_refresh();
    }
}

void BluetoothService::call_adapter(const char *method) {
    sdbus::IProxy *adapter = proxy(adapter_path_);
    if (adapter == nullptr) {
        return;
    }
    try {
        adapter->callMethodAsync(method)
            .onInterface(adapter_interface)
            .uponReplyInvoke([method](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("bluetooth: {} failed: {}", method, error->getMessage());
                }
            });
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: {} dispatch failed: {}", method, error.getMessage());
    }
}

void BluetoothService::set_powered(bool powered) {
    sdbus::IProxy *adapter = proxy(adapter_path_);
    if (adapter == nullptr) {
        return;
    }
    if (powered) {
        if (rfkill::bluetooth_hard_blocked()) {
            log::error("bluetooth: set_powered: rfkill hard block is active");
            return;
        }
        if (!rfkill::set_bluetooth_soft_blocked(false)) {
            log::error("bluetooth: rfkill unblock failed, trying Powered anyway");
        }
    }
    try {
        adapter->setPropertyAsync("Powered")
            .onInterface(adapter_interface)
            .toValue(powered)
            .uponReplyInvoke([this](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("bluetooth: cannot set Powered: {}", error->getMessage());
                }
                request_refresh();
            });
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: cannot set Powered: {}", error.getMessage());
    }
}

void BluetoothService::start_discovery() {
    if (status_.powered) {
        call_adapter("StartDiscovery");
    }
}

void BluetoothService::stop_discovery() {
    if (status_.powered) {
        call_adapter("StopDiscovery");
    }
}

void BluetoothService::connect(const std::string &path) { call_device(path, "Connect", true); }

void BluetoothService::disconnect(const std::string &path) {
    call_device(path, "Disconnect", false);
}

void BluetoothService::pair(const std::string &path) {
    if (path.empty() || !pairing_path_.empty()) {
        return;
    }
    pairing_path_ = path;
    call_device(path, "Pair", true, [this, path](bool ok) {
        pairing_path_.clear();
        agent_reply_ = nullptr;
        bool had_request = std::exchange(agent_request_, std::nullopt).has_value();
        if (ok) {
            trust(path);
        }
        if (had_request) {
            changed.emit();
        }
    });
}

void BluetoothService::trust(const std::string &path) {
    sdbus::IProxy *device = proxy(path);
    if (device == nullptr) {
        return;
    }
    try {
        device->setPropertyAsync("Trusted")
            .onInterface(device_interface)
            .toValue(true)
            .uponReplyInvoke([this](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("bluetooth: cannot set Trusted: {}", error->getMessage());
                }
                request_refresh();
            });
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: cannot set Trusted: {}", error.getMessage());
    }
}

void BluetoothService::cancel_pairing() {
    if (agent_request_) {
        finish_agent(false);
    }
    if (!pairing_path_.empty()) {
        call_device(pairing_path_, "CancelPairing", false);
    }
}

void BluetoothService::agent_answer(bool accept) {
    if (!agent_request_) {
        return;
    }
    if (!accept) {
        cancel_pairing();
    } else if (agent_request_->kind == BluetoothAgentRequest::Kind::confirm) {
        finish_agent(true);
    }
}

void BluetoothService::agent_input(const std::string &text) {
    if (!agent_request_) {
        return;
    }
    using Kind = BluetoothAgentRequest::Kind;
    bool digits = !text.empty() && text.size() <= passkey_digits &&
                  std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c) != 0; });
    if (agent_request_->kind == Kind::input_passkey && digits) {
        finish_agent(true, text);
    } else if (agent_request_->kind == Kind::input_pin && !text.empty() &&
               text.size() <= pin_max_chars) {
        finish_agent(true, text);
    }
}

void BluetoothService::hold(BluetoothAgentRequest request,
                            std::function<void(bool, const std::string &)> reply) {
    if (agent_reply_) {
        finish_agent(false);
    }
    agent_request_ = std::move(request);
    agent_reply_ = std::move(reply);
    changed.emit();
}

void BluetoothService::finish_agent(bool accept, const std::string &text) {
    auto reply = std::exchange(agent_reply_, nullptr);
    agent_request_.reset();
    if (reply) {
        try {
            reply(accept, text);
        } catch (const sdbus::Error &error) {
            log::error("bluetooth: agent reply failed: {}", error.getMessage());
        }
    }
    changed.emit();
}

std::string BluetoothService::device_name(const std::string &path) const {
    auto it = std::ranges::find(devices_, path, &BluetoothDevice::path);
    return it == devices_.end() ? path : it->name;
}

bool BluetoothService::device_known(const std::string &path) const {
    auto it = std::ranges::find(devices_, path, &BluetoothDevice::path);
    return it != devices_.end() && (it->paired || it->trusted);
}

void BluetoothService::register_agent() {
    if (!agent_ || agent_registered_) {
        return;
    }
    if (!manager_) {
        manager_ = bus_.proxy(bluez, "/org/bluez");
    }
    if (!manager_) {
        return;
    }
    // ponytail: a failed registration is not retried until bluetoothd restarts
    agent_registered_ = true;
    try {
        manager_->callMethodAsync("RegisterAgent")
            .onInterface(agent_manager_interface)
            .withArguments(sdbus::ObjectPath(agent_path), std::string(agent_capability))
            .uponReplyInvoke([this](std::optional<sdbus::Error> error) {
                if (error && error->getName() != already_exists_error) {
                    log::error("bluetooth: RegisterAgent failed: {}", error->getMessage());
                    return;
                }
                try {
                    manager_->callMethodAsync("RequestDefaultAgent")
                        .onInterface(agent_manager_interface)
                        .withArguments(sdbus::ObjectPath(agent_path))
                        .uponReplyInvoke([](std::optional<sdbus::Error> default_error) {
                            if (default_error) {
                                log::error("bluetooth: RequestDefaultAgent failed: {}",
                                           default_error->getMessage());
                            }
                        });
                } catch (const sdbus::Error &dispatch_error) {
                    log::error("bluetooth: RequestDefaultAgent dispatch failed: {}",
                               dispatch_error.getMessage());
                }
            });
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: RegisterAgent dispatch failed: {}", error.getMessage());
    }
}

void BluetoothService::export_agent() {
    sdbus::IConnection *conn = bus_.conn();
    if (conn == nullptr) {
        return;
    }
    using Kind = BluetoothAgentRequest::Kind;
    auto drop = [this] {
        agent_reply_ = nullptr;
        if (std::exchange(agent_request_, std::nullopt)) {
            changed.emit();
        }
    };
    auto allowed = [this](const std::string &device) {
        return bluetooth_agent_allows(device, pairing_path_, false);
    };
    auto display = [this, allowed](const std::string &device, Kind kind, std::string code,
                                   unsigned entered) {
        if (!allowed(device)) {
            throw rejection("Not pairing this device");
        }
        hold({kind, device, device_name(device), std::move(code), entered}, nullptr);
    };
    try {
        agent_ = sdbus::createObject(*conn, sdbus::ObjectPath(agent_path));
        agent_
            ->addVTable(
                sdbus::registerMethod("Release").implementedAs(drop),
                sdbus::registerMethod("Cancel").implementedAs(drop),
                sdbus::registerMethod("RequestAuthorization")
                    .implementedAs([allowed](sdbus::ObjectPath device) {
                        if (!allowed(device)) {
                            throw rejection("Not pairing this device");
                        }
                    }),
                sdbus::registerMethod("AuthorizeService")
                    .implementedAs([this](sdbus::ObjectPath device, std::string) {
                        if (!bluetooth_agent_allows(device, pairing_path_, device_known(device))) {
                            throw rejection("Unknown device");
                        }
                    }),
                sdbus::registerMethod("DisplayPasskey")
                    .implementedAs(
                        [display](sdbus::ObjectPath device, uint32_t passkey, uint16_t entered) {
                            display(device, Kind::display_passkey, bluetooth_passkey_text(passkey),
                                    entered);
                        }),
                sdbus::registerMethod("DisplayPinCode")
                    .implementedAs([display](sdbus::ObjectPath device, std::string pin) {
                        display(device, Kind::display_pin, std::move(pin), 0);
                    }),
                sdbus::registerMethod("RequestConfirmation")
                    .implementedAs([this, allowed](sdbus::Result<> &&result,
                                                   sdbus::ObjectPath device, uint32_t passkey) {
                        if (!allowed(device)) {
                            result.returnError(rejection("Not pairing this device"));
                            return;
                        }
                        auto reply = std::make_shared<sdbus::Result<>>(std::move(result));
                        hold({Kind::confirm, device, device_name(device),
                              bluetooth_passkey_text(passkey), 0},
                             [reply](bool accept, const std::string &) {
                                 if (accept) {
                                     reply->returnResults();
                                 } else {
                                     reply->returnError(rejection());
                                 }
                             });
                    }),
                sdbus::registerMethod("RequestPasskey")
                    .implementedAs([this, allowed](sdbus::Result<uint32_t> &&result,
                                                   sdbus::ObjectPath device) {
                        if (!allowed(device)) {
                            result.returnError(rejection("Not pairing this device"));
                            return;
                        }
                        auto reply = std::make_shared<sdbus::Result<uint32_t>>(std::move(result));
                        hold({Kind::input_passkey, device, device_name(device), {}, 0},
                             [reply](bool accept, const std::string &text) {
                                 if (accept) {
                                     reply->returnResults(
                                         static_cast<uint32_t>(std::stoul(text)));
                                 } else {
                                     reply->returnError(rejection());
                                 }
                             });
                    }),
                sdbus::registerMethod("RequestPinCode")
                    .implementedAs([this, allowed](sdbus::Result<std::string> &&result,
                                                   sdbus::ObjectPath device) {
                        if (!allowed(device)) {
                            result.returnError(rejection("Not pairing this device"));
                            return;
                        }
                        auto reply =
                            std::make_shared<sdbus::Result<std::string>>(std::move(result));
                        hold({Kind::input_pin, device, device_name(device), {}, 0},
                             [reply](bool accept, const std::string &text) {
                                 if (accept) {
                                     reply->returnResults(text);
                                 } else {
                                     reply->returnError(rejection());
                                 }
                             });
                    }))
            .forInterface(agent_interface);
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: cannot export agent: {}", error.what());
        agent_.reset();
    }
}

void BluetoothService::forget(const std::string &path) {
    sdbus::IProxy *adapter = proxy(adapter_path_);
    if (adapter == nullptr) {
        return;
    }
    try {
        adapter->callMethodAsync("RemoveDevice")
            .onInterface(adapter_interface)
            .withArguments(sdbus::ObjectPath(path))
            .uponReplyInvoke([this](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("bluetooth: RemoveDevice failed: {}", error->getMessage());
                }
                request_refresh();
            });
    } catch (const sdbus::Error &error) {
        log::error("bluetooth: RemoveDevice dispatch failed: {}", error.getMessage());
    }
}

} // namespace astralia
