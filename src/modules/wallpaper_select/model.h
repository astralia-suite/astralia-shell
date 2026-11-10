#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/animation.h"
#include "core/input.h"

#include "service/settings_service.h"
#include "service/wallpaper_service.h"

#include "render/geometry.h"

namespace astralia::wallpaper_select {

struct Hooks {
    std::function<const Config &()> config;
    std::function<void(const std::function<void(Config &)> &)> update;
};

struct WallpaperCard {
    ui::Box box;
    int index = 0;
};

class Model {
  public:
    Model(Hooks hooks, bool animated_wallpaper);
    ~Model();
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    bool available() const;
    void open(const std::string &monitor, int column);
    void close();
    void tick(std::chrono::steady_clock::time_point now);
    void begin_frame() {
        cards_.clear();
        fading_ = false;
    }
    void add_card(const ui::Box &box, int index) { cards_.push_back({box, index}); }

    void click(double x, double y);
    bool key(const input::KeyEvent &event);
    void scroll(float notches);
    void step(int delta);
    void select(int index);
    void apply();
    float fade(const std::string &path, bool ready);
    bool clickable(double x, double y) const;

    bool is_open() const { return open_; }
    bool closing() const { return closing_; }
    bool animating() const { return animations_.has_active() || fading_; }
    bool scanning() const { return scanning_; }
    float appear() const { return appear_; }
    float position() const { return position_; }
    int selected() const { return selected_; }
    const std::string &current() const { return current_; }
    const std::string &directory() const { return directory_; }
    const std::string &monitor() const { return monitor_; }
    int column() const { return column_; }
    bool animated() const { return animated_; }
    int column_count() const {
        return wallpaper_column_count(hooks_.config(), monitor_, animated_);
    }
    const std::vector<std::string> &files() const { return files_; }

    std::function<void()> on_close_requested;
    std::function<void()> on_closed;
    std::function<void()> on_changed;

  private:
    void changed();
    void scan();
    void finish_close();
    void request_close();
    const WallpaperCard *card_at(double x, double y) const;

    Hooks hooks_;
    bool animated_wallpaper_;
    bool animated_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    AnimationManager animations_;
    std::vector<WallpaperCard> cards_;
    std::vector<std::string> files_;
    std::string monitor_;
    std::string current_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> seen_;
    std::string directory_;
    int column_ = 0;
    int selected_ = 0;
    float position_ = 0.0f;
    float appear_ = 0.0f;
    float wheel_ = 0.0f;
    uint64_t generation_ = 0;
    bool open_ = false;
    bool closing_ = false;
    bool scanning_ = false;
    bool fading_ = false;
};

} // namespace astralia::wallpaper_select
