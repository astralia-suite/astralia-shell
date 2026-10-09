#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "service/audio_service.h"
#include "service/battery_service.h"
#include "service/bluetooth_service.h"
#include "service/icon_service.h"
#include "service/media_service.h"
#include "service/network_service.h"
#include "service/notification_service.h"
#include "service/tray_service.h"
#include "service/user_service.h"

#include "check.h"

void check_status_changes() {
    using astralia::NetworkKind;
    using astralia::StatusMessage;
    using test::check;
    using Messages = std::vector<StatusMessage>;
    astralia::NetworkStatus none;
    astralia::NetworkStatus home{NetworkKind::wifi, 80, "home", false};
    astralia::NetworkStatus cafe{NetworkKind::wifi, 60, "cafe", false};
    astralia::NetworkStatus portal{NetworkKind::wifi, 60, "cafe", true};
    astralia::NetworkStatus wired{NetworkKind::ethernet, 0, "", false};
    check(astralia::network_changes(home, home).empty(), "unchanged network sends nothing");
    check(astralia::network_changes(home, {NetworkKind::wifi, 30, "home", false}).empty(),
          "signal change sends nothing");
    check(astralia::network_changes(none, home) == Messages{{"Connected", "Connected to home"}},
          "Wi-Fi connect");
    check(astralia::network_changes(home, none) ==
              Messages{{"Disconnected", "Disconnected from home"}},
          "Wi-Fi disconnect");
    check(astralia::network_changes(home, cafe) == Messages{{"Connected", "Connected to cafe"}},
          "Wi-Fi switch");
    check(astralia::network_changes(none, wired) ==
              Messages{{"Connected", "Connected via Ethernet"}},
          "Ethernet up");
    check(astralia::network_changes(wired, none) ==
              Messages{{"Disconnected", "Ethernet disconnected"}},
          "Ethernet down");
    check(astralia::network_changes(cafe, portal) ==
              Messages{{"Captive Portal", "Sign in required for cafe"}},
          "captive portal");
    astralia::BluetoothStatus idle{true, true, false, ""};
    astralia::BluetoothStatus buds{true, true, true, "Buds"};
    check(astralia::bluetooth_changes(idle, idle).empty(), "unchanged Bluetooth sends nothing");
    check(astralia::bluetooth_changes(idle, {true, false, false, ""}).empty(),
          "power off sends nothing");
    check(astralia::bluetooth_changes(idle, buds) == Messages{{"Connected", "Connected to Buds"}},
          "Bluetooth connect");
    check(astralia::bluetooth_changes(buds, idle) ==
              Messages{{"Disconnected", "Disconnected from Buds"}},
          "Bluetooth disconnect");
}

void check_network_parse() {
    using astralia::NetworkMap;
    using test::check;
    std::set<std::string> profiles = astralia::network_parse_profiles(
        "home:802-11-wireless\nWired connection 1:802-3-ethernet\nlab\\:5G:802-11-wireless\n");
    check(profiles == std::set<std::string>{"home", "lab:5G"}, "profiles keep Wi-Fi only and unescape colons");
    NetworkMap networks = astralia::network_parse_networks(
        "home:WPA2 WPA3:82:*\ncafe::40: \nlab\\:5G:WPA2:60: \ncafe:WPA2:90: \n:WPA2:30: \n", profiles);
    check(networks.size() == 3, "hidden SSIDs drop and duplicates merge");
    check(networks["home"].connected && networks["home"].existing && networks["home"].security == "WPA2/WPA3",
          "connected saved network");
    check(networks["cafe"].security == "--" && networks["cafe"].signal == 40 && !networks["cafe"].existing,
          "first duplicate wins; open network shows --");
    check(networks["lab:5G"].in_range && networks["lab:5G"].existing, "escaped SSID matches its profile");
    NetworkMap out_of_range = astralia::network_parse_networks("", {"office"});
    check(out_of_range.size() == 1 && !out_of_range["office"].in_range, "profiles out of range stay listed");
    check(astralia::network_visible_count(out_of_range) == 0, "out-of-range profiles are not visible");
    check(astralia::network_parse_wifi_device("wlan0:wifi:connected\nlo:loopback:unmanaged\n"), "wifi device found");
    check(!astralia::network_parse_wifi_device("wlan0:wifi:unmanaged\neth0:ethernet:connected\n"), "unmanaged wifi ignored");
    NetworkMap self_only = astralia::network_parse_networks("home:WPA2:80:*\n", {});
    check(astralia::network_scan_would_collapse(networks, self_only), "a self-only rescan is discarded");
    check(!astralia::network_scan_would_collapse(self_only, networks), "a fuller rescan is kept");
}

