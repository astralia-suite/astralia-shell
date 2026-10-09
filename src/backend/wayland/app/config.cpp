#include <cstdio>
#include <optional>
#include <sys/inotify.h>
#include <unistd.h>

#include "wayland/app/config.h"

#include "core/config_file.h"

#include "wayland/core/log.h"

std::string config_path() { return astralia::user_config_path("astralia/config.json"); }

Config load_config() {
    std::optional<std::string> text = astralia::read_text_file(config_path());
    return text ? astralia::parse_config(*text) : Config();
}

void save_config(const Config &cfg) {
    if (!astralia::write_text_file(config_path(), astralia::serialize_config(cfg)))
        klog("config: failed to save %s", config_path().c_str());
}

int config_watch_init(const std::string &path) {
    if (path.empty())
        return -1;
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
        return -1;
    if (inotify_add_watch(fd, path.c_str(), IN_MODIFY | IN_CLOSE_WRITE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

ConfigWatchEvent config_watch_poll(int fd) {
    char buf[4096] __attribute__((aligned(alignof(struct inotify_event))));
    ConfigWatchEvent result;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (char *p = buf; p < buf + n;) {
            auto *ev = reinterpret_cast<struct inotify_event *>(p);
            if (ev->mask & IN_IGNORED)
                result.removed = true;
            else
                result.changed = true;
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    return result;
}
