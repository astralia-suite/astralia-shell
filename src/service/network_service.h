#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include <string>
#include <vector>

#include "core/async_process.h"
#include "core/dbus.h"
#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

enum class NetworkKind { none,
                         ethernet,
                         wifi };

struct NetworkStatus {
    NetworkKind kind = NetworkKind::none;
    int strength = 0;
    std::string ssid;
    bool portal = false;
    bool operator==(const NetworkStatus &) const = default;
};

struct StatusMessage {
    std::string summary;
    std::string body;
    bool operator==(const StatusMessage &) const = default;
};

struct NetworkInfo {
    std::string ssid;
    std::string security;
    int signal = 0;
    bool connected = false;
    bool existing = false;
    bool in_range = false;
    bool operator==(const NetworkInfo &) const = default;
};

using NetworkMap = std::map<std::string, NetworkInfo>;

struct NetworkDeviceStatus {
    bool wifi = false;
    bool ethernet = false;
    bool ethernet_connected = false;
    std::string ethernet_name;

    bool operator==(const NetworkDeviceStatus &) const = default;
};

std::vector<StatusMessage> network_changes(const NetworkStatus &prev, const NetworkStatus &next);
NetworkMap network_parse_networks(const std::string &text, const std::set<std::string> &profiles);
NetworkDeviceStatus network_parse_device_status(const std::string &text);
bool network_parse_wifi_device(const std::string &text);
std::string network_connectivity_name(uint32_t value);
std::set<std::string> network_parse_profiles(const std::string &text);
int network_visible_count(const NetworkMap &networks);
bool network_scan_would_collapse(const NetworkMap &current, const NetworkMap &parsed);

class NetworkService {
  public:
    NetworkService(SystemBus &bus, Reactor &loop);
    const NetworkStatus &status() const { return status_; }
    const NetworkMap &networks() const { return networks_; }
    bool wifi_available() const { return device_.wifi; }
    bool wifi_enabled() const { return wifi_enabled_; }
    bool ethernet_available() const { return device_.ethernet; }
    bool ethernet_connected() const { return device_.ethernet_connected; }
    const std::string &ethernet_name() const { return device_.ethernet_name; }
    const std::string &connectivity() const { return connectivity_; }
    bool scanning() const { return scanning_; }
    const std::string &connecting_to() const { return connecting_to_; }
    const std::string &last_error() const { return last_error_; }

    void start_watch();
    void stop_watch();
    void scan();
    void connect(const std::string &ssid, const std::string &password);
    void disconnect(const std::string &ssid);
    void forget(const std::string &ssid);
    void set_wifi_enabled(bool enabled);
    void clear_error();

    Signal<> changed;
    Signal<const StatusMessage &> messages;

  private:
    void refresh();
    void finish_refresh(NetworkStatus next);
    void check_connectivity();
    void set_connectivity(const std::string &value);
    void on_profiles(const std::string &output);
    void on_quick_scan(const std::string &output);
    void on_scan(const std::string &output);
    void on_connect(const std::string &output);
    void schedule_rescan(std::chrono::milliseconds delay);
    void cancel_scans();

    SystemBus &bus_;
    Reactor &loop_;
    NetworkStatus status_;
    NetworkDeviceStatus device_;
    std::string connectivity_ = "unknown";
    bool initialized_ = false;
    uint64_t refresh_generation_ = 0;
    std::unique_ptr<sdbus::IProxy> manager_;
    std::unique_ptr<sdbus::IProxy> active_proxy_;
    std::unique_ptr<sdbus::IProxy> access_point_proxy_;
    sdbus::Slot manager_reply_;
    sdbus::Slot active_reply_;
    sdbus::Slot access_point_reply_;

    NetworkMap networks_;
    std::set<std::string> profiles_;
    bool watching_ = false;
    bool wifi_enabled_ = false;
    bool scanning_ = false;
    bool scan_pending_ = false;
    std::string connecting_to_;
    std::string last_error_;
    AsyncProcess device_proc_;
    AsyncProcess profile_proc_;
    AsyncProcess quick_scan_proc_;
    AsyncProcess scan_proc_;
    AsyncProcess connect_proc_;
    AsyncProcess disconnect_proc_;
    AsyncProcess forget_proc_;
    AsyncProcess connectivity_proc_;
    std::optional<std::chrono::steady_clock::time_point> next_rescan_;
    int rescan_timer_ = -1;
    std::optional<std::chrono::steady_clock::time_point> next_connectivity_check_;
    int connectivity_timer_ = -1;
};

} // namespace astralia
