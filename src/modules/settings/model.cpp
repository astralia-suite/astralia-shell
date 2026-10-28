#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <thread>

#include "config/rain_config.h"
#include "config/settings_config.h"
#include "config/visualizer_config.h"

#include "render/icons.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/path_home.h"

#include "modules/settings/model.h"

#include "service/wallpaper_service.h"

namespace astralia::settings {

Caps settings_caps(const Capabilities &capabilities) {
    Caps caps;
    caps.animation = capabilities.animations;
    caps.idle = capabilities.idle;
    caps.rain = capabilities.rain;
    caps.visualizer = capabilities.visualizer;
    caps.animated_wallpaper = capabilities.animated_wallpaper;
    caps.lock_wallpaper = capabilities.lock;
    return caps;
}

namespace {

namespace cfg = settings_config;

WallpaperSlot slot_from(int value) {
    switch (value) {
    case static_cast<int>(WallpaperSlot::animated):
        return WallpaperSlot::animated;
    case static_cast<int>(WallpaperSlot::lock):
        return WallpaperSlot::lock;
    default:
        return WallpaperSlot::still;
    }
}

constexpr uint64_t owner_tab_fade = 20;
constexpr uint64_t owner_field_anim = 10000;

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
    override.osd = config.default_osd;
    override.notifications = config.default_notifications;
    override.autohide = config.autohide;
    override.ambient_enabled = config.ambient_enabled;
    override.ambient_timeout = config.ambient_timeout;
    override.screensaver_enabled = config.screensaver_enabled;
    override.screensaver_timeout = config.screensaver_timeout;
    return override;
}

} // namespace

const std::vector<TabInfo> &all_tabs() {
    static const std::vector<TabInfo> tabs{
        {Tab::general, "General", icon::adjustments},
        {Tab::ambient, "Ambient", icon::wave_sine},
        {Tab::bar, "Bar", icon::layout_navbar},
        {Tab::palette, "Color Palette", icon::palette_icon},
        {Tab::displays, "Displays", icon::device_desktop},
        {Tab::lock, "Lock Screen", icon::lock},
        {Tab::wallpaper, "Wallpaper", icon::wallpaper},
    };
    return tabs;
}

std::string field_text(const Config &config, Field field, const std::string &monitor) {
    switch (field) {
    case Field::wallpaper_dir:
        return path_collapse_home(config.wallpaper_dir);
    case Field::wallpaper_animated_dir:
        return path_collapse_home(config.wallpaper_animated_dir);
    case Field::lock_wallpaper_dir:
        return path_collapse_home(config.lock_wallpaper_dir);
    case Field::ambient_timeout:
        return std::to_string(ambient_timeout_for(config, monitor));
    case Field::screensaver_timeout:
        return std::to_string(screensaver_timeout_for(config, monitor));
    case Field::visualizer_fps:
        return std::to_string(config.visualizer.fps);
    case Field::visualizer_thin:
        return trim_float(config.visualizer.particle_thin);
    case Field::visualizer_size:
        return std::to_string(config.visualizer.particle_size);
    case Field::visualizer_complexity:
        return std::to_string(config.visualizer.fractal_complexity);
    case Field::visualizer_glow_directions:
        return trim_float(config.visualizer.glow_directions);
    case Field::visualizer_glow_quality:
        return trim_float(config.visualizer.glow_quality);
    case Field::none:
        break;
    }
    return "";
}

