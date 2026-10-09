#pragma once

#include <string>

#include "service/settings_service.h"

namespace astralia {

int wallpaper_column_count(const Config &cfg, const std::string &monitor_name, bool animated);
std::string wallpaper_column_override(const Config &cfg, const std::string &monitor_name, int column_index, bool animated);
std::string wallpaper_column_path(const Config &cfg, const std::string &monitor_name, int column_index, bool animated);
std::string wallpaper_fill_mode(const Config &cfg, const std::string &monitor_name, int column_index, bool animated);
void wallpaper_set_column(Config &cfg, const std::string &monitor_name, int column_index, const std::string &path, bool animated);
void wallpaper_set_fill_mode(Config &cfg, const std::string &monitor_name, int column_index, const std::string &mode, bool animated);
void wallpaper_set_column_count(Config &cfg, const std::string &monitor_name, int count, bool animated);
std::string wallpaper_image_for(const Config &cfg, const std::string &monitor_name);
void wallpaper_set_image(Config &cfg, const std::string &monitor_name, const std::string &path);
void wallpaper_clear_image(Config &cfg, const std::string &monitor_name);

} // namespace astralia
