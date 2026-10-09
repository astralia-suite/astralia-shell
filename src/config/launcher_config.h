#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace astralia {

namespace launcher_config {

// Launch commands
inline constexpr const char *browser = "astralia-open browser";
inline constexpr const char *editor = "astralia-open editor";
inline constexpr const char *file_manager = "astralia-open file-manager";
inline constexpr const char *terminal = "astralia-open terminal";
inline constexpr const char *open = "xdg-open";

// Web search
inline constexpr const char *google_url = "https://www.google.com/search?q=";
inline constexpr const char *duckduckgo_url = "https://duckduckgo.com/?q=";
inline constexpr const char *youtube_url = "https://www.youtube.com/results?search_query=";

// Search
inline constexpr int debounce_ms = 120;
inline constexpr int max_results = 20;
inline constexpr int listing_max_results = 50;

// Visit store
inline constexpr const char *visits_file = "astralia-shell/launcher_visits";

// Geometry
inline constexpr float width = 700.0f;
inline constexpr float pad = 10.0f;
inline constexpr float menu_pad = 14.0f;
inline constexpr float border_width = 2.0f;
inline constexpr float menu_border_width = 4.0f;
inline constexpr float highlight_border_width = 2.0f;
inline constexpr float bullet_size = 25.0f;
inline constexpr float bullet_gap = pad;
inline constexpr float search_height = 40.0f;
inline constexpr float row_height = 44.0f;
inline constexpr float row_spacing = 10.0f;
inline constexpr float row_pitch = row_height + row_spacing;
inline constexpr float list_top = 64.0f;
inline constexpr float list_gap = 10.0f;
inline constexpr float caret_width = 2.0f;
inline constexpr float two_line_gap = -3.0f;
inline constexpr float clip_inset = 10.0f;
inline constexpr int icon_size = 18;
inline constexpr int max_visible = 6;
inline constexpr std::size_t max_row_chars = 74;

// Typography
inline constexpr int label_px = 17;
inline constexpr int small_px = 12;

// Animation
inline constexpr float height_anim_ms = 200.0f;
inline constexpr float highlight_anim_ms = 140.0f;

// Bullets
inline constexpr const char *bullet_asset_prefix = "constellation/C";
inline constexpr const char *bullet_asset_suffix = ".png";

} // namespace launcher_config

struct DesktopEntry {
    std::string id;
    std::string name;
    std::string exec;
    std::string icon;
    bool terminal = false;
    bool no_display = false;
    bool hidden = false;
};

struct FileEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    float score = 0.0f;
};

struct ScoredApp {
    const DesktopEntry *entry;
    float score;
};

enum class LauncherMode { drun,
                          run,
                          google,
                          duckduckgo,
                          youtube,
                          url };

struct ModeQuery {
    LauncherMode mode;
    std::string query;
};

struct DrunResult {
    enum class Kind { app,
                      dir,
                      file } kind;
    const DesktopEntry *app = nullptr;
    FileEntry file;
};

struct VisitStore {
    std::unordered_map<std::string, int> counts;
    std::string path;
};

enum class SubmenuScreen { search,
                           browse,
                           dir_actions,
                           file_actions };

enum class SubmenuIcon { none,
                         arrow_right,
                         arrow_left,
                         code,
                         terminal,
                         folder_open };

struct SubmenuEntry {
    enum class Action {
        none,
        open_options,
        open_containing_dir,
        prev_dir,
        dir_open_file_manager,
        dir_open_editor,
        dir_open_terminal,
        file_open,
    };

    std::string name;
    std::string path;
    bool is_dir = false;
    SubmenuIcon icon = SubmenuIcon::none;
    Action action = Action::none;
};

struct SubmenuState {
    SubmenuScreen screen = SubmenuScreen::search;
    std::string current_path;
    bool came_from_browse = false;
    std::vector<SubmenuEntry> items;
};

using DirLister = std::function<std::vector<FileEntry>(const std::string &path, bool want_dirs)>;

} // namespace astralia
