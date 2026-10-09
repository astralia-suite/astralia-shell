#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

#include "core/dbus.h"
#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

inline constexpr std::chrono::milliseconds notification_hang_time{5000};
inline constexpr uint8_t notification_urgency_normal = 1;
inline constexpr uint8_t notification_urgency_critical = 2;

struct Notification {
    uint32_t id;
    std::string app;
    std::string summary;
    std::string body;
    uint8_t urgency = notification_urgency_normal;
    std::chrono::milliseconds timeout = notification_hang_time;
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point deadline;

    bool critical() const { return urgency == notification_urgency_critical; }
};

std::chrono::milliseconds notification_timeout(int32_t expire_timeout_ms);

class NotificationService {
  public:
    explicit NotificationService(Reactor &loop);
    NotificationService(const NotificationService &) = delete;
    NotificationService &operator=(const NotificationService &) = delete;

    const std::vector<Notification> &list() const { return list_; }
    void dismiss(uint32_t id);
    uint32_t post(std::string app, std::string summary, std::string body, int32_t expire_timeout = -1);

    Signal<> changed;

  private:
    uint32_t notify(std::string app, uint32_t replaces_id, std::string summary, std::string body,
                    const std::map<std::string, sdbus::Variant> &hints, int32_t expire_timeout);
    void close(uint32_t id, uint32_t reason);
    void expire();
    std::chrono::milliseconds until_next() const;
    void insert(Notification entry);

    Reactor &loop_;
    SystemBus bus_;
    std::unique_ptr<sdbus::IObject> object_;
    std::vector<Notification> list_;
    uint32_t next_id_ = 1;
    int timer_ = -1;
};

} // namespace astralia
