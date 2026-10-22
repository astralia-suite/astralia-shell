#include <algorithm>
#include <cstdlib>

#include "core/log.h"
#include "core/path_home.h"

#include "modules/launcher/apps.h"
#include "modules/launcher/desktop_entry.h"
#include "modules/launcher/files.h"
#include "modules/launcher/launch.h"
#include "modules/launcher/model.h"
#include "modules/launcher/search.h"
#include "modules/launcher/submenu.h"
#include "modules/launcher/visits.h"

#include "service/icon_service.h"

#include "render/icons.h"

namespace astralia::launcher {

namespace {

namespace cfg = launcher_config;

constexpr uint64_t owner_height = 100;
constexpr uint64_t owner_highlight = 101;
constexpr uint64_t owner_scroll = 102;
constexpr uint64_t owner_query = 1000;

std::vector<FileEntry> list_dir(const std::string &path, bool want_dirs) {
    return list_directory(path, want_dirs);
}

const char *submenu_glyph(const SubmenuEntry &entry) {
    switch (entry.icon) {
    case SubmenuIcon::arrow_right:
        return icon::arrow_right;
    case SubmenuIcon::arrow_left:
        return icon::arrow_left;
    case SubmenuIcon::code:
        return icon::code;
    case SubmenuIcon::terminal:
        return icon::terminal;
    case SubmenuIcon::folder_open:
        return icon::folder_open;
    case SubmenuIcon::none:
        break;
    }
    return entry.is_dir ? icon::folder : icon::edit;
}

} // namespace

float content_height(int visible_rows) {
    float height = cfg::menu_pad * 2.0f + cfg::search_height;
    if (visible_rows > 0) {
        height += cfg::list_gap + static_cast<float>(visible_rows) * cfg::row_height +
                  static_cast<float>(visible_rows - 1) * cfg::row_spacing;
    }
    return height;
}

Model::Model(Reactor &reactor) : reactor_(reactor), dirs_proc_(reactor), files_proc_(reactor) {
    std::weak_ptr<bool> alive = alive_;
    timer_ = reactor_.add_timer(
        [this, alive] {
            auto live = alive.lock();
            return live ? until_debounce() : std::chrono::hours(1);
        },
        [this, alive] {
            if (alive.lock()) {
                debounce_fired();
            }
        });
}

Model::~Model() {
    *alive_ = false;
    stop_search();
}

void Model::changed() {
    if (on_changed) {
        on_changed();
    }
}

void Model::open(bool global) {
    const char *home = std::getenv("HOME");
    search_root_ = global || home == nullptr ? "/" : home;
    apps_ = scan_desktop_entries();
    visits_ = visit_store_load(visit_store_path(std::getenv("XDG_STATE_HOME"), home));
    open_ = true;
    field_.cursor_idle_visible = true;
    log::info("launcher: open, searching from {}", search_root_);
}

void Model::close() {
    stop_search();
    debounce_pending_ = false;
    open_ = false;
    field_.text.clear();
    field_.preedit.clear();
    cursor_ = {};
    field_first_ = 0;
    dragging_ = false;
    have_click_ = false;
    text_field_type_anim_clear(query_anim_, animations_, owner_query);
    mode_ = LauncherMode::drun;
    effective_query_.clear();
    search_query_.clear();
    results_.clear();
    submenu_close(submenu_);
    selected_ = -1;
    hovered_ = -1;
    height_target_ = highlight_target_ = scroll_target_ = -1.0f;
    animations_.cancel_owner(owner_height);
    animations_.cancel_owner(owner_highlight);
    animations_.cancel_owner(owner_scroll);
}

int Model::item_count() const {
    return static_cast<int>(submenu_.screen == SubmenuScreen::search ? results_.size()
                                                                     : submenu_.items.size());
}

int Model::first_visible() const {
    return selected_ >= cfg::max_visible ? selected_ - cfg::max_visible + 1 : 0;
}

void Model::reset_selection() {
    selected_ = item_count() == 0 ? -1 : 0;
    hovered_ = -1;
}

void Model::query_changed() {
    ModeQuery mq = detect_mode_and_query(field_.text);
    mode_ = mq.mode;
    effective_query_ = mq.query;
    field_.cursor_idle_visible = true;
    text_cursor_clamp(field_.text, cursor_);
    if (cursor_.pos == field_.text.size()) {
        text_field_type_anim_sync(query_anim_, animations_, owner_query, field_.text);
    } else {
        text_field_type_anim_settle(query_anim_, animations_, owner_query, field_.text);
    }
    arm_debounce();
}

void Model::text_edited() {
    if (submenu_.screen != SubmenuScreen::search) {
        submenu_close(submenu_);
    }
    field_.preedit.clear();
    query_changed();
}

void Model::paste(const std::string &text) {
    std::string line;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
            line += c;
        }
    }
    if (line.empty()) {
        return;
    }
    hovered_ = -1;
    text_cursor_insert(field_.text, cursor_, line);
    text_edited();
    changed();
}

