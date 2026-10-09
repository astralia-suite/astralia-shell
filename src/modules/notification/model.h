#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/animation.h"

#include "service/notification_service.h"

#include "ui/canvas.h"

namespace astralia {

struct NotificationEntry {
    uint32_t id = 0;
    std::string app;
    std::string summary;
    std::string body;
    uint8_t urgency = notification_urgency_normal;
    float opacity = 0.0f;
    float slide_offset = 0.0f;
    float progress = 1.0f;
    bool exiting = false;
    bool timed = false;
};

class NotificationModel {
  public:
    void sync(const std::vector<Notification> &list);
    void sync(const NotificationService &service) { sync(service.list()); }
    void tick(std::chrono::steady_clock::time_point now) { animations_.tick(now); }
    bool animating() const { return animations_.hasActive(); }
    const std::vector<NotificationEntry> &entries() const { return entries_; }

  private:
    NotificationEntry *find(uint32_t id);
    void add(const Notification &notification);
    void start_exit(uint32_t id);
    void start_progress(uint32_t id, std::chrono::milliseconds timeout);

    std::vector<NotificationEntry> entries_;
    AnimationManager animations_;
};

class NotificationViewState {
  public:
    bool hide_locally(uint32_t id);
    bool set_hover(std::optional<uint32_t> id);
    void tick(std::chrono::steady_clock::time_point now) { animations_.tick(now); }
    void forget_missing(const std::vector<NotificationEntry> &entries);
    bool animating() const { return animations_.hasActive(); }
    uint32_t hovered() const { return hovered_; }
    float local_opacity(uint32_t id) const;
    bool fading(uint32_t id) const { return local_exit_.contains(id); }

  private:
    uint32_t hovered_ = 0;
    AnimationManager animations_;
    std::unordered_map<uint32_t, float> local_exit_;
};

struct NotificationCard {
    const NotificationEntry *entry = nullptr;
    float y = 0.0f;
    float height = 0.0f;
    float header = 0.0f;
    float summary = 0.0f;
    float body = 0.0f;
};

struct NotificationLayout {
    std::vector<NotificationCard> cards;
    float height = 0.0f;
};

NotificationLayout layout_notifications(ui::Canvas &canvas, const NotificationModel &model, const NotificationViewState &view, float max_height);

std::optional<uint32_t> notification_close_at(const NotificationLayout &layout, const NotificationViewState &view, double x, double y);

std::vector<std::pair<uint32_t, ui::Box>> notification_close_boxes(const NotificationLayout &layout, const NotificationViewState &view);

float notification_card_height(float header, float summary, float body);

} // namespace astralia
