#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "wayland/core/log.h"
#include "wayland/plugin/media_plugin.h"
#include "wayland/render/image.h"

#include "service/wayland/media_service.h"

namespace {

constexpr uintmax_t rgba_cache_max_bytes = 512ull * 1024 * 1024;

using StreamFn = decltype(&astralia_shell_media_plugin_stream);
using FramesFn = decltype(&astralia_shell_media_plugin_frames);
using ReleaseDrmFrameFn = decltype(&astralia_shell_media_plugin_release_drm_frame);

struct Plugin {
    StreamFn stream = nullptr;
    FramesFn frames = nullptr;
    ReleaseDrmFrameFn release_drm_frame = nullptr;
};

const Plugin &plugin() {
    static const Plugin loaded = [] {
        Plugin p;
        const char *candidates[] = {
            ASTRALIA_SHELL_MEDIA_PLUGIN,
            "build/libastralia-shell-media.so",
        };
        void *lib = nullptr;
        for (const char *path : candidates) {
            lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (lib)
                break;
        }
        if (!lib) {
            klog("media_decode: plugin not available (%s); animated content "
                 "disabled",
                 dlerror());
            return p;
        }
        p.stream = reinterpret_cast<StreamFn>(dlsym(lib, "astralia_shell_media_plugin_stream"));
        p.frames = reinterpret_cast<FramesFn>(dlsym(lib, "astralia_shell_media_plugin_frames"));
        p.release_drm_frame = reinterpret_cast<ReleaseDrmFrameFn>(
            dlsym(lib, "astralia_shell_media_plugin_release_drm_frame"));
        if (!p.stream || !p.frames || !p.release_drm_frame) {
            klog("media_decode: plugin missing expected symbols (%s); animated "
                 "content disabled",
                 dlerror());
            p = {};
        }
        return p;
    }();
    return loaded;
}

std::string rgba_cache_dir() {
    std::string base = animate_cache_home_dir();
    for (size_t pos = 1; pos <= base.size(); ++pos) {
        if (pos == base.size() || base[pos] == '/')
            mkdir(base.substr(0, pos).c_str(), 0755);
    }
    std::string dir = base + "/astralia-shell";
    mkdir(dir.c_str(), 0755);
    dir += "/wallpaper";
    mkdir(dir.c_str(), 0755);
    return dir;
}

std::string rgba_cache_path(const std::string &path, time_t mtime, int target_w, int target_h) {
    std::string key = path + ":" + std::to_string(mtime) + ":" + std::to_string(target_w) + "x" +
                      std::to_string(target_h);
    size_t hash = std::hash<std::string>{}(key);
    return rgba_cache_dir() + "/" + std::to_string(hash) + ".rgba";
}

unsigned char *rgba_read(const std::string &cache_path, int &out_width, int &out_height) {
    FILE *fp = fopen(cache_path.c_str(), "rb");
    if (!fp)
        return nullptr;
    uint32_t header[2];
    if (fread(header, sizeof(uint32_t), 2, fp) != 2) {
        fclose(fp);
        return nullptr;
    }
    int width = static_cast<int>(header[0]);
    int height = static_cast<int>(header[1]);
    size_t pixel_bytes = static_cast<size_t>(width) * height * 4;
    struct stat st{};
    if (width <= 0 || height <= 0 || fstat(fileno(fp), &st) != 0 ||
        static_cast<size_t>(st.st_size) != sizeof(header) + pixel_bytes) {
        fclose(fp);
        return nullptr;
    }
    auto *data = new unsigned char[pixel_bytes];
    size_t read = fread(data, 1, pixel_bytes, fp);
    fclose(fp);
    if (read != pixel_bytes) {
        delete[] data;
        return nullptr;
    }
    utimensat(AT_FDCWD, cache_path.c_str(), nullptr, 0);
    out_width = width;
    out_height = height;
    return data;
}

void rgba_prune(const std::filesystem::path &directory) {
    struct Entry {
        std::filesystem::path path;
        uintmax_t size;
        std::filesystem::file_time_type used;
    };
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    std::vector<Entry> entries;
    uintmax_t total = 0;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || entry.path().extension() != ".rgba")
            continue;
        uintmax_t size = entry.file_size(entry_error);
        entries.push_back({entry.path(), size, entry.last_write_time(entry_error)});
        total += size;
    }
    std::ranges::sort(entries, {}, &Entry::used);
    for (size_t i = 0; i + 1 < entries.size() && total > rgba_cache_max_bytes; ++i)
        if (std::filesystem::remove(entries[i].path, error))
            total -= entries[i].size;
}

void rgba_write(const std::string &cache_path, int width, int height, const unsigned char *data) {
    FILE *fp = fopen(cache_path.c_str(), "wb");
    if (!fp)
        return;
    uint32_t header[2] = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    fwrite(header, sizeof(uint32_t), 2, fp);
    fwrite(data, 1, static_cast<size_t>(width) * height * 4, fp);
    fclose(fp);
    rgba_prune(std::filesystem::path(cache_path).parent_path());
}

void box_downsample_rgba(const unsigned char *src, int sw, int sh, unsigned char *dst, int dw,
                         int dh) {
    for (int y = 0; y < dh; ++y) {
        int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
        int y1 = static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh);
        y1 = std::max(y1, y0 + 1);
        for (int x = 0; x < dw; ++x) {
            int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
            int x1 = static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw);
            x1 = std::max(x1, x0 + 1);
            long sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1 && sy < sh; ++sy) {
                const unsigned char *row = src + static_cast<size_t>(sy) * sw * 4;
                for (int sx = x0; sx < x1 && sx < sw; ++sx) {
                    const unsigned char *px = row + static_cast<size_t>(sx) * 4;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                    sum[3] += px[3];
                    ++count;
                }
            }
            count = std::max(count, 1);
            unsigned char *out = dst + (static_cast<size_t>(y) * dw + x) * 4;
            out[0] = static_cast<unsigned char>(sum[0] / count);
            out[1] = static_cast<unsigned char>(sum[1] / count);
            out[2] = static_cast<unsigned char>(sum[2] / count);
            out[3] = static_cast<unsigned char>(sum[3] / count);
        }
    }
}

