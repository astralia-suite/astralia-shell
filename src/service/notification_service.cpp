#include <algorithm>
#include <tuple>
#include <utility>

#include "core/log.h"

#include "service/notification_service.h"

namespace astralia {

namespace {

constexpr const char *bus_name = "org.freedesktop.Notifications";
constexpr const char *object_path = "/org/freedesktop/Notifications";
constexpr const char *interface = "org.freedesktop.Notifications";

// Close reasons
constexpr uint32_t reason_expired = 1;
constexpr uint32_t reason_dismissed = 2;
constexpr uint32_t reason_closed = 3;

} // namespace

std::chrono::milliseconds notification_timeout(int32_t expire_timeout_ms) {
    return expire_timeout_ms > 0 ? std::chrono::milliseconds(expire_timeout_ms) : notification_hang_time;
}

NotificationService::NotificationService(Reactor &loop)
    : loop_(loop), bus_(loop, BusKind::session) {
    timer_ = loop_.add_timer([this] { return until_next(); }, [this] { expire(); });
    sdbus::IConnection *conn = bus_.conn();
    if (conn == nullptr) {
        return;
    }
    try {
        object_ = sdbus::createObject(*conn, sdbus::ObjectPath(object_path));
        object_
            ->addVTable(
                sdbus::registerMethod("Notify").implementedAs(
                    [this](const std::string &app, uint32_t replaces_id, const std::string &,
                           const std::string &summary, const std::string &body,
                           const std::vector<std::string> &,
                           const std::map<std::string, sdbus::Variant> &hints, int32_t expire_timeout) {
                        return notify(app, replaces_id, summary, body, hints, expire_timeout);
                    }),
                sdbus::registerMethod("CloseNotification").implementedAs([this](uint32_t id) {
                    close(id, reason_closed);
                }),
                sdbus::registerMethod("GetCapabilities").implementedAs([] {
                    return std::vector<std::string>{"body"};
                }),
                sdbus::registerMethod("GetServerInformation").implementedAs([] {
                    return std::make_tuple(std::string("astralia"), std::string("astralia"),
                                           std::string("0.1.0"), std::string("1.2"));
                }),
                sdbus::registerSignal("NotificationClosed").withParameters<uint32_t, uint32_t>())
            .forInterface(interface);
        conn->requestName(sdbus::ServiceName(bus_name));
    } catch (const sdbus::Error &error) {
        log::error("notification: cannot own {}: {}", bus_name, error.what());
        object_.reset();
    }
}

void NotificationService::dismiss(uint32_t id) { close(id, reason_dismissed); }

uint32_t NotificationService::post(std::string app, std::string summary, std::string body, int32_t expire_timeout) {
    return notify(std::move(app), 0, std::move(summary), std::move(body), {}, expire_timeout);
}

uint32_t NotificationService::notify(std::string app, uint32_t replaces_id, std::string summary,
                                     std::string body,
                                     const std::map<std::string, sdbus::Variant> &hints,
                                     int32_t expire_timeout) {
    uint8_t urgency = notification_urgency_normal;
    if (auto hint = hints.find("urgency"); hint != hints.end() && hint->second.containsValueOfType<uint8_t>()) {
        urgency = hint->second.get<uint8_t>();
    }
    std::chrono::milliseconds timeout = notification_timeout(expire_timeout);
    auto now = std::chrono::steady_clock::now();
    Notification entry{0, std::move(app), std::move(summary), std::move(body), urgency, timeout, now, now + timeout};
    auto replaced = std::ranges::find(list_, replaces_id, &Notification::id);
    if (replaces_id != 0 && replaced != list_.end()) {
        entry.id = replaces_id;
        *replaced = std::move(entry);
        loop_.reschedule(timer_);
        changed.emit();
        return replaces_id;
    }
    entry.id = next_id_++;
    uint32_t id = entry.id;
    insert(std::move(entry));
    return id;
}

void NotificationService::insert(Notification entry) {
    list_.push_back(std::move(entry));
    loop_.reschedule(timer_);
    changed.emit();
}

void NotificationService::close(uint32_t id, uint32_t reason) {
    auto it = std::ranges::find(list_, id, &Notification::id);
    if (it == list_.end()) {
        return;
    }
    list_.erase(it);
    if (object_) {
        try {
            object_->emitSignal("NotificationClosed").onInterface(interface).withArguments(id, reason);
        } catch (const sdbus::Error &error) {
            log::error("notification: cannot emit NotificationClosed: {}", error.what());
        }
    }
    changed.emit();
}

void NotificationService::expire() {
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> expired;
    for (const Notification &entry : list_) {
        if (entry.deadline <= now) {
            expired.push_back(entry.id);
        }
    }
    for (uint32_t id : expired) {
        close(id, reason_expired);
    }
}

std::chrono::milliseconds NotificationService::until_next() const {
    if (list_.empty()) {
        return std::chrono::hours(1);
    }
    auto earliest = std::ranges::min_element(list_, {}, &Notification::deadline)->deadline;
    return std::max(std::chrono::ceil<std::chrono::milliseconds>(earliest - std::chrono::steady_clock::now()), std::chrono::milliseconds(0));
}

} // namespace astralia
