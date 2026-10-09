#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "config/icons.h"
#include "config/rain_config.h"
#include "config/settings_config.h"
#include "config/visualizer_config.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/path_home.h"

#include "modules/settings/model.h"

#include "service/wallpaper_service.h"

namespace astralia {

SettingsCaps settings_caps(const Capabilities &capabilities) {
    SettingsCaps caps;
    caps.animation = capabilities.animations;
    caps.idle = capabilities.idle;
    caps.rain = capabilities.rain;
    caps.visualizer = capabilities.visualizer;
    caps.animated_wallpaper = capabilities.animated_wallpaper;
    return caps;
}

namespace {

namespace cfg = settings_config;

constexpr uint64_t owner_tab_fade = 20;
constexpr uint64_t owner_field_anim = 10000;

std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim_float(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    std::string text(buffer);
    while (text.find('.') != std::string::npos && (text.back() == '0' || text.back() == '.')) {
        text.pop_back();
    }
    return text;
}

MonitorOverride &enable_defaults(MonitorOverride &override, const Config &config) {
    override.bar = config.default_bar_enabled;
    override.osd = config.default_osd_enabled;
    override.notifications = config.default_notifications_enabled;
    override.autohide = config.autohide;
    override.ambient_enabled = config.ambient_enabled;
    override.ambient_timeout_seconds = config.ambient_timeout_seconds;
    override.screensaver_enabled = config.screensaver_enabled;
    override.screensaver_timeout_seconds = config.screensaver_timeout_seconds;
    return override;
}

} // namespace

const std::vector<SettingsTabInfo> &settings_all_tabs() {
    static const std::vector<SettingsTabInfo> tabs{
        {SettingsTab::animation, "Animation", icon::adjustments},
        {SettingsTab::bar, "Bar", icon::layout_navbar},
        {SettingsTab::displays, "Displays", icon::device_desktop},
        {SettingsTab::idle, "Idle", icon::moon_stars},
        {SettingsTab::logout, "Logout", icon::power},
        {SettingsTab::rain, "Rain", icon::code},
        {SettingsTab::visualizer, "Visualizer", icon::wave_sine},
        {SettingsTab::wallpaper, "Wallpaper", icon::wallpaper},
    };
    return tabs;
}

std::string settings_field_text(const Config &config, SettingsField field, const std::string &monitor) {
    switch (field) {
    case SettingsField::wallpaper_dir:
        return path_collapse_home(config.wallpaper_dir);
    case SettingsField::wallpaper_animated_dir:
        return path_collapse_home(config.wallpaper_animated_dir);
    case SettingsField::ambient_timeout:
        return std::to_string(ambient_effective_timeout_seconds(config, monitor));
    case SettingsField::screensaver_timeout:
        return std::to_string(screensaver_effective_timeout_seconds(config, monitor));
    case SettingsField::visualizer_fps:
        return std::to_string(config.visualizer.fps);
    case SettingsField::visualizer_thin:
        return trim_float(config.visualizer.particle_thin);
    case SettingsField::visualizer_size:
        return std::to_string(config.visualizer.particle_size);
    case SettingsField::visualizer_complexity:
        return std::to_string(config.visualizer.fractal_complexity);
    case SettingsField::visualizer_glow_directions:
        return trim_float(config.visualizer.glow_directions);
    case SettingsField::visualizer_glow_quality:
        return trim_float(config.visualizer.glow_quality);
    case SettingsField::none:
        break;
    }
    return "";
}

void settings_apply_field(Config &config, SettingsField field, const std::string &text, const std::string &monitor) {
    try {
        switch (field) {
        case SettingsField::wallpaper_dir:
            config.wallpaper_dir = path_expand_home(text);
            break;
        case SettingsField::wallpaper_animated_dir:
            config.wallpaper_animated_dir = path_expand_home(text);
            break;
        case SettingsField::ambient_timeout: {
            auto value = static_cast<uint32_t>(std::clamp(std::stoi(text), cfg::idle_timeout_min, cfg::idle_timeout_max));
            (monitor.empty() ? config.ambient_timeout_seconds : config.monitor_overrides[monitor].ambient_timeout_seconds) = value;
            break;
        }
        case SettingsField::screensaver_timeout: {
            auto value = static_cast<uint32_t>(std::clamp(std::stoi(text), cfg::idle_timeout_min, cfg::idle_timeout_max));
            (monitor.empty() ? config.screensaver_timeout_seconds : config.monitor_overrides[monitor].screensaver_timeout_seconds) = value;
            break;
        }
        case SettingsField::visualizer_fps:
            config.visualizer.fps = std::clamp(std::stoi(text), kVisualizerFpsMin, kVisualizerFpsMax);
            break;
        case SettingsField::visualizer_thin:
            config.visualizer.particle_thin = std::clamp(std::stof(text), kVisualizerParticleThinMin, kVisualizerParticleThinMax);
            break;
        case SettingsField::visualizer_size:
            config.visualizer.particle_size = kVisualizerParticleSize;
            break;
        case SettingsField::visualizer_complexity:
            config.visualizer.fractal_complexity = std::clamp(std::stoi(text), kVisualizerComplexityMin, kVisualizerComplexityMax);
            break;
        case SettingsField::visualizer_glow_directions:
            config.visualizer.glow_directions = std::clamp(std::stof(text), kVisualizerGlowDirectionsMin, kVisualizerGlowDirectionsMax);
            break;
        case SettingsField::visualizer_glow_quality:
            config.visualizer.glow_quality = std::clamp(std::stof(text), kVisualizerGlowQualityMin, kVisualizerGlowQualityMax);
            break;
        case SettingsField::none:
            break;
        }
    } catch (const std::exception &) {
        log::error("settings: could not parse '{}' for field {}, keeping the previous value", text, static_cast<int>(field));
    }
}

bool settings_picker_less(const std::string &a, const std::string &b) {
    std::filesystem::path pa(a);
    std::filesystem::path pb(b);
    std::string ea = lowered(pa.extension().string());
    std::string eb = lowered(pb.extension().string());
    if (ea != eb) {
        return ea < eb;
    }
    return lowered(pa.filename().string()) < lowered(pb.filename().string());
}

bool settings_is_image(const std::string &path) {
    std::string extension = lowered(std::filesystem::path(path).extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".jfif" || extension == ".bmp" || extension == ".svg";
}

bool settings_is_video(const std::string &path) {
    std::string extension = lowered(std::filesystem::path(path).extension().string());
    return extension == ".mp4" || extension == ".webm" || extension == ".mkv";
}

SettingsModel::SettingsModel(SettingsCaps caps, SettingsHooks hooks)
    : caps_(std::move(caps)), hooks_(std::move(hooks)) {
    for (const SettingsTabInfo &info : settings_all_tabs()) {
        bool present = true;
        switch (info.tab) {
        case SettingsTab::animation:
            present = caps_.animation;
            break;
        case SettingsTab::idle:
            present = caps_.idle;
            break;
        case SettingsTab::logout:
            present = caps_.logout;
            break;
        case SettingsTab::rain:
            present = caps_.rain;
            break;
        case SettingsTab::visualizer:
            present = caps_.visualizer;
            break;
        default:
            break;
        }
        if (present) {
            tabs_.push_back(info);
        }
    }
    active_ = pending_ = caps_.remember_tab ? SettingsTab::displays : SettingsTab::wallpaper;
}

SettingsModel::~SettingsModel() {
    *alive_ = false;
}

void SettingsModel::changed() {
    if (on_changed) {
        on_changed();
    }
}

void SettingsModel::open() {
    open_ = true;
    if (!caps_.remember_tab) {
        active_ = pending_ = SettingsTab::wallpaper;
        animations_.cancelForOwner(owner_tab_fade);
        tab_alpha_ = 1.0f;
    }
    sync();
}

void SettingsModel::close() {
    commit_field();
    open_ = false;
    static_picker_.files.clear();
    static_picker_.dir.clear();
    animated_picker_.files.clear();
    animated_picker_.dir.clear();
    static_picker_.generation++;
    animated_picker_.generation++;
    static_picker_.scanning = animated_picker_.scanning = false;
}

BarStyle SettingsModel::effective_bar_style() const {
    BarStyle style = config().bar_style;
    if (std::ranges::find(caps_.bar_styles, style) != caps_.bar_styles.end()) {
        return style;
    }
    return std::ranges::find(caps_.bar_styles, BarStyle::continuous) != caps_.bar_styles.end() ? BarStyle::continuous : caps_.bar_styles.front();
}

void SettingsModel::sync() {
    monitors_.clear();
    if (hooks_.monitors) {
        for (std::string &name : hooks_.monitors()) {
            if (!name.empty()) {
                monitors_.push_back(std::move(name));
            }
        }
    }
    std::ranges::sort(monitors_);
    auto known = [this](const std::string &name) { return std::ranges::find(monitors_, name) != monitors_.end(); };
    if (!displays_monitor_.empty() && !known(displays_monitor_)) {
        displays_monitor_.clear();
    }
    if (!idle_monitor_.empty() && !known(idle_monitor_)) {
        idle_monitor_.clear();
    }
    std::string focused = hooks_.focused_monitor ? hooks_.focused_monitor() : std::string();
    const Config &current = config();
    for (bool animated : {false, true}) {
        WallpaperPicker &picker = animated ? animated_picker_ : static_picker_;
        if (monitors_.size() == 1) {
            picker.region = monitors_[0];
        } else if (picker.region.empty() || !known(picker.region)) {
            picker.region = known(focused) ? focused : (monitors_.empty() ? std::string() : monitors_[0]);
        }
        int count = picker.region.empty() ? 1 : wallpaper_column_count(current, picker.region, animated);
        picker.column = std::clamp(picker.column, 0, count - 1);
    }
    if (open_ && active_ == SettingsTab::wallpaper) {
        bool animated = caps_.animated_wallpaper && current.wallpaper_animated_enabled;
        const std::string &dir = animated ? current.wallpaper_animated_dir : current.wallpaper_dir;
        WallpaperPicker &picker = animated ? animated_picker_ : static_picker_;
        if (picker.dir != dir) {
            scan(picker, dir, animated);
        }
    }
}

void SettingsModel::rescan(bool animated) {
    const Config &current = config();
    scan(animated ? animated_picker_ : static_picker_, animated ? current.wallpaper_animated_dir : current.wallpaper_dir, animated);
}

void SettingsModel::scan(WallpaperPicker &picker, const std::string &dir, bool animated) {
    picker.dir = dir;
    picker.scanning = true;
    picker.scroll = 0.0f;
    uint64_t generation = ++picker.generation;
    std::weak_ptr<bool> alive = alive_;
    std::string expanded = path_expand_home(dir);
    std::thread([this, alive, &picker, expanded, generation, animated] {
        std::vector<std::string> found;
        std::error_code error;
        std::filesystem::recursive_directory_iterator it(expanded, std::filesystem::directory_options::skip_permission_denied, error);
        for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (it.depth() >= 1) {
                it.disable_recursion_pending();
            }
            const std::string path = it->path().string();
            if (it->is_regular_file(error) && (animated ? settings_is_video(path) : settings_is_image(path))) {
                found.push_back(path);
            }
        }
        std::ranges::sort(found, settings_picker_less);
        if (found.size() > cfg::max_images) {
            found.resize(cfg::max_images);
        }
        DeferredCall::call_later([this, alive, &picker, found = std::move(found), generation]() mutable {
            if (!alive.lock() || generation != picker.generation) {
                return;
            }
            picker.files = std::move(found);
            picker.scanning = false;
            changed();
        });
    }).detach();
}

void SettingsModel::select_tab(SettingsTab tab) {
    if (tab == active_) {
        return;
    }
    pending_ = tab;
    animations_.animate(tab_alpha_, 0.0f, cfg::tab_fade_ms, astralia::Easing::EaseOutCubic, [this](float v) { tab_alpha_ = v; }, [this] {
        active_ = pending_;
        animations_.animate(0.0f, 1.0f, cfg::tab_fade_ms, astralia::Easing::EaseOutCubic, [this](float v) { tab_alpha_ = v; }, {}, owner_tab_fade); }, owner_tab_fade);
    sync();
}

void SettingsModel::focus_field(SettingsField field) {
    if (field == focused_) {
        return;
    }
    commit_field();
    focused_ = field;
    field_.text = settings_field_text(config(), field, idle_monitor_);
    field_.preedit.clear();
    field_.cursor_idle_visible = true;
    text_field_type_anim_settle(field_anim_, animations_, owner_field_anim, field_.text);
    if (on_text_focus) {
        on_text_focus(true);
    }
}

void SettingsModel::commit_field() {
    if (focused_ == SettingsField::none) {
        return;
    }
    SettingsField field = focused_;
    std::string text = field_.text;
    std::string monitor = idle_monitor_;
    focused_ = SettingsField::none;
    field_.preedit.clear();
    text_field_type_anim_clear(field_anim_, animations_, owner_field_anim);
    if (on_text_focus) {
        on_text_focus(false);
    }
    edit([&](Config &config) { settings_apply_field(config, field, text, monitor); });
}

void SettingsModel::edit(const std::function<void(Config &)> &apply) {
    hooks_.update(apply);
    sync();
    changed();
}

bool SettingsModel::key(const input::KeyEvent &event) {
    if (focused_ == SettingsField::none) {
        if (event.kind == input::KeyKind::Escape) {
            if (on_close_requested) {
                on_close_requested();
            }
            return true;
        }
        if (event.kind == input::KeyKind::Up || event.kind == input::KeyKind::Down) {
            auto it = std::ranges::find(tabs_, pending_, &SettingsTabInfo::tab);
            if (it == tabs_.end()) {
                return false;
            }
            auto index = static_cast<std::ptrdiff_t>(it - tabs_.begin());
            index = std::clamp<std::ptrdiff_t>(index + (event.kind == input::KeyKind::Down ? 1 : -1), 0, static_cast<std::ptrdiff_t>(tabs_.size()) - 1);
            select_tab(tabs_[static_cast<std::size_t>(index)].tab);
            changed();
            return true;
        }
        return false;
    }
    switch (text_field_handle_key(field_, event)) {
    case TextFieldResult::Changed:
        text_field_type_anim_sync(field_anim_, animations_, owner_field_anim, field_.text);
        changed();
        return true;
    case TextFieldResult::Committed:
        commit_field();
        return true;
    case TextFieldResult::Cancelled:
        focused_ = SettingsField::none;
        field_.preedit.clear();
        text_field_type_anim_clear(field_anim_, animations_, owner_field_anim);
        if (on_text_focus) {
            on_text_focus(false);
        }
        changed();
        return true;
    case TextFieldResult::None:
        break;
    }
    return false;
}

void SettingsModel::commit_text(const std::string &text) {
    field_.text += text;
    field_.preedit.clear();
    text_field_type_anim_sync(field_anim_, animations_, owner_field_anim, field_.text);
    changed();
}

void SettingsModel::delete_before(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        text_field_backspace(field_.text);
    }
    text_field_type_anim_sync(field_anim_, animations_, owner_field_anim, field_.text);
    changed();
}

