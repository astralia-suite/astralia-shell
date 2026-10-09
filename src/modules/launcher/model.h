#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/launcher_config.h"

#include "core/animation.h"
#include "core/async_process.h"
#include "core/input.h"
#include "core/reactor.h"

#include "render/text_field.h"

namespace astralia {

struct LauncherRow {
    const char *glyph = nullptr;
    std::string label;
    std::string subtitle;
    std::string icon_path;
};

class LauncherModel {
  public:
    using Clock = std::chrono::steady_clock;

    explicit LauncherModel(Reactor &reactor);
    ~LauncherModel();
    LauncherModel(const LauncherModel &) = delete;
    LauncherModel &operator=(const LauncherModel &) = delete;

    void open(bool global);
    void close();
    void set_apps(std::vector<DesktopEntry> apps) { apps_ = std::move(apps); }
    void set_search_root(std::string root) { search_root_ = std::move(root); }

    void key(const input::KeyEvent &event);
    void commit_text(const std::string &text);
    void delete_before(uint32_t count);
    void set_preedit(std::string text);
    void click_row(int index);
    bool hover(int row);
    void toggle_caret();
    void tick(Clock::time_point now) { animations_.tick(now); }
    void sync_layout();

    bool is_open() const { return open_; }
    bool searching() const { return searching_; }
    bool animating() const { return animations_.hasActive(); }
    LauncherMode mode() const { return mode_; }
    const TextFieldState &field() const { return field_; }
    TextFieldState &field() { return field_; }
    const TextFieldTypeAnim &query_anim() const { return query_anim_; }
    const std::vector<DrunResult> &results() const { return results_; }
    const SubmenuState &submenu() const { return submenu_; }
    int selected() const { return selected_; }
    int hovered() const { return hovered_; }
    int first_visible() const;
    int item_count() const;
    float box_height() const { return height_; }
    float highlight_offset() const { return highlight_; }
    float scroll_offset() const { return scroll_; }
    std::vector<LauncherRow> rows();

    std::function<void()> on_changed;
    std::function<void()> on_close_requested;

  private:
    void changed();
    void query_changed();
    void arm_debounce();
    std::chrono::milliseconds until_debounce() const;
    void debounce_fired();
    void start_search();
    void stop_search();
    void finish_search();
    void launch_selected();
    void reset_selection();
    const std::string &icon_path_for(const DesktopEntry &entry);

    Reactor &reactor_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    int timer_ = -1;
    AnimationManager animations_;

    bool open_ = false;
    std::string search_root_;
    std::vector<DesktopEntry> apps_;
    TextFieldState field_;
    TextFieldTypeAnim query_anim_;
    LauncherMode mode_ = LauncherMode::drun;
    std::string effective_query_;
    std::string search_query_;
    std::vector<DrunResult> results_;
    SubmenuState submenu_;
    VisitStore visits_;
    int selected_ = -1;
    int hovered_ = -1;
    std::unordered_map<std::string, std::string> icon_paths_;

    bool debounce_pending_ = false;
    Clock::time_point debounce_due_{};
    AsyncProcess dirs_proc_;
    AsyncProcess files_proc_;
    std::string dirs_output_;
    std::string files_output_;
    int pending_searches_ = 0;
    bool searching_ = false;

    float height_ = 0.0f;
    float height_target_ = -1.0f;
    float highlight_ = 0.0f;
    float highlight_target_ = -1.0f;
    float scroll_ = 0.0f;
    float scroll_target_ = -1.0f;
};

float launcher_content_height(int visible_rows);

} // namespace astralia
