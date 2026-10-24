#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <wayland-client.h>

#include "wayland/core/log.h"

#include "service/wayland/clipboard_service.h"
#include "service/wayland/input_service.h"

namespace {

constexpr size_t max_paste = 1 << 20;
constexpr std::array<const char *, 5> mimes = {"text/plain;charset=utf-8", "text/plain",
                                               "UTF8_STRING", "TEXT", "STRING"};

int mime_rank(const char *mime) {
    for (size_t i = 0; i < mimes.size(); ++i)
        if (std::strcmp(mime, mimes[i]) == 0)
            return static_cast<int>(mimes.size() - i);
    return 0;
}

void offer_mime(void *data, wl_data_offer *offer, const char *mime) {
    static_cast<ClipboardService *>(data)->handle_offer_mime(offer, mime);
}

void offer_source_actions(void *, wl_data_offer *, uint32_t) {}

void offer_action(void *, wl_data_offer *, uint32_t) {}

constexpr wl_data_offer_listener offer_listener = {
    .offer = offer_mime,
    .source_actions = offer_source_actions,
    .action = offer_action,
};

void device_data_offer(void *data, wl_data_device *, wl_data_offer *offer) {
    static_cast<ClipboardService *>(data)->handle_data_offer(offer);
    wl_data_offer_add_listener(offer, &offer_listener, data);
}

void device_enter(void *, wl_data_device *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t,
                  wl_data_offer *) {}

void device_leave(void *, wl_data_device *) {}

void device_motion(void *, wl_data_device *, uint32_t, wl_fixed_t, wl_fixed_t) {}

void device_drop(void *, wl_data_device *) {}

void device_selection(void *data, wl_data_device *, wl_data_offer *offer) {
    static_cast<ClipboardService *>(data)->handle_selection(offer);
}

constexpr wl_data_device_listener device_listener = {
    .data_offer = device_data_offer,
    .enter = device_enter,
    .leave = device_leave,
    .motion = device_motion,
    .drop = device_drop,
    .selection = device_selection,
};

void source_target(void *, wl_data_source *, const char *) {}

void source_send(void *data, wl_data_source *source, const char *mime, int32_t fd) {
    static_cast<ClipboardService *>(data)->handle_send(source, mime, fd);
}

void source_cancelled(void *data, wl_data_source *source) {
    static_cast<ClipboardService *>(data)->handle_cancelled(source);
}

void source_dnd_drop_performed(void *, wl_data_source *) {}

void source_dnd_finished(void *, wl_data_source *) {}

void source_action(void *, wl_data_source *, uint32_t) {}

constexpr wl_data_source_listener source_listener = {
    .target = source_target,
    .send = source_send,
    .cancelled = source_cancelled,
    .dnd_drop_performed = source_dnd_drop_performed,
    .dnd_finished = source_dnd_finished,
    .action = source_action,
};

} // namespace

ClipboardService::~ClipboardService() { cleanup(); }

bool ClipboardService::bind(wl_data_device_manager *manager, wl_seat *seat, wl_display *display,
                            const KeyboardState *keyboard) {
    if (!manager || !seat)
        return false;
    manager_ = manager;
    display_ = display;
    keyboard_ = keyboard;
    device_ = wl_data_device_manager_get_data_device(manager, seat);
    wl_data_device_add_listener(device_, &device_listener, this);
    return true;
}

void ClipboardService::cleanup() {
    finish_read();
    if (source_) {
        wl_data_source_destroy(source_);
        source_ = nullptr;
    }
    for (auto &[offer, info] : offers_)
        wl_data_offer_destroy(offer);
    offers_.clear();
    selection_ = nullptr;
    if (device_) {
        if (wl_data_device_get_version(device_) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
            wl_data_device_release(device_);
        else
            wl_data_device_destroy(device_);
        device_ = nullptr;
    }
    manager_ = nullptr;
}

void ClipboardService::set(std::string text) {
    if (!manager_ || !device_) {
        klog("clipboard: wl_data_device_manager unavailable");
        return;
    }
    wl_data_source *previous = source_;
    source_ = wl_data_device_manager_create_data_source(manager_);
    wl_data_source_add_listener(source_, &source_listener, this);
    for (const char *mime : mimes)
        wl_data_source_offer(source_, mime);
    text_ = std::move(text);
    wl_data_device_set_selection(device_, source_, keyboard_ ? keyboard_->last_key_serial : 0);
    if (previous)
        wl_data_source_destroy(previous);
    if (display_)
        wl_display_flush(display_);
}

void ClipboardService::request(std::function<void(std::string)> done) {
    finish_read();
    if (source_) {
        done(text_);
        return;
    }
    auto it = selection_ ? offers_.find(selection_) : offers_.end();
    if (it == offers_.end() || it->second.rank == 0) {
        done({});
        return;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        done({});
        return;
    }
    wl_data_offer_receive(selection_, it->second.mime.c_str(), fds[1]);
    close(fds[1]);
    if (display_)
        wl_display_flush(display_);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    read_fd_ = fds[0];
    received_.clear();
    done_ = std::move(done);
}

void ClipboardService::read_ready() {
    if (read_fd_ < 0)
        return;
    std::array<char, 4096> buf;
    for (;;) {
        ssize_t n = read(read_fd_, buf.data(), buf.size());
        if (n > 0) {
            received_.append(buf.data(), static_cast<size_t>(n));
            if (received_.size() >= max_paste)
                break;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        break;
    }
    std::string text = std::move(received_);
    std::function<void(std::string)> done = std::move(done_);
    finish_read();
    if (done)
        done(std::move(text));
}

void ClipboardService::finish_read() {
    if (read_fd_ >= 0) {
        close(read_fd_);
        read_fd_ = -1;
    }
    received_.clear();
    done_ = nullptr;
}

void ClipboardService::handle_data_offer(wl_data_offer *offer) { offers_[offer] = {}; }

void ClipboardService::handle_offer_mime(wl_data_offer *offer, const char *mime) {
    auto it = offers_.find(offer);
    if (it == offers_.end())
        return;
    int rank = mime_rank(mime);
    if (rank > it->second.rank)
        it->second = {mime, rank};
}

void ClipboardService::drop_offer(wl_data_offer *offer) {
    offers_.erase(offer);
    wl_data_offer_destroy(offer);
}

void ClipboardService::handle_selection(wl_data_offer *offer) {
    if (selection_ && selection_ != offer)
        drop_offer(selection_);
    selection_ = offer;
    std::vector<wl_data_offer *> stale;
    for (auto &[known, info] : offers_)
        if (known != selection_)
            stale.push_back(known);
    for (wl_data_offer *known : stale)
        drop_offer(known);
}

void ClipboardService::handle_send(wl_data_source *source, const char *, int fd) {
    if (source == source_) {
        size_t written = 0;
        while (written < text_.size()) {
            ssize_t n = write(fd, text_.data() + written, text_.size() - written);
            if (n > 0) {
                written += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
    }
    close(fd);
}

void ClipboardService::handle_cancelled(wl_data_source *source) {
    if (source == source_)
        source_ = nullptr;
    wl_data_source_destroy(source);
}