void SettingsModel::set_preedit(std::string text) {
    field_.preedit = std::move(text);
    field_.cursor_idle_visible = true;
    changed();
}

void SettingsModel::toggle_caret() {
    if (focused_ != SettingsField::none) {
        text_field_idle_toggle(field_);
        changed();
    }
}

bool SettingsModel::clickable(double x, double y) const {
    return std::ranges::any_of(regions_, [&](const SettingsRegion &r) { return r.box.w > 0 && x >= r.box.x && x < r.box.x + r.box.w && y >= r.box.y && y < r.box.y + r.box.h; });
}

void SettingsModel::click(double x, double y) {
    for (const SettingsRegion &region : regions_) {
        const ui::Box &b = region.box;
        if (b.w > 0 && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
            dispatch(region);
            return;
        }
    }
    bool inside = x >= panel_.x && x < panel_.x + panel_.w && y >= panel_.y && y < panel_.y + panel_.h;
    if (!inside && on_close_requested) {
        on_close_requested();
    }
}

void SettingsModel::scroll(float pixels) {
    if (active_ != SettingsTab::wallpaper) {
        return;
    }
    bool animated = caps_.animated_wallpaper && config().wallpaper_animated_enabled;
    WallpaperPicker &picker = this->picker(animated);
    float inset_w = picker.grid_width - cfg::grid_inset * 2.0f;
    float inset_h = picker.grid_height - cfg::grid_inset * 2.0f;
    int columns = cfg::thumb_columns;
    (void)inset_w;
    std::size_t rows = (picker.files.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns);
    float content = rows == 0 ? 0.0f : static_cast<float>(rows) * (cfg::thumb_size + cfg::thumb_gap) - cfg::thumb_gap;
    picker.scroll = std::clamp(picker.scroll + pixels, 0.0f, std::max(0.0f, content - inset_h));
    changed();
}

