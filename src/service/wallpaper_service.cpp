#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/path_home.h"

#include "service/wallpaper_service.h"

namespace astralia {

namespace {

std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

template <typename Map>
const typename Map::mapped_type *lookup(const Map &map, const std::string &monitor_name) {
    auto it = map.find(monitor_name);
    return it != map.end() ? &it->second : nullptr;
}

using ColumnMap = std::map<std::string, std::vector<std::string>>;
using CountMap = std::map<std::string, int>;

const ColumnMap &column_map(const Config &cfg, WallpaperSlot slot) {
    switch (slot) {
    case WallpaperSlot::animated:
        return cfg.wallpaper_animated_columns;
    case WallpaperSlot::lock:
        return cfg.lock_wallpaper_columns;
    case WallpaperSlot::still:
        break;
    }
    return cfg.wallpaper_columns;
}

ColumnMap &column_map(Config &cfg, WallpaperSlot slot) {
    return const_cast<ColumnMap &>(column_map(std::as_const(cfg), slot));
}

const ColumnMap &fill_mode_map(const Config &cfg, WallpaperSlot slot) {
    switch (slot) {
    case WallpaperSlot::animated:
        return cfg.wallpaper_animated_fill_modes;
    case WallpaperSlot::lock:
        return cfg.lock_wallpaper_fill_modes;
    case WallpaperSlot::still:
        break;
    }
    return cfg.wallpaper_fill_modes;
}

ColumnMap &fill_mode_map(Config &cfg, WallpaperSlot slot) {
    return const_cast<ColumnMap &>(fill_mode_map(std::as_const(cfg), slot));
}

const CountMap &count_map(const Config &cfg, WallpaperSlot slot) {
    switch (slot) {
    case WallpaperSlot::animated:
        return cfg.wallpaper_animated_column_counts;
    case WallpaperSlot::lock:
        return cfg.lock_wallpaper_column_counts;
    case WallpaperSlot::still:
        break;
    }
    return cfg.wallpaper_column_counts;
}

CountMap &count_map(Config &cfg, WallpaperSlot slot) {
    return const_cast<CountMap &>(count_map(std::as_const(cfg), slot));
}

} // namespace

WallpaperSlot wallpaper_slot_of(bool animated) {
    return animated ? WallpaperSlot::animated : WallpaperSlot::still;
}

WallpaperSlot wallpaper_lock_slot(const Config &cfg) {
    return cfg.lock_wallpaper_custom ? WallpaperSlot::lock : WallpaperSlot::still;
}

const std::string &wallpaper_slot_dir(const Config &cfg, WallpaperSlot slot) {
    switch (slot) {
    case WallpaperSlot::animated:
        return cfg.wallpaper_animated_dir;
    case WallpaperSlot::lock:
        return cfg.lock_wallpaper_dir;
    case WallpaperSlot::still:
        break;
    }
    return cfg.wallpaper_dir;
}

bool wallpaper_is_image(const std::string &path) {
    std::string extension = lowered(std::filesystem::path(path).extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".jfif" || extension == ".bmp" || extension == ".svg";
}

bool wallpaper_is_video(const std::string &path) {
    std::string extension = lowered(std::filesystem::path(path).extension().string());
    return extension == ".mp4" || extension == ".webm" || extension == ".mkv";
}

bool wallpaper_picker_less(const std::string &a, const std::string &b) {
    std::filesystem::path pa(a);
    std::filesystem::path pb(b);
    std::string ea = lowered(pa.extension().string());
    std::string eb = lowered(pb.extension().string());
    if (ea != eb) {
        return ea < eb;
    }
    return lowered(pa.filename().string()) < lowered(pb.filename().string());
}

std::vector<std::string> wallpaper_list_files(const std::string &dir, bool animated,
                                              std::size_t limit) {
    std::vector<std::string> found;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        path_expand_home(dir), std::filesystem::directory_options::skip_permission_denied, error);
    for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (it.depth() >= 1) {
            it.disable_recursion_pending();
        }
        const std::string path = it->path().string();
        if (it->is_regular_file(error) &&
            (animated ? wallpaper_is_video(path) : wallpaper_is_image(path))) {
            found.push_back(path);
        }
    }
    std::ranges::sort(found, wallpaper_picker_less);
    if (found.size() > limit) {
        found.resize(limit);
    }
    return found;
}

int wallpaper_column_count(const Config &cfg, const std::string &monitor_name, WallpaperSlot slot) {
    const int *count = lookup(count_map(cfg, slot), monitor_name);
    return count != nullptr && *count > 0 ? *count : 1;
}

