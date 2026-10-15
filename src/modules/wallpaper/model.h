#pragma once

#include <cstdint>
#include <vector>

#include "render/geometry.h"

namespace astralia {

enum class DesktopMenuAction : uint8_t { settings,
                                         wallpaper };

struct DesktopMenuEntry {
    DesktopMenuAction action;
    const char *label;
    const char *icon;
};

class DesktopMenuModel {
  public:
    static const std::vector<DesktopMenuEntry> &entries();

    void open(float x, float y, float bounds_width, float bounds_height, int column);
    void close();
    bool toggle(float x, float y, float bounds_width, float bounds_height, int column);

    bool hover(float local_x, float local_y);
    void clear_hover();
    int entry_at(float local_x, float local_y) const;
    ui::Box row(int index) const;

    bool is_open() const { return open_; }
    const ui::Box &box() const { return box_; }
    int hovered() const { return hovered_; }
    int column() const { return column_; }

  private:
    ui::Box box_;
    int hovered_ = -1;
    int column_ = 0;
    bool open_ = false;
};

} // namespace astralia