void SettingsModel::dispatch(const SettingsRegion &region) {
    const Config &current = config();
    auto monitor = [&](int index) { return index < 0 || static_cast<std::size_t>(index) >= monitors_.size() ? std::string() : monitors_[static_cast<std::size_t>(index)]; };
    switch (region.act) {
    case SettingsAct::close:
        if (on_close_requested) {
            on_close_requested();
        }
        return;
    case SettingsAct::tab:
        commit_field();
        select_tab(static_cast<SettingsTab>(region.a));
        changed();
        return;
    case SettingsAct::field_focus:
        focus_field(static_cast<SettingsField>(region.a));
        changed();
        return;
    case SettingsAct::displays_monitor:
        displays_monitor_ = monitor(region.a);
        changed();
        return;
    case SettingsAct::idle_monitor:
        commit_field();
        idle_monitor_ = monitor(region.a);
        changed();
        return;
    default:
        break;
    }
    commit_field();
    const std::string &target = region.b == 1 ? idle_monitor_ : displays_monitor_;
    switch (region.act) {
    case SettingsAct::toggle_override:
        edit([&](Config &config) {
            MonitorOverride &override = config.monitor_overrides[target];
            if (!override.enabled) {
                enable_defaults(override, current);
            }
            override.enabled = !override.enabled;
        });
        return;
    case SettingsAct::toggle_bar:
    case SettingsAct::toggle_osd:
    case SettingsAct::toggle_notifications:
    case SettingsAct::toggle_autohide:
    case SettingsAct::idle_ambient:
    case SettingsAct::idle_screensaver: {
        SettingsAct act = region.act;
        edit([&](Config &config) {
            MonitorOverride *override = target.empty() ? nullptr : &config.monitor_overrides[target];
            auto flip = [&](bool &defaults, bool MonitorOverride::*field) {
                bool &value = override != nullptr ? override->*field : defaults;
                value = !value;
            };
            switch (act) {
            case SettingsAct::toggle_bar:
                flip(config.default_bar_enabled, &MonitorOverride::bar);
                break;
            case SettingsAct::toggle_osd:
                flip(config.default_osd_enabled, &MonitorOverride::osd);
                break;
            case SettingsAct::toggle_notifications:
                flip(config.default_notifications_enabled, &MonitorOverride::notifications);
                break;
            case SettingsAct::toggle_autohide:
                flip(config.autohide, &MonitorOverride::autohide);
                break;
            case SettingsAct::idle_ambient:
                flip(config.ambient_enabled, &MonitorOverride::ambient_enabled);
                break;
            default:
                flip(config.screensaver_enabled, &MonitorOverride::screensaver_enabled);
                break;
            }
        });
        return;
    }
    case SettingsAct::idle_enable:
        edit([](Config &config) { config.idle_management_enabled = !config.idle_management_enabled; });
        return;
    case SettingsAct::idle_ambient_reset:
        edit([&](Config &config) { config.monitor_overrides[idle_monitor_].ambient_timeout_seconds = config.ambient_timeout_seconds; });
        return;
    case SettingsAct::idle_screensaver_reset:
        edit([&](Config &config) { config.monitor_overrides[idle_monitor_].screensaver_timeout_seconds = config.screensaver_timeout_seconds; });
        return;
    case SettingsAct::bar_style:
        if (region.a >= 0 && static_cast<std::size_t>(region.a) < caps_.bar_styles.size()) {
            BarStyle style = caps_.bar_styles[static_cast<std::size_t>(region.a)];
            edit([style](Config &config) { config.bar_style = style; });
        }
        return;
    case SettingsAct::rain_mode:
        edit([&](Config &config) { config.rain.mode = region.a == 1 ? RainMode::Stiletto : RainMode::Matrix; });
        return;
    case SettingsAct::rain_async:
        edit([](Config &config) { config.rain.async_speed = !config.rain.async_speed; });
        return;
    case SettingsAct::visualizer_shape:
        edit([&](Config &config) { config.visualizer.visualizer_shape = region.a == 1 ? VisualizerShape::Sphere : VisualizerShape::Bar; });
        return;
    case SettingsAct::visualizer_reset:
        edit([&](Config &config) {
            VisualizerParams defaults;
            switch (static_cast<SettingsField>(region.a)) {
            case SettingsField::visualizer_fps:
                config.visualizer.fps = defaults.fps;
                break;
            case SettingsField::visualizer_thin:
                config.visualizer.particle_thin = defaults.particle_thin;
                break;
            case SettingsField::visualizer_size:
                config.visualizer.particle_size = defaults.particle_size;
                break;
            case SettingsField::visualizer_complexity:
                config.visualizer.fractal_complexity = defaults.fractal_complexity;
                break;
            case SettingsField::visualizer_glow_directions:
                config.visualizer.glow_directions = defaults.glow_directions;
                break;
            case SettingsField::visualizer_glow_quality:
                config.visualizer.glow_quality = defaults.glow_quality;
                break;
            default:
                break;
            }
        });
        return;
    case SettingsAct::animation_disable:
        edit([](Config &config) { config.animations_disabled = !config.animations_disabled; });
        return;
    case SettingsAct::logout_logo:
        edit([](Config &config) { config.logout_animated_logo = !config.logout_animated_logo; });
        return;
    default:
        dispatch_wallpaper(region);
        return;
    }
}