void Model::key_text(const input::KeyEvent &event) {
    if (event.alt) {
        return;
    }
    if (!event.ctrl) {
        if (event.text.empty()) {
            return;
        }
        text_cursor_insert(field_.text, cursor_, event.text);
        text_edited();
        return;
    }
    constexpr uint32_t key_a = 0x61, key_c = 0x63, key_v = 0x76, key_x = 0x78;
    uint32_t sym = event.base_sym >= 'A' && event.base_sym <= 'Z' ? event.base_sym + ('a' - 'A')
                                                                  : event.base_sym;
    switch (sym) {
    case key_a:
        text_cursor_select_all(field_.text, cursor_);
        field_.cursor_idle_visible = true;
        break;
    case key_c:
    case key_x:
        if (cursor_.has_selection() && clipboard_ != nullptr) {
            clipboard_->set(text_cursor_selected_text(field_.text, cursor_));
        }
        if (sym == key_x && text_cursor_delete_selection(field_.text, cursor_)) {
            text_edited();
        }
        break;
    case key_v:
        if (clipboard_ != nullptr) {
            std::weak_ptr<bool> alive = alive_;
            clipboard_->request([this, alive](std::string text) {
                if (alive.lock() && open_) {
                    paste(text);
                }
            });
        }
        break;
    default:
        break;
    }
}

size_t Model::byte_at_column(float column) const {
    return text_field_byte_offset(field_.text, static_cast<size_t>(std::max(column, 0.0f) + 0.5f));
}

size_t Model::field_window(size_t width) {
    size_t total = text_field_utf8_len(field_.text);
    if (total <= width) {
        return field_first_ = 0;
    }
    size_t column = text_field_char_index(field_.text, cursor_.pos);
    if (column < field_first_) {
        field_first_ = column;
    } else if (column > field_first_ + width) {
        field_first_ = column - width;
    }
    return field_first_ = std::min(field_first_, total - width);
}

void Model::pointer_down(float column, Clock::time_point now) {
    if (!open_) {
        return;
    }
    size_t byte = byte_at_column(column);
    bool double_click = have_click_ && byte == last_click_byte_ &&
                        now - last_click_ <= std::chrono::milliseconds(cfg::double_click_ms);
    field_.cursor_idle_visible = true;
    hovered_ = -1;
    if (double_click) {
        text_cursor_select_word(
            field_.text, cursor_,
            text_field_byte_offset(field_.text, static_cast<size_t>(std::max(column, 0.0f))));
        have_click_ = false;
        dragging_ = false;
    } else {
        text_cursor_set(field_.text, cursor_, byte, false);
        have_click_ = true;
        last_click_ = now;
        last_click_byte_ = byte;
        dragging_ = true;
    }
    changed();
}

void Model::pointer_drag(float column) {
    if (!open_ || !dragging_) {
        return;
    }
    text_cursor_set(field_.text, cursor_, byte_at_column(column), true);
    field_.cursor_idle_visible = true;
    changed();
}

void Model::pointer_up() { dragging_ = false; }

void Model::arm_debounce() {
    debounce_pending_ = true;
    debounce_due_ = Clock::now() + std::chrono::milliseconds(cfg::debounce_ms);
    reactor_.reschedule(timer_);
}

std::chrono::milliseconds Model::until_debounce() const {
    if (!debounce_pending_) {
        return std::chrono::hours(1);
    }
    return std::max(std::chrono::ceil<std::chrono::milliseconds>(debounce_due_ - Clock::now()),
                    std::chrono::milliseconds(0));
}

void Model::debounce_fired() {
    if (!debounce_pending_) {
        return;
    }
    debounce_pending_ = false;
    if (!open_) {
        return;
    }
    if (mode_ == LauncherMode::drun && !effective_query_.empty()) {
        start_search();
        return;
    }
    stop_search();
    results_.clear();
    if (submenu_.screen == SubmenuScreen::search) {
        selected_ = -1;
    }
    changed();
}