void apply_field(Config &config, Field field, const std::string &text, const std::string &monitor) {
    try {
        switch (field) {
        case Field::wallpaper_dir:
            config.wallpaper_dir = path_expand_home(text);
            break;
        case Field::wallpaper_animated_dir:
            config.wallpaper_animated_dir = path_expand_home(text);
            break;
        case Field::lock_wallpaper_dir:
            config.lock_wallpaper_dir = path_expand_home(text);
            break;
        case Field::ambient_timeout: {
            auto value = static_cast<uint32_t>(
                std::clamp(std::stoi(text), cfg::idle_timeout_min, cfg::idle_timeout_max));
            (monitor.empty() ? config.ambient_timeout
                             : config.monitor_overrides[monitor].ambient_timeout) = value;
            break;
        }
        case Field::screensaver_timeout: {
            auto value = static_cast<uint32_t>(
                std::clamp(std::stoi(text), cfg::idle_timeout_min, cfg::idle_timeout_max));
            (monitor.empty() ? config.screensaver_timeout
                             : config.monitor_overrides[monitor].screensaver_timeout) = value;
            break;
        }
        case Field::visualizer_fps:
            config.visualizer.fps =
                std::clamp(std::stoi(text), visualizer_config::fps_min, visualizer_config::fps_max);
            break;
        case Field::visualizer_thin:
            config.visualizer.particle_thin =
                std::clamp(std::stof(text), visualizer_config::particle_thin_min,
                           visualizer_config::particle_thin_max);
            break;
        case Field::visualizer_size:
            config.visualizer.particle_size = visualizer_config::particle_size;
            break;
        case Field::visualizer_complexity:
            config.visualizer.fractal_complexity =
                std::clamp(std::stoi(text), visualizer_config::complexity_min,
                           visualizer_config::complexity_max);
            break;
        case Field::visualizer_glow_directions:
            config.visualizer.glow_directions =
                std::clamp(std::stof(text), visualizer_config::glow_directions_min,
                           visualizer_config::glow_directions_max);
            break;
        case Field::visualizer_glow_quality:
            config.visualizer.glow_quality =
                std::clamp(std::stof(text), visualizer_config::glow_quality_min,
                           visualizer_config::glow_quality_max);
            break;
        case Field::none:
            break;
        }
    } catch (const std::exception &) {
        log::error("settings: could not parse '{}' for field {}, keeping the "
                   "previous value",
                   text, static_cast<int>(field));
    }
}

Model::Model(Caps caps, Hooks hooks) : caps_(std::move(caps)), hooks_(std::move(hooks)) {
    for (const TabInfo &info : all_tabs()) {
        bool present = true;
        switch (info.tab) {
        case Tab::ambient:
            present = caps_.rain || caps_.visualizer;
            break;
        case Tab::general:
            present = caps_.animation;
            break;
        case Tab::lock:
            present = caps_.lock_wallpaper;
            break;
        default:
            break;
        }
        if (present) {
            tabs_.push_back(info);
        }
    }
    active_ = pending_ = Tab::wallpaper;
}

Model::~Model() { *alive_ = false; }

void Model::changed() {
    if (on_changed) {
        on_changed();
    }
}

void Model::open() {
    open_ = true;
    dropdown_open_ = -1;
    active_ = pending_ = Tab::wallpaper;
    menu_ = pending_menu_ = true;
    animations_.cancel_owner(owner_tab_fade);
    tab_alpha_ = 1.0f;
    sequence_.open();
    sync();
}

void Model::close() {
    if (!open_ || sequence_.closing()) {
        return;
    }
    commit_field();
    sequence_.close([this] {
        open_ = false;
        for (WallpaperSlot slot :
             {WallpaperSlot::still, WallpaperSlot::animated, WallpaperSlot::lock}) {
            WallpaperPicker &picker = this->picker(slot);
            picker.files.clear();
            picker.dir.clear();
            picker.generation++;
            picker.scanning = false;
        }
        if (on_closed) {
            on_closed();
        }
    });
}

BarStyle Model::effective_bar_style() const {
    BarStyle style = config().bar_style;
    if (std::ranges::find(caps_.bar_styles, style) != caps_.bar_styles.end()) {
        return style;
    }
    return std::ranges::find(caps_.bar_styles, BarStyle::continuous) != caps_.bar_styles.end()
               ? BarStyle::continuous
               : caps_.bar_styles.front();
}