void SettingsModel::dispatch_wallpaper(const SettingsRegion &region) {
    bool animated = region.b == 1;
    WallpaperPicker &picker = this->picker(animated);
    switch (region.act) {
    case SettingsAct::wallpaper_default:
        edit([](Config &config) { config.default_wallpaper_enabled = !config.default_wallpaper_enabled; });
        return;
    case SettingsAct::wallpaper_animated:
        edit([](Config &config) { config.wallpaper_animated_enabled = !config.wallpaper_animated_enabled; });
        return;
    case SettingsAct::wallpaper_region: {
        int monitor = region.a;
        animated = (region.b & 0x100) != 0;
        WallpaperPicker &target = this->picker(animated);
        if (monitor >= 0 && static_cast<std::size_t>(monitor) < monitors_.size()) {
            target.region = monitors_[static_cast<std::size_t>(monitor)];
            target.column = region.b & 0xff;
        }
        changed();
        return;
    }
    case SettingsAct::wallpaper_pick:
        if (region.a >= 0 && static_cast<std::size_t>(region.a) < picker.files.size()) {
            std::string path = picker.files[static_cast<std::size_t>(region.a)];
            edit([&](Config &config) { wallpaper_set_column(config, picker.region, picker.column, path, animated); });
        }
        return;
    case SettingsAct::wallpaper_remove:
        edit([&](Config &config) {
            if (caps_.wallpaper_columns) {
                wallpaper_set_column(config, picker.region, picker.column, "", animated);
            } else {
                wallpaper_clear_image(config, picker.region);
            }
        });
        return;
    case SettingsAct::wallpaper_fill:
        edit([&](Config &config) { wallpaper_set_fill_mode(config, picker.region, picker.column, region.a == 1 ? "fit" : "crop", animated); });
        return;
    case SettingsAct::wallpaper_count:
        edit([&](Config &config) {
            int count = std::clamp(wallpaper_column_count(config, picker.region, animated) + region.a, 1, cfg::max_columns);
            wallpaper_set_column_count(config, picker.region, count, animated);
        });
        return;
    case SettingsAct::wallpaper_rescan:
        rescan(animated);
        changed();
        return;
    default:
        return;
    }
}

} // namespace astralia