std::string wallpaper_column_override(const Config &cfg, const std::string &monitor_name,
                                      int column_index, WallpaperSlot slot) {
    const std::vector<std::string> *columns = lookup(column_map(cfg, slot), monitor_name);
    if (columns != nullptr && column_index >= 0 &&
        static_cast<std::size_t>(column_index) < columns->size()) {
        return (*columns)[static_cast<std::size_t>(column_index)];
    }
    return "";
}

std::string wallpaper_column_path(const Config &cfg, const std::string &monitor_name,
                                  int column_index, WallpaperSlot slot) {
    std::string override = wallpaper_column_override(cfg, monitor_name, column_index, slot);
    if (!override.empty()) {
        return override;
    }
    return cfg.default_wallpaper ? cfg.wallpaper_path : "";
}

std::string wallpaper_fill_mode(const Config &cfg, const std::string &monitor_name,
                                int column_index, WallpaperSlot slot) {
    const std::vector<std::string> *modes = lookup(fill_mode_map(cfg, slot), monitor_name);
    if (modes != nullptr && column_index >= 0 &&
        static_cast<std::size_t>(column_index) < modes->size() &&
        !(*modes)[static_cast<std::size_t>(column_index)].empty()) {
        return (*modes)[static_cast<std::size_t>(column_index)];
    }
    return "crop";
}

void wallpaper_set_column(Config &cfg, const std::string &monitor_name, int column_index,
                          const std::string &path, WallpaperSlot slot) {
    std::vector<std::string> &columns = column_map(cfg, slot)[monitor_name];
    if (static_cast<std::size_t>(column_index) >= columns.size()) {
        columns.resize(static_cast<std::size_t>(column_index) + 1);
    }
    columns[static_cast<std::size_t>(column_index)] = path;
}

void wallpaper_set_fill_mode(Config &cfg, const std::string &monitor_name, int column_index,
                             const std::string &mode, WallpaperSlot slot) {
    std::vector<std::string> &modes = fill_mode_map(cfg, slot)[monitor_name];
    if (static_cast<std::size_t>(column_index) >= modes.size()) {
        modes.resize(static_cast<std::size_t>(column_index) + 1);
    }
    modes[static_cast<std::size_t>(column_index)] = mode;
}

void wallpaper_set_column_count(Config &cfg, const std::string &monitor_name, int count,
                                WallpaperSlot slot) {
    count_map(cfg, slot)[monitor_name] = count;
}

int wallpaper_column_count(const Config &cfg, const std::string &monitor_name, bool animated) {
    return wallpaper_column_count(cfg, monitor_name, wallpaper_slot_of(animated));
}

std::string wallpaper_column_override(const Config &cfg, const std::string &monitor_name,
                                      int column_index, bool animated) {
    return wallpaper_column_override(cfg, monitor_name, column_index, wallpaper_slot_of(animated));
}

std::string wallpaper_column_path(const Config &cfg, const std::string &monitor_name,
                                  int column_index, bool animated) {
    return wallpaper_column_path(cfg, monitor_name, column_index, wallpaper_slot_of(animated));
}

std::string wallpaper_fill_mode(const Config &cfg, const std::string &monitor_name,
                                int column_index, bool animated) {
    return wallpaper_fill_mode(cfg, monitor_name, column_index, wallpaper_slot_of(animated));
}

void wallpaper_set_column(Config &cfg, const std::string &monitor_name, int column_index,
                          const std::string &path, bool animated) {
    wallpaper_set_column(cfg, monitor_name, column_index, path, wallpaper_slot_of(animated));
}

void wallpaper_set_fill_mode(Config &cfg, const std::string &monitor_name, int column_index,
                             const std::string &mode, bool animated) {
    wallpaper_set_fill_mode(cfg, monitor_name, column_index, mode, wallpaper_slot_of(animated));
}

void wallpaper_set_column_count(Config &cfg, const std::string &monitor_name, int count,
                                bool animated) {
    wallpaper_set_column_count(cfg, monitor_name, count, wallpaper_slot_of(animated));
}

std::string wallpaper_image_for(const Config &cfg, const std::string &monitor_name) {
    return wallpaper_column_path(cfg, monitor_name, 0, WallpaperSlot::still);
}

void wallpaper_set_image(Config &cfg, const std::string &monitor_name, const std::string &path) {
    std::vector<std::string> &columns = cfg.wallpaper_columns[monitor_name];
    if (columns.empty()) {
        columns.resize(1);
    }
    columns[0] = path;
}

void wallpaper_clear_image(Config &cfg, const std::string &monitor_name) {
    auto it = cfg.wallpaper_columns.find(monitor_name);
    if (it == cfg.wallpaper_columns.end()) {
        return;
    }
    if (it->second.size() > 1) {
        it->second[0].clear();
    } else {
        cfg.wallpaper_columns.erase(it);
    }
}

} // namespace astralia
