#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"

#include "service/network_service.h"

namespace astralia {

namespace {

const std::string nm_service = "org.freedesktop.NetworkManager";
const std::string nm_path = "/org/freedesktop/NetworkManager";
const std::string nm_active_interface = "org.freedesktop.NetworkManager.Connection.Active";
const std::string nm_access_point_interface = "org.freedesktop.NetworkManager.AccessPoint";
using Changed = const std::string &;
using Changes = const std::map<std::string, sdbus::Variant> &;
using Invalidated = const std::vector<std::string> &;
constexpr std::chrono::seconds connectivity_interval{30};

// Rescan cadence
constexpr std::chrono::milliseconds rescan_pending{750};
constexpr std::chrono::milliseconds rescan_empty{2000};
constexpr std::chrono::milliseconds rescan_idle{7000};
constexpr std::chrono::milliseconds rescan_after_connect{5000};
constexpr std::chrono::milliseconds rescan_after_change{3000};
constexpr std::chrono::milliseconds rescan_after_state{1000};
constexpr std::chrono::milliseconds rescan_never = std::chrono::hours(24);

std::string wifi_ssid(const NetworkStatus &status) {
    return status.kind == NetworkKind::wifi ? status.ssid : std::string();
}

std::string trim(const std::string &s) {
    std::size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

std::vector<std::string> split_fields(const std::string &line) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            current += line[++i];
        } else if (line[i] == ':') {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current += line[i];
        }
    }
    parts.push_back(std::move(current));
    return parts;
}

std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string raw;
    while (std::getline(stream, raw)) {
        std::string line = trim(raw);
        if (!line.empty()) {
            out.push_back(std::move(line));
        }
    }
    return out;
}

std::vector<std::string> device_argv() {
    return {"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"};
}

std::vector<std::string> connectivity_argv() {
    return {"nmcli", "networking", "connectivity", "check"};
}

std::vector<std::string> profile_argv() {
    return {"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"};
}

std::vector<std::string> list_argv(bool rescan) {
    return {"nmcli", "-t", "-f", "SSID,SECURITY,SIGNAL,IN-USE", "device", "wifi", "list", "--rescan", rescan ? "yes" : "no"};
}

std::vector<std::string> connect_argv(const std::string &ssid, const std::string &password, bool saved) {
    if (saved) {
        return {"nmcli", "-t", "connection", "up", "id", ssid};
    }
    std::vector<std::string> argv{"nmcli", "-t", "device", "wifi", "connect", ssid};
    if (!password.empty()) {
        argv.push_back("password");
        argv.push_back(password);
    }
    return argv;
}

std::vector<std::string> disconnect_argv(const std::string &ssid) {
    return {"nmcli", "connection", "down", "id", ssid};
}

std::vector<std::string> forget_argv(const std::string &ssid) {
    static const char *script = R"(
ssid="$1"
uuid=$(nmcli -t -f NAME,UUID,TYPE connection show | awk -F: -v target="$ssid" '$1 == target && $3 == "802-11-wireless" { print $2; exit }')
[ -n "$uuid" ] && nmcli connection delete uuid "$uuid"
)";
    return {"sh", "-c", script, "--", ssid};
}

std::string connect_error(const std::string &output) {
    if (output.find("Secrets were required") != std::string::npos ||
        output.find("no secrets provided") != std::string::npos) {
        return "Incorrect password";
    }
    if (output.find("No network with SSID") != std::string::npos) {
        return "Network not found";
    }
    if (output.find("Timeout") != std::string::npos) {
        return "Connection timed out";
    }
    return "Connection failed";
}

} // namespace