void Model::sync() {
    monitors_.clear();
    if (hooks_.monitors) {
        for (std::string &name : hooks_.monitors()) {
            if (!name.empty()) {
                monitors_.push_back(std::move(name));
            }
        }
    }
    std::ranges::sort(monitors_);
    auto known = [this](const std::string &name) {
        return std::ranges::find(monitors_, name) != monitors_.end();
    };
    if (!displays_monitor_.empty() && !known(displays_monitor_)) {
        displays_monitor_.clear();
    }
    std::string focused = hooks_.focused_monitor ? hooks_.focused_monitor() : std::string();
    const Config &current = config();
    for (WallpaperSlot slot :
         {WallpaperSlot::still, WallpaperSlot::animated, WallpaperSlot::lock}) {
        WallpaperPicker &picker = this->picker(slot);
        if (monitors_.size() == 1) {
            picker.region = monitors_[0];
        } else if (picker.region.empty() || !known(picker.region)) {
            picker.region =
                known(focused) ? focused : (monitors_.empty() ? std::string() : monitors_[0]);
        }
        int count =
            picker.region.empty() ? 1 : wallpaper_column_count(current, picker.region, slot);
        picker.column = std::clamp(picker.column, 0, count - 1);
    }
    if (open_ && !sequence_.closing() && !menu_) {
        std::optional<WallpaperSlot> slot;
        if (active_ == Tab::wallpaper) {
            slot = caps_.animated_wallpaper && current.wallpaper_animated_enabled
                       ? WallpaperSlot::animated
                       : WallpaperSlot::still;
        } else if (active_ == Tab::lock && current.lock_wallpaper_custom) {
            slot = WallpaperSlot::lock;
        }
        if (slot) {
            const std::string &dir = wallpaper_slot_dir(current, *slot);
            WallpaperPicker &picker = this->picker(*slot);
            if (picker.dir != dir) {
                scan(picker, dir, *slot == WallpaperSlot::animated);
            }
        }
    }
}

void Model::rescan(WallpaperSlot slot) {
    scan(picker(slot), wallpaper_slot_dir(config(), slot), slot == WallpaperSlot::animated);
}

void Model::scan(WallpaperPicker &picker, const std::string &dir, bool animated) {
    picker.dir = dir;
    picker.scanning = true;
    picker.scroll = 0.0f;
    uint64_t generation = ++picker.generation;
    std::weak_ptr<bool> alive = alive_;
    std::thread([this, alive, &picker, dir, generation, animated] {
        std::vector<std::string> found = wallpaper_list_files(dir, animated, cfg::max_images);
        DeferredCall::call_later(
            [this, alive, &picker, found = std::move(found), generation]() mutable {
                if (!alive.lock() || generation != picker.generation) {
                    return;
                }
                picker.files = std::move(found);
                picker.scanning = false;
                changed();
            });
    }).detach();
}

void Model::select_tab(Tab tab) { go(false, tab); }

void Model::show_menu() { go(true, active_); }

void Model::go(bool menu, Tab tab) {
    if (menu ? pending_menu_ : (!pending_menu_ && tab == pending_)) {
        return;
    }
    pending_menu_ = menu;
    pending_ = tab;
    dropdown_open_ = -1;
    animations_.animate(
        tab_alpha_, 0.0f, cfg::tab_fade_ms, astralia::Easing::EaseOutCubic,
        [this](float v) { tab_alpha_ = v; },
        [this] {
            menu_ = pending_menu_;
            active_ = pending_;
            sync();
            animations_.animate(
                0.0f, 1.0f, cfg::tab_fade_ms, astralia::Easing::EaseOutCubic,
                [this](float v) { tab_alpha_ = v; }, {}, owner_tab_fade);
        },
        owner_tab_fade);
    sync();
}

void Model::focus_field(Field field) {
    if (field == focused_) {
        return;
    }
    commit_field();
    focused_ = field;
    field_.text = field_text(config(), field, displays_monitor_);
    field_.preedit.clear();
    field_.cursor_idle_visible = true;
    text_field_type_anim_settle(field_anim_, animations_, owner_field_anim, field_.text);
    if (on_text_focus) {
        on_text_focus(true);
    }
}