unsigned char *decode_first_frame(const std::string &path, int &w, int &h, int target_w = 0,
                                  int target_h = 0) {
    unsigned char *data = load_image_decode(path, w, h, 0, target_w, target_h);
    if (data)
        return data;
    std::string filter = "format=rgba";
    if (target_w > 0 && target_h > 0)
        filter = "scale=" + std::to_string(target_w) + ":" + std::to_string(target_h) +
                 ":force_original_aspect_ratio=increase,format=rgba";
    std::vector<MediaFrame> frames = media_decode_frames(path, filter, 1);
    if (frames.empty() || frames[0].rgba.empty())
        return nullptr;
    w = frames[0].width;
    h = frames[0].height;
    auto *out = new unsigned char[frames[0].rgba.size()];
    std::memcpy(out, frames[0].rgba.data(), frames[0].rgba.size());
    return out;
}

} // namespace

MediaDecodePlayback media_decode_stream(const std::string &path, const std::string &filter_desc,
                                        int fps, bool supports_row_length,
                                        MediaFrameCallback on_frame,
                                        MediaDrmCallback on_drm_frame) {
    if (!plugin().stream)
        return {};
    return plugin().stream(path, filter_desc, fps, supports_row_length, std::move(on_frame),
                           std::move(on_drm_frame));
}

std::vector<MediaFrame> media_decode_frames(const std::string &path, const std::string &filter_desc,
                                            int max_frames) {
    if (!plugin().frames)
        return {};
    return plugin().frames(path, filter_desc, max_frames);
}

void media_decode_stop(MediaDecodePlayback &playback) {
    if (!playback.stop_flag)
        return;
    playback.stop_flag->store(true);
    if (playback.worker.joinable())
        playback.worker.join();
    playback.stop_flag.reset();
    playback.pause_flag.reset();
    playback.status.reset();
    playback.egl_import_failed.reset();
}

void media_decode_pause(MediaDecodePlayback &playback) {
    if (playback.pause_flag)
        playback.pause_flag->store(true);
}

void media_decode_resume(MediaDecodePlayback &playback) {
    if (playback.pause_flag)
        playback.pause_flag->store(false);
}

void media_decode_release_drm_frame(void *avframe_handle) {
    if (plugin().release_drm_frame)
        plugin().release_drm_frame(avframe_handle);
}

MediaDecodeStatus media_decode_status(const MediaDecodePlayback &playback) {
    return playback.status ? playback.status->load() : MediaDecodeStatus::Idle;
}

std::string animate_cache_home_dir() {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    if (cache_home && *cache_home)
        return cache_home;
    const char *home = getenv("HOME");
    return std::string(home ? home : "") + "/.cache";
}

AnimateSize animate_decode_size(int target_w, int target_h, int max_height) {
    if (target_w <= 0 || target_h <= 0 || max_height <= 0 || target_h <= max_height)
        return {std::max(target_w, 0), std::max(target_h, 0)};
    double ratio = static_cast<double>(max_height) / target_h;
    return {std::max(1, static_cast<int>(std::lround(target_w * ratio))), max_height};
}

std::string animate_scale_filter(int w, int h, AnimateFit fit) {
    std::string ws = std::to_string(w);
    std::string hs = std::to_string(h);
    if (fit == AnimateFit::Fit)
        return "scale=" + ws + ":" + hs + ":force_original_aspect_ratio=decrease,pad=" + ws + ":" +
               hs + ":(ow-iw)/2:(oh-ih)/2:color=black,format=rgba";
    return "scale=" + ws + ":" + hs + ":force_original_aspect_ratio=increase,crop=" + ws + ":" +
           hs + ",format=rgba";
}

unsigned char *animate_decode_scaled(const std::string &path, int target_w, int target_h,
                                     int &out_width, int &out_height) {
    struct stat st{};
    bool cacheable = target_w > 0 && target_h > 0 && stat(path.c_str(), &st) == 0;
    std::string cache_path;
    if (cacheable) {
        cache_path = rgba_cache_path(path, st.st_mtime, target_w, target_h);
        if (unsigned char *cached = rgba_read(cache_path, out_width, out_height))
            return cached;
    }

    unsigned char *data = decode_first_frame(path, out_width, out_height, target_w, target_h);
    if (!data)
        return nullptr;
    if (target_w <= 0 || target_h <= 0)
        return data;
    float scale = std::max(static_cast<float>(target_w) / out_width,
                           static_cast<float>(target_h) / out_height);
    unsigned char *result = data;
    if (scale < 1.0f) {
        int dw = std::max(1, static_cast<int>(std::lround(out_width * scale)));
        int dh = std::max(1, static_cast<int>(std::lround(out_height * scale)));
        auto *scaled = new unsigned char[static_cast<size_t>(dw) * dh * 4];
        box_downsample_rgba(data, out_width, out_height, scaled, dw, dh);
        delete[] data;
        out_width = dw;
        out_height = dh;
        result = scaled;
    }
    if (cacheable)
        rgba_write(cache_path, out_width, out_height, result);
    return result;
}