std::vector<StatusMessage> network_changes(const NetworkStatus &prev, const NetworkStatus &next) {
    std::vector<StatusMessage> messages;
    std::string was = wifi_ssid(prev);
    std::string now = wifi_ssid(next);
    if (now != was) {
        if (!now.empty()) {
            messages.push_back({"Connected", "Connected to " + now});
        } else {
            messages.push_back({"Disconnected", "Disconnected from " + was});
        }
    }
    bool was_ethernet = prev.kind == NetworkKind::ethernet;
    bool now_ethernet = next.kind == NetworkKind::ethernet;
    if (now_ethernet && !was_ethernet) {
        messages.push_back({"Connected", "Connected via Ethernet"});
    } else if (was_ethernet && !now_ethernet) {
        messages.push_back({"Disconnected", "Ethernet disconnected"});
    }
    if (next.portal && !prev.portal) {
        messages.push_back({"Captive Portal", now.empty() ? std::string("Sign in required")
                                                          : "Sign in required for " + now});
    }
    return messages;
}

NetworkMap network_parse_networks(const std::string &text, const std::set<std::string> &profiles) {
    NetworkMap result;
    for (const std::string &line : lines(text)) {
        std::vector<std::string> parts = split_fields(line);
        if (parts.size() != 4 || parts[0].empty()) {
            continue;
        }
        const std::string &ssid = parts[0];
        std::string security = parts[1];
        if (std::size_t pos = security.find("WPA2 WPA3"); pos != std::string::npos) {
            security.replace(pos, 9, "WPA2/WPA3");
        }
        if (std::size_t pos = security.find("WPA1 WPA2"); pos != std::string::npos) {
            security.replace(pos, 9, "WPA1/WPA2");
        }
        int signal = 0;
        std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), signal);
        bool connected = parts[3] == "*";
        auto [it, inserted] = result.try_emplace(ssid);
        if (inserted) {
            it->second = {ssid, security.empty() ? "--" : security, signal, connected,
                          profiles.contains(ssid), true};
        } else if (connected) {
            it->second.connected = true;
        }
    }
    for (const std::string &name : profiles) {
        result.try_emplace(name, NetworkInfo{name, "--", 0, false, true, false});
    }
    return result;
}

NetworkDeviceStatus network_parse_device_status(const std::string &text) {
    NetworkDeviceStatus status;
    for (const std::string &line : lines(text)) {
        std::vector<std::string> parts = split_fields(line);
        if (parts.size() < 3 || parts[2] == "unmanaged") {
            continue;
        }
        if (parts[1] == "wifi") {
            status.wifi = true;
        } else if (parts[1] == "ethernet") {
            status.ethernet = true;
            if (parts[2].starts_with("connected")) {
                status.ethernet_connected = true;
                if (status.ethernet_name.empty()) {
                    for (std::size_t i = 3; i < parts.size(); ++i) {
                        status.ethernet_name += (i > 3 ? ":" : "") + parts[i];
                    }
                }
            }
        }
    }
    return status;
}

bool network_parse_wifi_device(const std::string &text) { return network_parse_device_status(text).wifi; }

std::string network_connectivity_name(uint32_t value) {
    switch (value) {
    case 1:
        return "none";
    case 2:
        return "portal";
    case 3:
        return "limited";
    case 4:
        return "full";
    default:
        return "unknown";
    }
}

std::set<std::string> network_parse_profiles(const std::string &text) {
    std::set<std::string> profiles;
    for (const std::string &line : lines(text)) {
        std::vector<std::string> parts = split_fields(line);
        if (parts.size() == 2 && parts[1] == "802-11-wireless" && !parts[0].empty()) {
            profiles.insert(parts[0]);
        }
    }
    return profiles;
}

int network_visible_count(const NetworkMap &networks) {
    return static_cast<int>(std::ranges::count_if(networks, [](const auto &entry) {
        const NetworkInfo &info = entry.second;
        return info.connected || info.in_range || !info.existing;
    }));
}

bool network_scan_would_collapse(const NetworkMap &current, const NetworkMap &parsed) {
    int in_range = 0;
    bool self_only = true;
    for (const auto &[ssid, info] : parsed) {
        if (!info.in_range) {
            continue;
        }
        ++in_range;
        self_only = self_only && info.connected;
    }
    return in_range == 1 && self_only && network_visible_count(current) > 1;
}