void Model::commit_field() {
    if (focused_ == Field::none) {
        return;
    }
    Field field = focused_;
    std::string text = field_.text;
    std::string monitor = displays_monitor_;
    focused_ = Field::none;
    field_.preedit.clear();
    text_field_type_anim_clear(field_anim_, animations_, owner_field_anim);
    if (on_text_focus) {
        on_text_focus(false);
    }
    edit([&](Config &config) { apply_field(config, field, text, monitor); });
}

void Model::edit(const std::function<void(Config &)> &apply) {
    hooks_.update(apply);
    sync();
    changed();
}

bool Model::key(const input::KeyEvent &event) {
    if (!open_ || sequence_.closing()) {
        return false;
    }
    if (focused_ == Field::none) {
        if (event.kind == input::KeyKind::Escape) {
            if (!pending_menu_) {
                show_menu();
                changed();
                return true;
            }
            if (on_close_requested) {
                on_close_requested();
            }
            return true;
        }
        if (!pending_menu_ &&
            (event.kind == input::KeyKind::Up || event.kind == input::KeyKind::Down)) {
            auto it = std::ranges::find(tabs_, pending_, &TabInfo::tab);
            if (it == tabs_.end()) {
                return false;
            }
            auto index = static_cast<std::ptrdiff_t>(it - tabs_.begin());
            index =
                std::clamp<std::ptrdiff_t>(index + (event.kind == input::KeyKind::Down ? 1 : -1), 0,
                                           static_cast<std::ptrdiff_t>(tabs_.size()) - 1);
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
        focused_ = Field::none;
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

void Model::commit_text(const std::string &text) {
    field_.text += text;
    field_.preedit.clear();
    text_field_type_anim_sync(field_anim_, animations_, owner_field_anim, field_.text);
    changed();
}

void Model::delete_before(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        text_field_backspace(field_.text);
    }
    text_field_type_anim_sync(field_anim_, animations_, owner_field_anim, field_.text);
    changed();
}

void Model::set_preedit(std::string text) {
    field_.preedit = std::move(text);
    field_.cursor_idle_visible = true;
    changed();
}

void Model::toggle_caret() {
    if (focused_ != Field::none) {
        text_field_idle_toggle(field_);
        changed();
    }
}

bool Model::clickable(double x, double y) const {
    return std::ranges::any_of(regions_, [&](const Region &r) {
        return r.box.w > 0 && x >= r.box.x && x < r.box.x + r.box.w && y >= r.box.y &&
               y < r.box.y + r.box.h;
    });
}

void Model::click(double x, double y) {
    if (!open_ || sequence_.closing() || !sequence_.settled()) {
        return;
    }
    for (const Region &region : regions_) {
        const ui::Box &b = region.box;
        if (b.w > 0 && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
            if (region.act != Act::dropdown_toggle) {
                dropdown_open_ = -1;
            }
            dispatch(region);
            return;
        }
    }
    bool inside =
        x >= panel_.x && x < panel_.x + panel_.w && y >= panel_.y && y < panel_.y + panel_.h;
    if (!inside && on_close_requested) {
        on_close_requested();
    }
}

void Model::scroll(float pixels) {
    if (!open_ || sequence_.closing() || !sequence_.settled()) {
        return;
    }
    if (menu_ || (active_ != Tab::wallpaper && active_ != Tab::lock)) {
        return;
    }
    WallpaperSlot slot = WallpaperSlot::lock;
    if (active_ == Tab::wallpaper) {
        slot = caps_.animated_wallpaper && config().wallpaper_animated_enabled
                   ? WallpaperSlot::animated
                   : WallpaperSlot::still;
    }
    WallpaperPicker &picker = this->picker(slot);
    float inset_w = picker.grid_width - cfg::grid_inset * 2.0f;
    float inset_h = picker.grid_height - cfg::grid_inset * 2.0f;
    int columns = cfg::thumb_columns;
    (void)inset_w;
    std::size_t rows = (picker.files.size() + static_cast<std::size_t>(columns) - 1) /
                       static_cast<std::size_t>(columns);
    float content =
        rows == 0 ? 0.0f
                  : static_cast<float>(rows) * (cfg::thumb_size + cfg::thumb_gap) - cfg::thumb_gap;
    picker.scroll = std::clamp(picker.scroll + pixels, 0.0f, std::max(0.0f, content - inset_h));
    changed();
}

void Model::dispatch(const Region &region) {
    const Config &current = config();
    auto monitor = [&](int index) {
        return index < 0 || static_cast<std::size_t>(index) >= monitors_.size()
                   ? std::string()
                   : monitors_[static_cast<std::size_t>(index)];
    };
    switch (region.act) {
    case Act::close:
        if (on_close_requested) {
            on_close_requested();
        }
        return;
    case Act::tab:
        commit_field();
        select_tab(static_cast<Tab>(region.a));
        changed();
        return;
    case Act::menu:
        commit_field();
        show_menu();
        changed();
        return;
    case Act::field_focus:
        focus_field(static_cast<Field>(region.a));
        changed();
        return;
    case Act::displays_monitor:
        displays_monitor_ = monitor(region.a);
        changed();
        return;
    default:
        break;
    }
    commit_field();
    const std::string &target = displays_monitor_;
    switch (region.act) {
    case Act::toggle_override:
        edit([&](Config &config) {
            MonitorOverride &override = config.monitor_overrides[target];
            if (!override.enabled) {
                enable_defaults(override, current);
            }
            override.enabled = !override.enabled;
        });
        return;
    case Act::toggle_osd:
    case Act::toggle_notifications:
    case Act::toggle_autohide:
    case Act::idle_ambient:
    case Act::idle_screensaver: {
        Act act = region.act;
        edit([&](Config &config) {
            MonitorOverride *override =
                target.empty() ? nullptr : &config.monitor_overrides[target];
            auto flip = [&](bool &defaults, bool MonitorOverride::*field) {
                bool &value = override != nullptr ? override->*field : defaults;
                value = !value;
            };
            switch (act) {
            case Act::toggle_osd:
                flip(config.default_osd, &MonitorOverride::osd);
                break;
            case Act::toggle_notifications:
                flip(config.default_notifications, &MonitorOverride::notifications);
                break;
            case Act::toggle_autohide:
                flip(config.autohide, &MonitorOverride::autohide);
                break;
            case Act::idle_ambient:
                flip(config.ambient_enabled, &MonitorOverride::ambient_enabled);
                break;
            default:
                flip(config.screensaver_enabled, &MonitorOverride::screensaver_enabled);
                break;
            }
        });
        return;
    }
    case Act::idle_enable:
        edit([](Config &config) { config.idle_enabled = !config.idle_enabled; });
        return;
    case Act::idle_ambient_reset:
        edit([&](Config &config) {
            config.monitor_overrides[displays_monitor_].ambient_timeout = config.ambient_timeout;
        });
        return;
    case Act::idle_screensaver_reset:
        edit([&](Config &config) {
            config.monitor_overrides[displays_monitor_].screensaver_timeout =
                config.screensaver_timeout;
        });
        return;
    case Act::bar_style:
        if (region.a >= 0 && static_cast<std::size_t>(region.a) < caps_.bar_styles.size()) {
            BarStyle style = caps_.bar_styles[static_cast<std::size_t>(region.a)];
            edit([style](Config &config) { config.bar_style = style; });
        }
        return;
    case Act::rain_mode:
        edit([&](Config &config) {
            config.rain.mode = region.a == 1 ? RainMode::Stiletto : RainMode::Matrix;
        });
        return;
    case Act::rain_async:
        edit([](Config &config) { config.rain.async_speed = !config.rain.async_speed; });
        return;
    case Act::visualizer_shape:
        edit([&](Config &config) {
            config.visualizer.visualizer_shape =
                region.a == 1 ? VisualizerShape::Sphere : VisualizerShape::Bar;
        });
        return;
    case Act::visualizer_reset:
        edit([&](Config &config) {
            VisualizerParams defaults;
            switch (static_cast<Field>(region.a)) {
            case Field::visualizer_fps:
                config.visualizer.fps = defaults.fps;
                break;
            case Field::visualizer_thin:
                config.visualizer.particle_thin = defaults.particle_thin;
                break;
            case Field::visualizer_size:
                config.visualizer.particle_size = defaults.particle_size;
                break;
            case Field::visualizer_complexity:
                config.visualizer.fractal_complexity = defaults.fractal_complexity;
                break;
            case Field::visualizer_glow_directions:
                config.visualizer.glow_directions = defaults.glow_directions;
                break;
            case Field::visualizer_glow_quality:
                config.visualizer.glow_quality = defaults.glow_quality;
                break;
            default:
                break;
            }
        });
        return;
    case Act::animation_disable:
        edit([](Config &config) { config.animations_disabled = !config.animations_disabled; });
        return;
    default:
        dispatch_wallpaper(region);
        return;
    }
}

void Model::dispatch_wallpaper(const Region &region) {
    WallpaperSlot slot = slot_from(region.b);
    WallpaperPicker &picker = this->picker(slot);
    switch (region.act) {
    case Act::wallpaper_default:
        edit([](Config &config) { config.default_wallpaper = !config.default_wallpaper; });
        return;
    case Act::wallpaper_animated:
        edit([](Config &config) {
            config.wallpaper_animated_enabled = !config.wallpaper_animated_enabled;
        });
        return;
    case Act::lock_wallpaper_mode:
        edit([&](Config &config) { config.lock_wallpaper_custom = region.a == 1; });
        return;
    case Act::wallpaper_region: {
        int monitor = region.a;
        WallpaperPicker &target = this->picker(slot_from(region.b >> 8));
        if (monitor >= 0 && static_cast<std::size_t>(monitor) < monitors_.size()) {
            target.region = monitors_[static_cast<std::size_t>(monitor)];
            target.column = region.b & 0xff;
        }
        changed();
        return;
    }
    case Act::wallpaper_pick:
        if (region.a >= 0 && static_cast<std::size_t>(region.a) < picker.files.size()) {
            std::string path = picker.files[static_cast<std::size_t>(region.a)];
            edit([&](Config &config) {
                wallpaper_set_column(config, picker.region, picker.column, path, slot);
            });
        }
        return;
    case Act::wallpaper_remove:
        edit([&](Config &config) {
            if (caps_.wallpaper_columns || slot == WallpaperSlot::lock) {
                wallpaper_set_column(config, picker.region, picker.column, "", slot);
            } else {
                wallpaper_clear_image(config, picker.region);
            }
        });
        return;
    case Act::wallpaper_fill:
        edit([&](Config &config) {
            wallpaper_set_fill_mode(config, picker.region, picker.column,
                                    region.a == 1 ? "fit" : "crop", slot);
        });
        return;
    case Act::wallpaper_count:
        edit([&](Config &config) {
            int count = std::clamp(wallpaper_column_count(config, picker.region, slot) + region.a,
                                   1, cfg::max_columns);
            wallpaper_set_column_count(config, picker.region, count, slot);
        });
        return;
    case Act::wallpaper_rescan:
        rescan(slot);
        changed();
        return;
    case Act::dropdown_toggle:
        dropdown_open_ = dropdown_open_ == region.a ? -1 : region.a;
        changed();
        return;
    case Act::dropdown_dismiss:
        dropdown_open_ = -1;
        changed();
        return;
    case Act::capture_pick:
        edit([&](Config &config) {
            bool valid = region.a >= 0 && static_cast<std::size_t>(region.a) < monitors_.size();
            config.capture_monitor = valid ? monitors_[static_cast<std::size_t>(region.a)] : "";
            config.capture_column = valid ? region.b : 0;
        });
        return;
    case Act::palette_source:
        edit([&](Config &config) {
            config.palette_source =
                region.a == 1 ? PaletteSource::region_capture : PaletteSource::default_palette;
        });
        return;
    case Act::color_scheme:
        edit([&](Config &config) {
            config.color_scheme = region.a == 1 ? ColorScheme::dark : ColorScheme::light;
        });
        return;
    default:
        return;
    }
}

} // namespace astralia::settings