void Model::start_search() {
    stop_search();
    search_query_ = effective_query_;
    std::string pattern = to_glob_pattern(search_query_);
    searching_ = true;
    pending_searches_ = 2;
    for (bool dirs : {true, false}) {
        AsyncProcess &process = dirs ? dirs_proc_ : files_proc_;
        bool started = process.start(fd_search_argv(pattern, search_root_, dirs, cfg::max_results),
                                     [this, dirs](std::string output) {
                                         (dirs ? dirs_output_ : files_output_) = std::move(output);
                                         if (--pending_searches_ == 0 && searching_) {
                                             finish_search();
                                         }
                                     });
        if (!started) {
            --pending_searches_;
        }
    }
    if (pending_searches_ == 0) {
        finish_search();
    }
}

void Model::stop_search() {
    dirs_proc_.cancel();
    files_proc_.cancel();
    dirs_output_.clear();
    files_output_.clear();
    pending_searches_ = 0;
    searching_ = false;
}

void Model::finish_search() {
    searching_ = false;
    std::vector<ScoredApp> apps = search_apps(apps_, search_query_);
    std::vector<FileEntry> files;
    for (bool dirs : {true, false}) {
        for (FileEntry &file : fd_search_parse_output(dirs ? dirs_output_ : files_output_, dirs)) {
            file.score = score_path(file.name, search_query_);
            if (file.score >= 0.0f) {
                files.push_back(std::move(file));
            }
        }
    }
    results_ = combined_drun_results(apps, files, visits_, cfg::max_results);
    if (submenu_.screen == SubmenuScreen::search) {
        selected_ = results_.empty() ? -1 : 0;
    }
    changed();
}

void Model::launch_selected() {
    if (submenu_.screen != SubmenuScreen::search) {
        if (selected_ < 0 || selected_ >= static_cast<int>(submenu_.items.size())) {
            return;
        }
        SubmenuEntry entry = submenu_.items[static_cast<size_t>(selected_)];
        if (submenu_handle_entry(submenu_, entry, list_dir)) {
            reset_selection();
            return;
        }
        launch_submenu_action(entry, visits_);
        if (on_close_requested) {
            on_close_requested();
        }
        return;
    }
    if (mode_ != LauncherMode::drun) {
        launch_non_drun(mode_, effective_query_);
        if (on_close_requested) {
            on_close_requested();
        }
        return;
    }
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        return;
    }
    const DrunResult &result = results_[static_cast<size_t>(selected_)];
    switch (result.kind) {
    case DrunResult::Kind::app:
        launch_app(*result.app, visits_);
        if (on_close_requested) {
            on_close_requested();
        }
        break;
    case DrunResult::Kind::dir:
        submenu_open_directory(submenu_, result.file.path, list_dir);
        reset_selection();
        break;
    case DrunResult::Kind::file:
        submenu_open_file_actions(submenu_, result.file.path);
        reset_selection();
        break;
    }
}

void Model::key(const input::KeyEvent &event) {
    hovered_ = -1;
    switch (event.kind) {
    case input::KeyKind::Text:
        key_text(event);
        break;
    case input::KeyKind::Preedit:
        field_.preedit = event.text;
        field_.cursor_idle_visible = true;
        break;
    case input::KeyKind::Backspace:
        text_cursor_backspace(field_.text, cursor_);
        text_edited();
        break;
    case input::KeyKind::Delete:
        if (text_cursor_delete(field_.text, cursor_)) {
            text_edited();
        }
        break;
    case input::KeyKind::Left:
    case input::KeyKind::Right: {
        CursorDir dir = event.kind == input::KeyKind::Left ? CursorDir::Left : CursorDir::Right;
        if (event.ctrl) {
            text_cursor_move_word(field_.text, cursor_, dir, event.shift);
        } else {
            text_cursor_move(field_.text, cursor_, dir, event.shift);
        }
        field_.cursor_idle_visible = true;
        break;
    }
    case input::KeyKind::Home:
        text_cursor_home(cursor_, event.shift);
        field_.cursor_idle_visible = true;
        break;
    case input::KeyKind::End:
        text_cursor_end(field_.text, cursor_, event.shift);
        field_.cursor_idle_visible = true;
        break;
    case input::KeyKind::Up:
    case input::KeyKind::Down: {
        int count = item_count();
        selected_ = count == 0
                        ? -1
                        : std::clamp(selected_ + (event.kind == input::KeyKind::Down ? 1 : -1), 0,
                                     count - 1);
        break;
    }
    case input::KeyKind::Escape:
        if (submenu_.screen == SubmenuScreen::search) {
            if (on_close_requested) {
                on_close_requested();
            }
            return;
        }
        submenu_go_back(submenu_, list_dir);
        reset_selection();
        break;
    case input::KeyKind::Enter:
        launch_selected();
        break;
    case input::KeyKind::Tab:
        break;
    }
    changed();
}