NetworkService::NetworkService(SystemBus &bus, Reactor &loop)
    : bus_(bus), loop_(loop), manager_(bus_.proxy(nm_service, nm_path)), device_proc_(loop),
      profile_proc_(loop), quick_scan_proc_(loop), scan_proc_(loop), connect_proc_(loop),
      disconnect_proc_(loop), forget_proc_(loop), connectivity_proc_(loop) {
    rescan_timer_ = loop_.add_timer(
        [this] {
            if (!next_rescan_) {
                return rescan_never;
            }
            auto left = std::chrono::ceil<std::chrono::milliseconds>(*next_rescan_ - std::chrono::steady_clock::now());
            return std::max(left, std::chrono::milliseconds(1));
        },
        [this] {
            if (next_rescan_ && std::chrono::steady_clock::now() >= *next_rescan_) {
                next_rescan_.reset();
                scan();
            }
        });
    connectivity_timer_ = loop_.add_timer(
        [this] {
            if (!next_connectivity_check_) {
                return rescan_never;
            }
            auto left = std::chrono::ceil<std::chrono::milliseconds>(*next_connectivity_check_ - std::chrono::steady_clock::now());
            return std::max(left, std::chrono::milliseconds(1));
        },
        [this] {
            if (next_connectivity_check_ && std::chrono::steady_clock::now() >= *next_connectivity_check_) {
                next_connectivity_check_ = std::chrono::steady_clock::now() + connectivity_interval;
                if (status_.kind != NetworkKind::none) {
                    check_connectivity();
                }
            }
        });
    if (!manager_) {
        return;
    }
    manager_->uponSignal("PropertiesChanged")
        .onInterface("org.freedesktop.DBus.Properties")
        .call([this](Changed interface, Changes properties, Invalidated) {
            if (interface != nm_service) {
                return;
            }
            bool state = properties.contains("State") || properties.contains("ActiveConnections");
            if (state && watching_) {
                device_proc_.start(device_argv(), [this](const std::string &output) {
                    NetworkDeviceStatus next = network_parse_device_status(output);
                    if (next != device_) {
                        device_ = std::move(next);
                        changed.emit();
                    }
                });
                schedule_rescan(rescan_after_state);
            }
            if (properties.contains("Connectivity") && watching_) {
                check_connectivity();
            }
            refresh();
        });
    refresh();
}

void NetworkService::refresh() {
    if (!manager_) {
        return;
    }
    uint64_t generation = ++refresh_generation_;
    manager_reply_ = dbus_get_all_async(*manager_, nm_service, [this, generation](const DbusProperties &properties) {
        if (generation != refresh_generation_) {
            return;
        }
        bool wifi_was = wifi_enabled_;
        wifi_enabled_ = dbus_value<bool>(properties, "WirelessEnabled").value_or(false);
        if (wifi_enabled_ != wifi_was && initialized_) {
            if (!wifi_enabled_) {
                cancel_scans();
                networks_.clear();
            } else if (watching_) {
                scan();
            }
            changed.emit();
        }
        NetworkStatus next;
        std::string type = dbus_value<std::string>(properties, "PrimaryConnectionType").value_or("");
        set_connectivity(network_connectivity_name(dbus_value<uint32_t>(properties, "Connectivity").value_or(0)));
        next.portal = connectivity_ == "portal";
        if (type == "802-3-ethernet") {
            next.kind = NetworkKind::ethernet;
            finish_refresh(std::move(next));
            return;
        }
        auto active = dbus_value<sdbus::ObjectPath>(properties, "PrimaryConnection");
        if (type != "802-11-wireless" || !active || std::string(*active) == "/") {
            finish_refresh(std::move(next));
            return;
        }
        next.kind = NetworkKind::wifi;
        active_reply_ = {};
        access_point_reply_ = {};
        active_proxy_ = bus_.proxy(nm_service, *active);
        if (!active_proxy_) {
            finish_refresh(std::move(next));
            return;
        }
        active_reply_ = dbus_get_all_async(*active_proxy_, nm_active_interface, [this, generation, next](const DbusProperties &active_properties) mutable {
            if (generation != refresh_generation_) {
                return;
            }
            auto access_point = dbus_value<sdbus::ObjectPath>(active_properties, "SpecificObject");
            if (!access_point || std::string(*access_point) == "/") {
                finish_refresh(std::move(next));
                return;
            }
            access_point_proxy_ = bus_.proxy(nm_service, *access_point);
            if (!access_point_proxy_) {
                finish_refresh(std::move(next));
                return;
            }
            access_point_reply_ = dbus_get_all_async(*access_point_proxy_, nm_access_point_interface, [this, generation, next](const DbusProperties &ap_properties) mutable {
                if (generation != refresh_generation_) {
                    return;
                }
                next.strength = dbus_value<uint8_t>(ap_properties, "Strength").value_or(0);
                if (auto ssid = dbus_value<std::vector<uint8_t>>(ap_properties, "Ssid")) {
                    next.ssid.assign(ssid->begin(), ssid->end());
                }
                finish_refresh(std::move(next));
            });
        });
    });
}