void check_media() {
    using astralia::MediaPlayback;
    using test::check;
    check(astralia::media_parse_playback("Playing") == MediaPlayback::playing, "Playing parses");
    check(astralia::media_parse_playback("Paused") == MediaPlayback::paused, "Paused parses");
    check(astralia::media_parse_playback("bogus") == MediaPlayback::stopped, "unknown is stopped");
    check(astralia::media_format_position(0) == "0:00", "zero position");
    check(astralia::media_format_position(65'000'000) == "1:05", "pads seconds");
    check(astralia::media_format_position(-5) == "0:00", "negative clamps");
    check(astralia::media_is_local_art_url("file:///tmp/a.png") && !astralia::media_is_local_art_url("https://x/a.png") && !astralia::media_is_local_art_url(""), "only file URLs are local art");
    check(astralia::media_select_player({}) == -1, "no players");
    check(astralia::media_select_player({{"a", MediaPlayback::paused}, {"b", MediaPlayback::playing}}) == 1, "playing wins");
    check(astralia::media_select_player({{"a", MediaPlayback::paused}, {"b", MediaPlayback::stopped}}) == 0, "else the first");
}

void check_tray() {
    using astralia::TrayMenuLayout;
    using test::check;
    using Props = std::map<std::string, sdbus::Variant>;
    check(astralia::tray_strip_mnemonic("_Open __file") == "Open _file", "mnemonics stripped, doubled kept");
    TrayMenuLayout leaf{int32_t{3}, Props{{"label", sdbus::Variant(std::string("_Quit"))}, {"enabled", sdbus::Variant(false)}}, std::vector<sdbus::Variant>{}};
    TrayMenuLayout separator{int32_t{2}, Props{{"type", sdbus::Variant(std::string("separator"))}}, std::vector<sdbus::Variant>{}};
    TrayMenuLayout toggle{int32_t{4}, Props{{"label", sdbus::Variant(std::string("Mute"))}, {"toggle-type", sdbus::Variant(std::string("checkmark"))}, {"toggle-state", sdbus::Variant(int32_t{1})}}, std::vector<sdbus::Variant>{}};
    TrayMenuLayout sub{int32_t{1}, Props{{"label", sdbus::Variant(std::string("More"))}, {"children-display", sdbus::Variant(std::string("submenu"))}}, std::vector<sdbus::Variant>{sdbus::Variant(toggle)}};
    TrayMenuLayout root{int32_t{0}, Props{}, std::vector<sdbus::Variant>{sdbus::Variant(sub), sdbus::Variant(separator), sdbus::Variant(leaf)}};
    astralia::TrayMenuEntry menu = astralia::tray_parse_menu(root);
    check(menu.children.size() == 3, "three top-level entries");
    check(menu.children[0].children.size() == 1 && menu.children[0].children[0].checkbox && menu.children[0].children[0].checked, "nested checked toggle");
    check(menu.children[1].separator, "separator parsed");
    check(menu.children[2].label == "Quit" && !menu.children[2].enabled, "disabled leaf label");
}

