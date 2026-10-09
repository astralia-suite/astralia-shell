#include <string>
#include <vector>

#include "service/wallpaper_service.h"

namespace astralia {

namespace {

template <typename Map>
const typename Map::mapped_type *lookup(const Map &map, const std::string &monitor_name) {
    auto it = map.find(monitor_name);
    return it != map.end() ? &it->second : nullptr;
}

const std::vector<std::string> *columns_for(const Config &cfg, const std::string &monitor_name, bool animated) {
    return lookup(animated ? cfg.wallpaper_animated_columns : cfg.wallpaper_columns, monitor_name);
}

const std::vector<std::string> *fill_modes_for(const Config &cfg, const std::string &monitor_name, bool animated) {
    return lookup(animated ? cfg.wallpaper_animated_fill_modes : cfg.wallpaper_fill_modes, monitor_name);
}

} // namespace

int wallpaper_column_count(const Config &cfg, const std::string &monitor_name, bool animated) {
    const int *count = lookup(animated ? cfg.wallpaper_animated_column_counts : cfg.wallpaper_column_counts, monitor_name);
    return count != nullptr && *count > 0 ? *count : 1;
}

std::string wallpaper_column_override(const Config &cfg, const std::string &monitor_name, int column_index, bool animated) {
    const std::vector<std::string> *columns = columns_for(cfg, monitor_name, animated);
    if (columns != nullptr && column_index >= 0 && static_cast<std::size_t>(column_index) < columns->size()) {
        return (*columns)[static_cast<std::size_t>(column_index)];
    }
    return "";
}

std::string wallpaper_column_path(const Config &cfg, const std::string &monitor_name, int column_index, bool animated) {
    std::string override = wallpaper_column_override(cfg, monitor_name, column_index, animated);
    if (!override.empty()) {
        return override;
    }
    return cfg.default_wallpaper_enabled ? cfg.wallpaper_path : "";
}

std::string wallpaper_fill_mode(const Config &cfg, const std::string &monitor_name, int column_index, bool animated) {
    const std::vector<std::string> *modes = fill_modes_for(cfg, monitor_name, animated);
    if (modes != nullptr && column_index >= 0 && static_cast<std::size_t>(column_index) < modes->size() &&
        !(*modes)[static_cast<std::size_t>(column_index)].empty()) {
        return (*modes)[static_cast<std::size_t>(column_index)];
    }
    return "crop";
}

std::string wallpaper_image_for(const Config &cfg, const std::string &monitor_name) {
    return wallpaper_column_path(cfg, monitor_name, 0, false);
}

void wallpaper_set_column(Config &cfg, const std::string &monitor_name, int column_index, const std::string &path, bool animated) {
    std::vector<std::string> &columns = (animated ? cfg.wallpaper_animated_columns : cfg.wallpaper_columns)[monitor_name];
    if (static_cast<std::size_t>(column_index) >= columns.size()) {
        columns.resize(static_cast<std::size_t>(column_index) + 1);
    }
    columns[static_cast<std::size_t>(column_index)] = path;
}

void wallpaper_set_fill_mode(Config &cfg, const std::string &monitor_name, int column_index, const std::string &mode, bool animated) {
    std::vector<std::string> &modes = (animated ? cfg.wallpaper_animated_fill_modes : cfg.wallpaper_fill_modes)[monitor_name];
    if (static_cast<std::size_t>(column_index) >= modes.size()) {
        modes.resize(static_cast<std::size_t>(column_index) + 1);
    }
    modes[static_cast<std::size_t>(column_index)] = mode;
}

void wallpaper_set_column_count(Config &cfg, const std::string &monitor_name, int count, bool animated) {
    (animated ? cfg.wallpaper_animated_column_counts : cfg.wallpaper_column_counts)[monitor_name] = count;
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
