#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "core/clipboard.h"

struct wl_data_device;
struct wl_data_device_manager;
struct wl_data_offer;
struct wl_data_source;
struct wl_display;
struct wl_seat;

struct KeyboardState;

class ClipboardService final : public astralia::Clipboard {
  public:
    ~ClipboardService() override;

    bool bind(wl_data_device_manager *manager, wl_seat *seat, wl_display *display,
              const KeyboardState *keyboard);
    void cleanup();

    void set(std::string text) override;
    void request(std::function<void(std::string)> done) override;

    int read_fd() const { return read_fd_; }
    void read_ready();

    void handle_data_offer(wl_data_offer *offer);
    void handle_offer_mime(wl_data_offer *offer, const char *mime);
    void handle_selection(wl_data_offer *offer);
    void handle_send(wl_data_source *source, const char *mime, int fd);
    void handle_cancelled(wl_data_source *source);

  private:
    struct OfferInfo {
        std::string mime;
        int rank = 0;
    };

    void finish_read();
    void drop_offer(wl_data_offer *offer);

    wl_data_device_manager *manager_ = nullptr;
    wl_data_device *device_ = nullptr;
    wl_display *display_ = nullptr;
    const KeyboardState *keyboard_ = nullptr;
    wl_data_source *source_ = nullptr;
    wl_data_offer *selection_ = nullptr;
    std::unordered_map<wl_data_offer *, OfferInfo> offers_;
    std::string text_;
    int read_fd_ = -1;
    std::string received_;
    std::function<void(std::string)> done_;
};