void check_battery_parse() {
    using astralia::DbusProperties;
    using test::check;
    DbusProperties charging{
        {"IsPresent", sdbus::Variant(true)},
        {"Percentage", sdbus::Variant(79.6)},
        {"State", sdbus::Variant(uint32_t{1})},
        {"TimeToFull", sdbus::Variant(int64_t{1800})},
        {"TimeToEmpty", sdbus::Variant(int64_t{0})},
    };
    astralia::BatteryStatus status = astralia::battery_parse_status(charging);
    check(status.present && status.percent == 80 && status.charging && !status.full && !status.pending, "charging display device");
    check(status.seconds_left == 1800, "charging reports time to full");

    DbusProperties discharging{
        {"IsPresent", sdbus::Variant(true)},
        {"Percentage", sdbus::Variant(41.0)},
        {"State", sdbus::Variant(uint32_t{2})},
        {"TimeToFull", sdbus::Variant(int64_t{0})},
        {"TimeToEmpty", sdbus::Variant(int64_t{5400})},
    };
    status = astralia::battery_parse_status(discharging);
    check(!status.charging && status.seconds_left == 5400, "discharging reports time to empty");

    check(astralia::battery_parse_status({{"IsPresent", sdbus::Variant(false)}}) == astralia::BatteryStatus{}, "absent battery is the default status");
    check(astralia::battery_parse_status({}) == astralia::BatteryStatus{}, "missing properties give the default status");

    DbusProperties mouse{
        {"Type", sdbus::Variant(uint32_t{5})},
        {"IsPresent", sdbus::Variant(true)},
        {"Percentage", sdbus::Variant(12.0)},
        {"State", sdbus::Variant(uint32_t{2})},
        {"NativePath", sdbus::Variant(std::string("hid-1234"))},
    };
    astralia::BatteryDevice device = astralia::battery_parse_device("/dev/mouse", mouse);
    check(device.path == "/dev/mouse" && device.native_path == "hid-1234" && device.percent == 12, "device fields parse");
    check(!device.is_battery(), "a mouse is not a laptop battery");
    DbusProperties battery{{"Type", sdbus::Variant(uint32_t{2})}};
    check(astralia::battery_parse_device("/b", battery).is_battery(), "type 2 is a battery");
    DbusProperties monitor{{"Type", sdbus::Variant(uint32_t{4})}};
    check(astralia::battery_parse_device("/m", monitor).is_battery(), "type 4 is a monitor battery");
}

void check_bluetooth() {
    using astralia::BluetoothDevice;
    using astralia::BluetoothDeviceKind;
    using test::check;
    check(astralia::bluetooth_classify_icon("audio-headset") == BluetoothDeviceKind::headset, "headset icon");
    check(astralia::bluetooth_classify_icon("audio-headphones") == BluetoothDeviceKind::headphones, "headphones icon");
    check(astralia::bluetooth_classify_icon("input-mouse") == BluetoothDeviceKind::mouse, "mouse icon");
    check(astralia::bluetooth_classify_icon("phone") == BluetoothDeviceKind::phone, "phone icon");
    check(astralia::bluetooth_classify_icon("something-unrecognized") == BluetoothDeviceKind::unknown, "unknown icon");
    check(astralia::bluetooth_classify_class(0x0404) == BluetoothDeviceKind::headset, "headset class");
    check(astralia::bluetooth_classify_class(0x0508) == BluetoothDeviceKind::mouse, "mouse class");
    check(astralia::bluetooth_classify_class(0x0504) == BluetoothDeviceKind::keyboard, "keyboard class");

    BluetoothDevice connected;
    connected.connected = true;
    connected.paired = true;
    check(astralia::bluetooth_is_connected(connected) && !astralia::bluetooth_is_paired(connected) && !astralia::bluetooth_is_nearby(connected), "connected bucket");
    BluetoothDevice paired;
    paired.paired = true;
    check(!astralia::bluetooth_is_connected(paired) && astralia::bluetooth_is_paired(paired) && !astralia::bluetooth_is_nearby(paired), "paired bucket");
    BluetoothDevice trusted;
    trusted.trusted = true;
    check(astralia::bluetooth_is_paired(trusted), "trusted counts as paired");
    BluetoothDevice nearby;
    check(astralia::bluetooth_is_nearby(nearby) && !astralia::bluetooth_is_paired(nearby), "nearby bucket");

    using Props = std::map<std::string, sdbus::Variant>;
    astralia::BluetoothObjects objects;
    objects[sdbus::ObjectPath("/org/bluez/hci0")]["org.bluez.Adapter1"] = Props{{"Powered", sdbus::Variant(true)}, {"Discovering", sdbus::Variant(true)}};
    auto &buds = objects[sdbus::ObjectPath("/org/bluez/hci0/dev_AA")];
    buds["org.bluez.Device1"] = Props{{"Address", sdbus::Variant(std::string("AA:BB"))}, {"Alias", sdbus::Variant(std::string("Buds"))}, {"Paired", sdbus::Variant(true)}, {"Connected", sdbus::Variant(true)}, {"Icon", sdbus::Variant(std::string("audio-headset"))}};
    buds["org.bluez.Battery1"] = Props{{"Percentage", sdbus::Variant(uint8_t{77})}};
    auto &pad = objects[sdbus::ObjectPath("/org/bluez/hci0/dev_BB")];
    pad["org.bluez.Device1"] = Props{{"Address", sdbus::Variant(std::string("CC:DD"))}, {"Class", sdbus::Variant(uint32_t{0x0508})}};
    astralia::BluetoothSnapshot snapshot = astralia::bluetooth_parse(objects, {"/org/bluez/hci0/dev_BB"});
    check(snapshot.status.present && snapshot.status.powered && snapshot.status.scanning, "adapter state parses");
    check(snapshot.status.connected && snapshot.status.device == "Buds", "connected device names the status");
    check(snapshot.adapter_path == "/org/bluez/hci0", "adapter path is kept");
    check(snapshot.devices.size() == 2, "both devices parse");
    check(snapshot.devices[0].battery == 77 && snapshot.devices[0].kind == BluetoothDeviceKind::headset, "battery and icon kind");
    check(snapshot.devices[1].name == "CC:DD" && snapshot.devices[1].connecting && snapshot.devices[1].kind == BluetoothDeviceKind::mouse, "address names an unnamed device; busy device is connecting; class gives the kind");
    check(astralia::bluetooth_parse({}, {}).status == astralia::BluetoothStatus{}, "no adapter gives the default status");

    std::string path = "/tmp/astralia_test_rfkill_" + std::to_string(getpid());
    check(!astralia::rfkill::read_sysfs_uint(path).has_value() && !astralia::rfkill::read_sysfs_string(path).has_value(), "missing sysfs file reads as nothing");
    {
        std::ofstream file(path);
        file << "1\n";
    }
    check(astralia::rfkill::read_sysfs_uint(path) == 1u, "sysfs unsigned reads");
    {
        std::ofstream file(path);
        file << "bluetooth\n";
    }
    check(astralia::rfkill::read_sysfs_string(path) == "bluetooth", "sysfs string reads");
    unlink(path.c_str());
}