void NetworkService::finish_refresh(NetworkStatus next) {
    bool first = !initialized_;
    initialized_ = true;
    if (next == status_) {
        return;
    }
    NetworkStatus prev = status_;
    status_ = std::move(next);
    if (!first) {
        for (const StatusMessage &message : network_changes(prev, status_)) {
            messages.emit(message);
        }
        if (watching_) {
            schedule_rescan(rescan_after_state);
        }
    }
    if (status_.kind != NetworkKind::none && (first || prev.kind != status_.kind || prev.ssid != status_.ssid)) {
        check_connectivity();
    }
    changed.emit();
}

void NetworkService::check_connectivity() {
    connectivity_proc_.start(connectivity_argv(), [this](const std::string &output) {
        std::string text = trim(output);
        for (char &c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        set_connectivity(text == "none" || text.empty() ? "unknown" : text);
        NetworkStatus next = status_;
        next.portal = connectivity_ == "portal";
        if (next != status_) {
            NetworkStatus prev = status_;
            status_ = next;
            for (const StatusMessage &message : network_changes(prev, status_)) {
                messages.emit(message);
            }
            changed.emit();
        }
    });
}

void NetworkService::set_connectivity(const std::string &value) { connectivity_ = value; }

void NetworkService::start_watch() {
    watching_ = true;
    device_proc_.start(device_argv(), [this](const std::string &output) {
        device_ = network_parse_device_status(output);
        changed.emit();
    });
    next_connectivity_check_ = std::chrono::steady_clock::now() + connectivity_interval;
    loop_.reschedule(connectivity_timer_);
    scan();
}

void NetworkService::stop_watch() {
    watching_ = false;
    next_rescan_.reset();
    next_connectivity_check_.reset();
}

void NetworkService::scan() {
    if (!wifi_enabled_) {
        return;
    }
    last_error_.clear();
    if (profile_proc_.running() || quick_scan_proc_.running() || scan_proc_.running()) {
        scan_pending_ = true;
        return;
    }
    scanning_ = profile_proc_.start(profile_argv(), [this](const std::string &output) { on_profiles(output); });
    changed.emit();
}

void NetworkService::on_profiles(const std::string &output) {
    profiles_ = network_parse_profiles(output);
    if (!wifi_enabled_) {
        scanning_ = false;
        changed.emit();
        return;
    }
    if (networks_.empty()) {
        for (const std::string &name : profiles_) {
            networks_.try_emplace(name, NetworkInfo{name, "--", 0, false, true, false});
        }
    }
    quick_scan_proc_.start(list_argv(false), [this](const std::string &out) { on_quick_scan(out); });
    changed.emit();
}

void NetworkService::on_quick_scan(const std::string &output) {
    if (!wifi_enabled_) {
        scanning_ = false;
        changed.emit();
        return;
    }
    NetworkMap quick = network_parse_networks(output, profiles_);
    bool any_in_range = std::ranges::any_of(quick, [](const auto &entry) { return entry.second.in_range; });
    if (any_in_range && quick != networks_) {
        networks_ = std::move(quick);
        changed.emit();
    }
    scan_proc_.start(list_argv(true), [this](const std::string &out) { on_scan(out); });
}

void NetworkService::on_scan(const std::string &output) {
    NetworkMap parsed = network_parse_networks(output, profiles_);
    if (!network_scan_would_collapse(networks_, parsed)) {
        networks_ = std::move(parsed);
    }
    bool has_real = std::ranges::any_of(networks_, [](const auto &entry) { return entry.second.in_range; });
    std::chrono::milliseconds next = rescan_idle;
    if (std::exchange(scan_pending_, false)) {
        scanning_ = false;
        next = rescan_pending;
    } else if (wifi_enabled_ && !has_real) {
        next = rescan_empty;
    } else {
        scanning_ = false;
    }
    schedule_rescan(next);
    changed.emit();
}

void NetworkService::connect(const std::string &ssid, const std::string &password) {
    if (connect_proc_.running()) {
        return;
    }
    connecting_to_ = ssid;
    last_error_.clear();
    std::vector<std::string> argv = connect_argv(ssid, password, profiles_.contains(ssid));
    connect_proc_.start(argv, [this](const std::string &output) { on_connect(output); }, true);
    for (std::string &arg : argv) {
        explicit_bzero(arg.data(), arg.size());
    }
    changed.emit();
}

void NetworkService::on_connect(const std::string &output) {
    std::string ssid = std::exchange(connecting_to_, {});
    if (output.find("successfully activated") != std::string::npos ||
        output.find("Connection successfully") != std::string::npos) {
        if (auto it = networks_.find(ssid); it != networks_.end()) {
            it->second.connected = true;
            it->second.existing = true;
        }
    } else if (!trim(output).empty()) {
        last_error_ = connect_error(output);
        log::error("network: connect {} failed: {}", ssid, trim(output));
        if (last_error_ == "Incorrect password") {
            forget(ssid);
        }
    }
    schedule_rescan(rescan_after_connect);
    changed.emit();
}

void NetworkService::disconnect(const std::string &ssid) {
    disconnect_proc_.start(disconnect_argv(ssid), [this, ssid](const std::string &) {
        if (auto it = networks_.find(ssid); it != networks_.end()) {
            it->second.connected = false;
        }
        schedule_rescan(rescan_after_change);
        changed.emit();
    });
}

void NetworkService::forget(const std::string &ssid) {
    forget_proc_.start(forget_argv(ssid), [this, ssid](const std::string &) {
        profiles_.erase(ssid);
        if (auto it = networks_.find(ssid); it != networks_.end()) {
            it->second.existing = false;
        }
        schedule_rescan(rescan_after_change);
        changed.emit();
    });
}

void NetworkService::set_wifi_enabled(bool enabled) {
    if (!manager_) {
        return;
    }
    try {
        manager_->setPropertyAsync("WirelessEnabled")
            .onInterface(nm_service)
            .toValue(enabled)
            .uponReplyInvoke([](std::optional<sdbus::Error> error) {
                if (error) {
                    log::error("network: cannot set WirelessEnabled: {}", error->getMessage());
                }
            });
    } catch (const sdbus::Error &error) {
        log::error("network: cannot set WirelessEnabled: {}", error.what());
    }
}

void NetworkService::clear_error() {
    last_error_.clear();
    changed.emit();
}

void NetworkService::schedule_rescan(std::chrono::milliseconds delay) {
    if (!watching_) {
        return;
    }
    next_rescan_ = std::chrono::steady_clock::now() + delay;
    loop_.reschedule(rescan_timer_);
}

void NetworkService::cancel_scans() {
    profile_proc_.cancel();
    quick_scan_proc_.cancel();
    scan_proc_.cancel();
    scanning_ = false;
    scan_pending_ = false;
}

} // namespace astralia
