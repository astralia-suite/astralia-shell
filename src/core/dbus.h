#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

#include "core/reactor.h"

namespace astralia {

enum class BusKind { system,
                     session };

class SystemBus {
  public:
    explicit SystemBus(Reactor &loop, BusKind kind = BusKind::system);
    ~SystemBus();
    SystemBus(const SystemBus &) = delete;
    SystemBus &operator=(const SystemBus &) = delete;

    sdbus::IConnection *conn() const { return conn_.get(); }
    sdbus::Slot add_match(const std::string &rule, std::function<void()> handler);
    std::unique_ptr<sdbus::IProxy> proxy(const std::string &service, const std::string &path);

  private:
    void drain();

    Reactor &loop_;
    std::unique_ptr<sdbus::IConnection> conn_;
    int fd_ = -1;
    int event_fd_ = -1;
};

using DbusProperties = std::map<std::string, sdbus::Variant>;

sdbus::Slot dbus_get_all_async(sdbus::IProxy &proxy, const std::string &interface,
                               std::function<void(const DbusProperties &)> done);

template <typename T>
std::optional<T> dbus_value(const DbusProperties &properties, const std::string &name) {
    auto it = properties.find(name);
    if (it == properties.end()) {
        return std::nullopt;
    }
    try {
        return it->second.template get<T>();
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> dbus_property(sdbus::IProxy *proxy, const std::string &interface,
                               const std::string &name) {
    if (proxy == nullptr) {
        return std::nullopt;
    }
    try {
        return proxy->getProperty(name).onInterface(interface).template get<T>();
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> dbus_property(sdbus::IConnection &conn, const std::string &service,
                               const std::string &path, const std::string &interface,
                               const std::string &name) {
    try {
        auto proxy = sdbus::createProxy(conn, sdbus::ServiceName(service), sdbus::ObjectPath(path));
        return dbus_property<T>(proxy.get(), interface, name);
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

} // namespace astralia