void check_network_more() {
    using astralia::NetworkInfo;
    using astralia::NetworkMap;
    using test::check;
    {
        NetworkMap nets = astralia::network_parse_networks("HomeNet:WPA2:80:*\nGuest:WPA2 WPA3:40:\nOpen:--:20:\n", {});
        check(nets.size() == 3 && nets.at("HomeNet").connected && nets.at("HomeNet").signal == 80 && nets.at("HomeNet").security == "WPA2", "scan lines parse");
        check(nets.at("Guest").security == "WPA2/WPA3" && !nets.at("Guest").connected && nets.at("Open").security == "--", "security names");
        bool all_in_range = true;
        for (const auto &[ssid, info] : nets) {
            all_in_range = all_in_range && info.in_range;
        }
        check(all_in_range, "scanned networks are in range");
    }
    check(astralia::network_parse_networks("Dup:WPA2:30:\nDup:WPA2:90:*\n", {}).at("Dup").connected, "a connected duplicate marks the network");
    check(astralia::network_parse_networks("My\\:Network:WPA2:50:\n", {}).contains("My:Network"), "escaped colon in an SSID");
    {
        NetworkMap saved = astralia::network_parse_networks("", {"SavedNet"});
        check(saved.size() == 1 && saved.at("SavedNet").existing && !saved.at("SavedNet").in_range && !saved.at("SavedNet").connected, "saved profile out of range");
    }
    check(astralia::network_parse_networks("garbage:noenoughfields\n", {}).empty(), "short lines are dropped");

    astralia::NetworkDeviceStatus both = astralia::network_parse_device_status("wlan0:wifi:connected:MyWifi\neth0:ethernet:connected:Wired connection 1\nlo:loopback:unmanaged:--\n");
    check(both.wifi && both.ethernet && both.ethernet_connected && both.ethernet_name == "Wired connection 1", "wifi and ethernet device status");
    astralia::NetworkDeviceStatus connecting = astralia::network_parse_device_status("eth0:ethernet:connecting:--\n");
    check(connecting.ethernet && !connecting.ethernet_connected, "ethernet still connecting");
    astralia::NetworkDeviceStatus unavailable = astralia::network_parse_device_status("wlan0:wifi:unavailable:--\n");
    check(unavailable.wifi && !unavailable.ethernet, "wifi unavailable still counts as present");
    astralia::NetworkDeviceStatus colon = astralia::network_parse_device_status("eth0:ethernet:connected:Lab\\:Wired\n");
    check(colon.ethernet_name == "Lab:Wired", "escaped colon in a connection name");

    check(astralia::network_connectivity_name(2) == "portal" && astralia::network_connectivity_name(4) == "full" && astralia::network_connectivity_name(3) == "limited" && astralia::network_connectivity_name(1) == "none" && astralia::network_connectivity_name(0) == "unknown", "connectivity names");

    NetworkInfo connected;
    connected.connected = true;
    NetworkInfo saved_in_range;
    saved_in_range.existing = true;
    saved_in_range.in_range = true;
    NetworkInfo available;
    NetworkInfo stub;
    stub.existing = true;
    NetworkMap visible{{"A", connected}, {"B", saved_in_range}, {"C", available}, {"D", stub}};
    check(astralia::network_visible_count(visible) == 3, "stubs are not visible");

    NetworkInfo a;
    a.connected = true;
    a.in_range = true;
    NetworkInfo b;
    b.existing = true;
    b.in_range = true;
    NetworkInfo c;
    c.in_range = true;
    NetworkMap populated{{"A", a}, {"B", b}, {"C", c}};
    check(astralia::network_scan_would_collapse(populated, {{"A", a}}), "self-only scan collapses");
    check(!astralia::network_scan_would_collapse(populated, {{"Z", c}}), "neighbour-only scan is kept");
    check(!astralia::network_scan_would_collapse(populated, {{"A", a}, {"B", b}}), "fuller scan is kept");
    check(!astralia::network_scan_would_collapse({{"Only", a}}, {{"A", a}}), "a sparse list never collapses");
}