void Model::commit_text(const std::string &text) {
    text_cursor_insert(field_.text, cursor_, text);
    field_.preedit.clear();
    query_changed();
    changed();
}

void Model::delete_before(uint32_t count) {
    text_cursor_clamp(field_.text, cursor_);
    cursor_.anchor = cursor_.pos;
    for (uint32_t i = 0; i < count; ++i) {
        text_cursor_backspace(field_.text, cursor_);
    }
    query_changed();
    changed();
}

void Model::set_preedit(std::string text) {
    field_.preedit = std::move(text);
    field_.cursor_idle_visible = true;
    changed();
}

void Model::click_row(int index) {
    selected_ = index;
    hovered_ = -1;
    launch_selected();
    changed();
}

bool Model::hover(int row) {
    int next = open_ ? row : -1;
    if (next == hovered_) {
        return false;
    }
    hovered_ = next;
    return true;
}

void Model::toggle_caret() { text_field_idle_toggle(field_); }

const std::string &Model::icon_path_for(const DesktopEntry &entry) {
    auto it = icon_paths_.find(entry.id);
    if (it == icon_paths_.end()) {
        it = icon_paths_.emplace(entry.id, app_icon_path(entry.icon)).first;
    }
    return it->second;
}

std::vector<Row> Model::rows() {
    std::vector<Row> out;
    if (submenu_.screen == SubmenuScreen::search) {
        for (const DrunResult &result : results_) {
            switch (result.kind) {
            case DrunResult::Kind::app:
                out.push_back({icon::apps, result.app->name, "", icon_path_for(*result.app)});
                break;
            case DrunResult::Kind::dir:
                out.push_back(
                    {icon::folder, result.file.name, path_collapse_home(result.file.path), ""});
                break;
            case DrunResult::Kind::file:
                out.push_back(
                    {icon::edit, result.file.name, path_collapse_home(result.file.path), ""});
                break;
            }
        }
        return out;
    }
    for (const SubmenuEntry &entry : submenu_.items) {
        out.push_back({submenu_glyph(entry), entry.name,
                       entry.path.empty() ? "" : path_collapse_home(entry.path), ""});
    }
    return out;
}

void Model::sync_layout() {
    int visible = std::min(item_count(), cfg::max_visible);
    float content = content_height(visible);
    if (height_target_ < 0.0f) {
        height_ = height_target_ = content;
    } else if (content != height_target_) {
        height_target_ = content;
        animations_.animate(
            height_, height_target_, cfg::height_anim_ms, astralia::Easing::EaseInOutCubic,
            [this](float v) { height_ = v; }, {}, owner_height);
    }
    if (selected_ < 0) {
        highlight_target_ = scroll_target_ = -1.0f;
        return;
    }
    float highlight = static_cast<float>(selected_) * cfg::row_pitch;
    if (highlight_target_ < 0.0f) {
        highlight_ = highlight_target_ = highlight;
    } else if (highlight != highlight_target_) {
        highlight_target_ = highlight;
        animations_.animate(
            highlight_, highlight_target_, cfg::highlight_anim_ms, astralia::Easing::EaseOutCubic,
            [this](float v) { highlight_ = v; }, {}, owner_highlight);
    }
    float scroll = static_cast<float>(first_visible()) * cfg::row_pitch;
    if (scroll_target_ < 0.0f) {
        scroll_ = scroll_target_ = scroll;
    } else if (scroll != scroll_target_) {
        scroll_target_ = scroll;
        animations_.animate(
            scroll_, scroll_target_, cfg::highlight_anim_ms, astralia::Easing::EaseOutCubic,
            [this](float v) { scroll_ = v; }, {}, owner_scroll);
    }
}

} // namespace astralia::launcher