void check_notification() {
    using namespace std::chrono_literals;
    using test::check;
    check(astralia::notification_timeout(-1) == astralia::notification_hang_time, "default expiry uses the hang time");
    check(astralia::notification_timeout(0) == astralia::notification_hang_time, "zero expiry uses the hang time");
    check(astralia::notification_timeout(1500) == 1500ms, "an explicit expiry is honored");
    astralia::Notification critical;
    critical.urgency = astralia::notification_urgency_critical;
    astralia::Notification normal;
    check(critical.critical() && !normal.critical(), "urgency 2 is critical");
}

void check_user_and_icons() {
    using test::check;
    check(astralia::user_parse_os_name("NAME=\"Arch Linux\"\nPRETTY_NAME=\"Arch Linux\"\nID=arch\n") == "Arch Linux", "quoted pretty name");
    check(astralia::user_parse_os_name("PRETTY_NAME=Plain\n") == "Plain", "unquoted pretty name");
    check(astralia::user_parse_os_name("ID=arch\n") == "Linux", "missing pretty name falls back");
    check(astralia::icon_theme_order("Papirus").front() == "Papirus" && astralia::icon_theme_order("").back() == "hicolor", "icon theme order");
    check(astralia::icon_theme_order("hicolor").front() != "hicolor", "hicolor is always last");
    check(astralia::icon_direct_path("/a/b.png") == "/a/b.png" && astralia::icon_direct_path("/a/b.svg").empty() && astralia::icon_direct_path("name").empty(), "direct icon paths are absolute PNG files");
    check(astralia::resolve_app_icon_path("").empty(), "an empty icon field resolves to nothing");
}

void check_audio_percent() {
    using astralia::audio_percent;
    std::array<float, 2> forty{0.064f, 0.064f};
    test::check(audio_percent(forty) == 40, "audio equal channels");
    std::array<float, 2> uneven{0.0f, 1.0f};
    test::check(audio_percent(uneven) == 50, "audio uneven channels average");
    std::array<float, 1> full{1.0f};
    test::check(audio_percent(full) == 100, "audio full volume");
    test::check(audio_percent({}) == 0, "audio no channels");
}
