use std::collections::BTreeMap;

use serde_json::{Map, Value};

use crate::base::paths::home;
use crate::config::file::{collapse_home, expand_home, user_config_path};
use crate::config::visualizer_config;

const INSTALLED_WALLPAPER: &str = "/usr/share/astralia-shell/wallpaper/default.jpg";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BarStyle {
    #[default]
    Continuous,
    Okinami,
    Pop,
}

impl BarStyle {
    pub const ALL: [BarStyle; 3] = [BarStyle::Continuous, BarStyle::Okinami, BarStyle::Pop];

    pub fn name(self) -> &'static str {
        match self {
            BarStyle::Continuous => "continuous",
            BarStyle::Okinami => "okinami",
            BarStyle::Pop => "pop",
        }
    }
    /// The name the settings show.
    pub fn label(self) -> &'static str {
        match self {
            BarStyle::Continuous => "Continuous",
            BarStyle::Okinami => "Okinami",
            BarStyle::Pop => "Pop!",
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PaletteSource {
    #[default]
    DefaultPalette,
    RegionCapture,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MonitorOverride {
    pub enabled: bool,
    pub osd: bool,
    pub notifications: bool,
    pub autohide: bool,
    /// Whether `autohide`, `bar_disabled` and `expand_widgets` apply; separate from `enabled`.
    pub bar_override: bool,
    pub bar_disabled: bool,
    pub expand_widgets: bool,
    pub ambient_enabled: bool,
    pub ambient_timeout: u32,
    pub screensaver_enabled: bool,
    pub screensaver_timeout: u32,
    pub lock: bool,
}

impl Default for MonitorOverride {
    fn default() -> Self {
        MonitorOverride {
            enabled: false,
            osd: true,
            notifications: true,
            autohide: false,
            bar_override: false,
            bar_disabled: false,
            expand_widgets: false,
            ambient_enabled: true,
            ambient_timeout: 150,
            screensaver_enabled: true,
            screensaver_timeout: 300,
            lock: true,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum VisualizerShape {
    #[default]
    Bar,
    Sphere,
}

#[derive(Clone, Debug, PartialEq)]
pub struct VisualizerParams {
    pub visualizer_shape: VisualizerShape,
    pub fps: i32,
    pub particle_thin: f32,
    pub particle_size: i32,
    pub fractal_complexity: i32,
    pub glow_directions: f32,
    pub glow_quality: f32,
}

impl Default for VisualizerParams {
    fn default() -> Self {
        VisualizerParams {
            visualizer_shape: VisualizerShape::Bar,
            fps: visualizer_config::FPS,
            particle_thin: 0.12,
            particle_size: visualizer_config::PARTICLE_SIZE,
            fractal_complexity: 3,
            glow_directions: 16.0,
            glow_quality: 6.0,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum RainMode {
    #[default]
    Matrix,
    Stiletto,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RainParams {
    pub mode: RainMode,
    pub async_speed: bool,
}

type Columns = BTreeMap<String, Vec<String>>;
type Counts = BTreeMap<String, i32>;

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub wallpaper_path: String,
    pub wallpaper_dir: String,
    pub wallpaper_columns: Columns,
    pub wallpaper_column_counts: Counts,
    pub wallpaper_fill_modes: Columns,

    pub wallpaper_animated_enabled: bool,
    pub wallpaper_animated_dir: String,
    pub wallpaper_animated_columns: Columns,
    pub wallpaper_animated_column_counts: Counts,
    pub wallpaper_animated_fill_modes: Columns,

    pub lock_wallpaper_custom: bool,
    pub lock_wallpaper_dir: String,
    pub lock_wallpaper_columns: Columns,
    pub lock_wallpaper_column_counts: Counts,
    pub lock_wallpaper_fill_modes: Columns,

    pub palette_source: PaletteSource,
    pub capture_monitor: String,
    pub capture_column: i32,

    pub autohide: bool,
    pub bar_disabled: bool,
    pub expand_widgets: bool,
    pub bar_style: BarStyle,
    pub default_osd: bool,
    pub default_notifications: bool,
    pub default_wallpaper: bool,
    pub default_lock: bool,
    pub monitor_overrides: BTreeMap<String, MonitorOverride>,

    pub animations_disabled: bool,

    pub logout_logo_path: String,
    pub sync_compositor_borders: bool,
    pub idle_logo_path: String,

    pub idle_enabled: bool,
    pub ambient_enabled: bool,
    pub ambient_timeout: u32,
    pub screensaver_enabled: bool,
    pub screensaver_timeout: u32,

    pub visualizer: VisualizerParams,
    pub rain: RainParams,
}

impl Config {
    /// The defaults with directories under `home`.
    pub fn with_home(home: &str) -> Config {
        Config {
            wallpaper_path: default_wallpaper_path(),
            wallpaper_dir: format!("{home}/Pictures"),
            wallpaper_columns: Columns::new(),
            wallpaper_column_counts: Counts::new(),
            wallpaper_fill_modes: Columns::new(),
            wallpaper_animated_enabled: false,
            wallpaper_animated_dir: format!("{home}/Videos"),
            wallpaper_animated_columns: Columns::new(),
            wallpaper_animated_column_counts: Counts::new(),
            wallpaper_animated_fill_modes: Columns::new(),
            lock_wallpaper_custom: false,
            lock_wallpaper_dir: format!("{home}/Pictures"),
            lock_wallpaper_columns: Columns::new(),
            lock_wallpaper_column_counts: Counts::new(),
            lock_wallpaper_fill_modes: Columns::new(),
            palette_source: PaletteSource::DefaultPalette,
            capture_monitor: String::new(),
            capture_column: 0,
            autohide: false,
            bar_disabled: false,
            expand_widgets: false,
            bar_style: BarStyle::Continuous,
            default_osd: true,
            default_notifications: true,
            default_wallpaper: true,
            default_lock: true,
            monitor_overrides: BTreeMap::new(),
            animations_disabled: false,
            logout_logo_path: String::new(),
            sync_compositor_borders: true,
            idle_logo_path: String::new(),
            idle_enabled: true,
            ambient_enabled: true,
            ambient_timeout: 150,
            screensaver_enabled: true,
            screensaver_timeout: 300,
            visualizer: VisualizerParams::default(),
            rain: RainParams::default(),
        }
    }
}

impl Default for Config {
    fn default() -> Self {
        Config::with_home(&home())
    }
}

pub fn default_wallpaper_path() -> String {
    if std::path::Path::new(INSTALLED_WALLPAPER).exists() {
        return INSTALLED_WALLPAPER.to_string();
    }
    concat!(env!("CARGO_MANIFEST_DIR"), "/assets/wallpaper/default.jpg").to_string()
}

/// The shell's `config.json`.
pub fn config_path() -> String {
    user_config_path("astralia/config.json")
}

fn active_override<'a>(cfg: &'a Config, monitor: &str) -> Option<&'a MonitorOverride> {
    cfg.monitor_overrides.get(monitor).filter(|o| o.enabled)
}

pub fn osd_enabled_for(cfg: &Config, monitor: &str) -> bool {
    active_override(cfg, monitor).map_or(cfg.default_osd, |o| o.osd)
}

pub fn notifications_enabled_for(cfg: &Config, monitor: &str) -> bool {
    active_override(cfg, monitor).map_or(cfg.default_notifications, |o| o.notifications)
}

fn bar_override<'a>(cfg: &'a Config, monitor: &str) -> Option<&'a MonitorOverride> {
    cfg.monitor_overrides.get(monitor).filter(|o| o.bar_override)
}

pub fn autohide_enabled_for(cfg: &Config, monitor: &str) -> bool {
    bar_override(cfg, monitor).map_or(cfg.autohide, |o| o.autohide)
}

pub fn bar_disabled_for(cfg: &Config, monitor: &str) -> bool {
    bar_override(cfg, monitor).map_or(cfg.bar_disabled, |o| o.bar_disabled)
}

pub fn expand_widgets_for(cfg: &Config, monitor: &str) -> bool {
    bar_override(cfg, monitor).map_or(cfg.expand_widgets, |o| o.expand_widgets)
}

pub fn ambient_enabled_for(cfg: &Config, monitor: &str) -> bool {
    cfg.idle_enabled && active_override(cfg, monitor).map_or(cfg.ambient_enabled, |o| o.ambient_enabled)
}

pub fn ambient_timeout_for(cfg: &Config, monitor: &str) -> u32 {
    active_override(cfg, monitor).map_or(cfg.ambient_timeout, |o| o.ambient_timeout)
}

pub fn screensaver_enabled_for(cfg: &Config, monitor: &str) -> bool {
    cfg.idle_enabled && active_override(cfg, monitor).map_or(cfg.screensaver_enabled, |o| o.screensaver_enabled)
}

pub fn screensaver_timeout_for(cfg: &Config, monitor: &str) -> u32 {
    active_override(cfg, monitor).map_or(cfg.screensaver_timeout, |o| o.screensaver_timeout)
}

pub fn lock_enabled_for(cfg: &Config, monitor: &str) -> bool {
    active_override(cfg, monitor).map_or(cfg.default_lock, |o| o.lock)
}

type Object = Map<String, Value>;

fn section<'a>(root: &'a Object, key: &str, legacy_key: &str) -> Option<&'a Object> {
    root.get(key).and_then(Value::as_object).or_else(|| root.get(legacy_key).and_then(Value::as_object))
}

fn object_in<'a>(parent: Option<&'a Object>, key: &str) -> Option<&'a Object> {
    parent?.get(key)?.as_object()
}

/// A present key decides even when wrongly typed (then the fallback); the legacy key is read only
/// when the new one is absent.
fn flag_or_legacy(object: Option<&Object>, key: &str, legacy_key: &str, fallback: bool) -> bool {
    let Some(object) = object else { return fallback };
    let found = object.get(key).or_else(|| object.get(legacy_key));
    found.and_then(Value::as_bool).unwrap_or(fallback)
}

fn flag(object: Option<&Object>, key: &str, fallback: bool) -> bool {
    flag_or_legacy(object, key, key, fallback)
}

fn number(object: Option<&Object>, key: &str) -> Option<f64> {
    object?.get(key)?.as_f64()
}

fn integer(object: Option<&Object>, key: &str, fallback: i32) -> i32 {
    number(object, key).map_or(fallback, |n| n as i32)
}

fn seconds(object: Option<&Object>, key: &str, fallback: u32) -> u32 {
    number(object, key).map_or(fallback, |n| n.max(0.0) as u32)
}

fn real(object: Option<&Object>, key: &str, fallback: f32) -> f32 {
    number(object, key).map_or(fallback, |n| n as f32)
}

fn text(object: Option<&Object>, key: &str, fallback: &str) -> String {
    object.and_then(|o| o.get(key)).and_then(Value::as_str).unwrap_or(fallback).to_string()
}

/// `~` expansion that leaves the path alone when `HOME` is unset.
fn expand(path: String, home: &str) -> String {
    if home.is_empty() { path } else { expand_home(&path, home) }
}

fn read_columns(wallpaper: Option<&Object>, key: &str, out: &mut Columns, home: Option<&str>) {
    let Some(map) = object_in(wallpaper, key) else { return };
    for (name, value) in map {
        let Some(items) = value.as_array() else { continue };
        let values = items.iter().filter_map(Value::as_str).map(|s| match home {
            Some(home) => expand(s.to_string(), home),
            None => s.to_string(),
        });
        out.insert(name.clone(), values.collect());
    }
}

fn read_counts(wallpaper: Option<&Object>, key: &str, out: &mut Counts) {
    let Some(map) = object_in(wallpaper, key) else { return };
    for (name, value) in map {
        if let Some(n) = value.as_f64() {
            out.insert(name.clone(), n as i32);
        }
    }
}

const RESERVED_DISPLAYS_KEYS: [&str; 10] = [
    "defaultBar",
    "defaultOsd",
    "defaultNotifications",
    "defaultWallpaper",
    "defaultLock",
    "defaultSpark",
    "defaultHeralds",
    "defaultExpanse",
    "defaultPenance",
    "idle",
];

pub fn parse_config(text: &str) -> Config {
    parse_config_in(text, &home())
}

/// `parse_config` with `~` and the default directories resolved against `home`.
pub fn parse_config_in(source: &str, home: &str) -> Config {
    let mut cfg = Config::with_home(home);
    let Ok(Value::Object(root)) = serde_json::from_str::<Value>(source) else { return cfg };
    let root = &root;

    let bar = section(root, "bar", "qixing");
    cfg.autohide = flag(bar, "autohideEnabled", cfg.autohide);
    cfg.bar_disabled = flag(bar, "disabled", cfg.bar_disabled);
    cfg.expand_widgets = flag(bar, "expandWidgets", cfg.expand_widgets);
    let style = text(bar, "style", BarStyle::Continuous.name());
    if let Some(style) = BarStyle::ALL.into_iter().find(|s| s.name() == style) {
        cfg.bar_style = style;
    }

    let wallpaper = section(root, "wallpaper", "expanse");
    cfg.wallpaper_path = expand(text(wallpaper, "path", &cfg.wallpaper_path), home);
    cfg.wallpaper_dir = expand(text(wallpaper, "dir", &cfg.wallpaper_dir), home);
    read_columns(wallpaper, "columns", &mut cfg.wallpaper_columns, Some(home));
    read_counts(wallpaper, "columnCounts", &mut cfg.wallpaper_column_counts);
    read_columns(wallpaper, "fillModes", &mut cfg.wallpaper_fill_modes, None);
    cfg.wallpaper_animated_enabled = flag(wallpaper, "animatedEnabled", cfg.wallpaper_animated_enabled);
    cfg.wallpaper_animated_dir = expand(text(wallpaper, "animatedDir", &cfg.wallpaper_animated_dir), home);
    read_columns(wallpaper, "animatedColumns", &mut cfg.wallpaper_animated_columns, Some(home));
    read_counts(wallpaper, "animatedColumnCounts", &mut cfg.wallpaper_animated_column_counts);
    read_columns(wallpaper, "animatedFillModes", &mut cfg.wallpaper_animated_fill_modes, None);
    cfg.lock_wallpaper_custom = flag(wallpaper, "lockCustom", cfg.lock_wallpaper_custom);
    cfg.lock_wallpaper_dir = expand(text(wallpaper, "lockDir", &cfg.lock_wallpaper_dir), home);
    read_columns(wallpaper, "lockColumns", &mut cfg.lock_wallpaper_columns, Some(home));
    read_counts(wallpaper, "lockColumnCounts", &mut cfg.lock_wallpaper_column_counts);
    read_columns(wallpaper, "lockFillModes", &mut cfg.lock_wallpaper_fill_modes, None);
    cfg.palette_source = if text(object_in(Some(root), "palette"), "source", "") == "region" {
        PaletteSource::RegionCapture
    } else {
        PaletteSource::DefaultPalette
    };
    cfg.capture_monitor = text(wallpaper, "captureMonitor", &cfg.capture_monitor);
    cfg.capture_column = integer(wallpaper, "captureColumn", cfg.capture_column);

    let displays = object_in(Some(root), "displays");
    cfg.default_osd = flag_or_legacy(displays, "defaultOsd", "defaultSpark", cfg.default_osd);
    cfg.default_notifications = flag_or_legacy(displays, "defaultNotifications", "defaultHeralds", cfg.default_notifications);
    cfg.default_wallpaper = flag_or_legacy(displays, "defaultWallpaper", "defaultExpanse", cfg.default_wallpaper);
    cfg.default_lock = flag_or_legacy(displays, "defaultLock", "defaultPenance", cfg.default_lock);
    for (name, value) in displays.into_iter().flatten() {
        let Some(value) = value.as_object().filter(|_| !RESERVED_DISPLAYS_KEYS.contains(&name.as_str())) else { continue };
        let value = Some(value);
        let d = MonitorOverride::default();
        let enabled = flag(value, "_enabled", d.enabled);
        let mo = MonitorOverride {
            enabled,
            osd: flag_or_legacy(value, "osd", "spark", d.osd),
            notifications: flag_or_legacy(value, "notifications", "heralds", d.notifications),
            autohide: flag(value, "autohide", d.autohide),
            // Written before the bar had its own switch, the display override covered autohide.
            bar_override: flag(value, "barOverride", enabled),
            bar_disabled: flag(value, "barDisabled", d.bar_disabled),
            expand_widgets: flag(value, "expandWidgets", cfg.expand_widgets),
            ambient_enabled: flag(value, "ambientEnabled", d.ambient_enabled),
            ambient_timeout: seconds(value, "ambientTimeoutSeconds", d.ambient_timeout),
            screensaver_enabled: flag(value, "screensaverEnabled", d.screensaver_enabled),
            screensaver_timeout: seconds(value, "screensaverTimeoutSeconds", d.screensaver_timeout),
            lock: flag_or_legacy(value, "lock", "penance", d.lock),
        };
        cfg.monitor_overrides.insert(name.clone(), mo);
    }

    let general = object_in(Some(root), "general");
    let legacy_disabled = flag(section(root, "animation", "animation"), "disabled", cfg.animations_disabled);
    cfg.animations_disabled = flag(general, "animationsDisabled", legacy_disabled);

    let idle = object_in(displays, "idle").or_else(|| section(root, "idle", "blink"));
    cfg.idle_enabled = flag(idle, "enabled", cfg.idle_enabled);
    cfg.ambient_enabled = flag(idle, "ambientEnabled", cfg.ambient_enabled);
    cfg.ambient_timeout = seconds(idle, "ambientTimeoutSeconds", cfg.ambient_timeout);
    cfg.screensaver_enabled = flag(idle, "screensaverEnabled", cfg.screensaver_enabled);
    cfg.screensaver_timeout = seconds(idle, "screensaverTimeoutSeconds", cfg.screensaver_timeout);
    cfg.idle_logo_path = expand(text(idle, "logoPath", &cfg.idle_logo_path), home);
    cfg.logout_logo_path = expand(text(general, "logoutLogoPath", &cfg.logout_logo_path), home);
    cfg.sync_compositor_borders = flag(general, "syncCompositorBorders", cfg.sync_compositor_borders);

    use visualizer_config as vc;
    let ambient = object_in(Some(root), "ambient");
    let visualizer = object_in(ambient, "visualizer").or_else(|| section(root, "visualizer", "resonance"));
    let v = &mut cfg.visualizer;
    v.fps = integer(visualizer, "fps", v.fps).clamp(vc::FPS_MIN, vc::FPS_MAX);
    v.particle_thin = real(visualizer, "particleThin", v.particle_thin).clamp(vc::PARTICLE_THIN_MIN, vc::PARTICLE_THIN_MAX);
    v.particle_size = vc::PARTICLE_SIZE;
    v.fractal_complexity = integer(visualizer, "fractalComplexity", v.fractal_complexity).clamp(vc::COMPLEXITY_MIN, vc::COMPLEXITY_MAX);
    v.glow_directions = real(visualizer, "glowDirections", v.glow_directions).clamp(vc::GLOW_DIRECTIONS_MIN, vc::GLOW_DIRECTIONS_MAX);
    v.glow_quality = real(visualizer, "glowQuality", v.glow_quality).clamp(vc::GLOW_QUALITY_MIN, vc::GLOW_QUALITY_MAX);
    v.visualizer_shape =
        if text(visualizer, "visualizerShape", "bar") == "sphere" { VisualizerShape::Sphere } else { VisualizerShape::Bar };

    let rain = object_in(ambient, "rain").or_else(|| object_in(Some(root), "rain"));
    cfg.rain.mode = if text(rain, "mode", "matrix") == "stiletto" { RainMode::Stiletto } else { RainMode::Matrix };
    cfg.rain.async_speed = flag(rain, "asyncSpeed", false);
    cfg
}

/// An ordered JSON value: the writer keeps insertion order, which `serde_json::Map` does not.
enum Json {
    Bool(bool),
    Number(f64),
    Str(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

fn object(items: Vec<(&str, Json)>) -> Json {
    Json::Object(items.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
}

fn write_string(out: &mut String, s: &str) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

/// Shortest round-trip digits, as `std::to_chars`, for the magnitudes a config holds.
fn write_number(out: &mut String, value: f64) {
    if value.is_finite() { out.push_str(&value.to_string()) } else { out.push_str("null") }
}

fn write_value(out: &mut String, value: &Json, depth: usize) {
    let newline = |out: &mut String, level: usize| {
        out.push('\n');
        out.push_str(&"  ".repeat(level));
    };
    match value {
        Json::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        Json::Number(n) => write_number(out, *n),
        Json::Str(s) => write_string(out, s),
        Json::Array(items) if items.is_empty() => out.push_str("[]"),
        Json::Array(items) => {
            out.push('[');
            for (i, item) in items.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                newline(out, depth + 1);
                write_value(out, item, depth + 1);
            }
            newline(out, depth);
            out.push(']');
        }
        Json::Object(items) if items.is_empty() => out.push_str("{}"),
        Json::Object(items) => {
            out.push('{');
            for (i, (key, item)) in items.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                newline(out, depth + 1);
                write_string(out, key);
                out.push_str(": ");
                write_value(out, item, depth + 1);
            }
            newline(out, depth);
            out.push('}');
        }
    }
}

pub fn serialize_config(cfg: &Config) -> String {
    serialize_config_in(cfg, &home())
}

/// `serialize_config` with paths under `home` written as `~`.
pub fn serialize_config_in(cfg: &Config, home: &str) -> String {
    use Json::{Bool, Number, Str};
    let path = |p: &str| Str(collapse_home(p, home));
    let columns = |c: &Columns, collapse: bool| {
        Json::Object(
            c.iter()
                .map(|(name, values)| {
                    let items = values.iter().map(|v| if collapse { path(v) } else { Str(v.clone()) });
                    (name.clone(), Json::Array(items.collect()))
                })
                .collect(),
        )
    };
    let counts = |c: &Counts| Json::Object(c.iter().map(|(name, n)| (name.clone(), Number(f64::from(*n)))).collect());
    // A float goes through its shortest decimal text, so 0.12f is written as 0.12.
    let float = |f: f32| Number(f.to_string().parse().unwrap_or(f64::from(f)));
    let int = |n: i32| Number(f64::from(n));
    let uint = |n: u32| Number(f64::from(n));

    let wallpaper = object(vec![
        ("path", path(&cfg.wallpaper_path)),
        ("dir", path(&cfg.wallpaper_dir)),
        ("columns", columns(&cfg.wallpaper_columns, true)),
        ("columnCounts", counts(&cfg.wallpaper_column_counts)),
        ("fillModes", columns(&cfg.wallpaper_fill_modes, false)),
        ("animatedEnabled", Bool(cfg.wallpaper_animated_enabled)),
        ("animatedDir", path(&cfg.wallpaper_animated_dir)),
        ("animatedColumns", columns(&cfg.wallpaper_animated_columns, true)),
        ("animatedColumnCounts", counts(&cfg.wallpaper_animated_column_counts)),
        ("animatedFillModes", columns(&cfg.wallpaper_animated_fill_modes, false)),
        ("lockCustom", Bool(cfg.lock_wallpaper_custom)),
        ("lockDir", path(&cfg.lock_wallpaper_dir)),
        ("lockColumns", columns(&cfg.lock_wallpaper_columns, true)),
        ("lockColumnCounts", counts(&cfg.lock_wallpaper_column_counts)),
        ("lockFillModes", columns(&cfg.lock_wallpaper_fill_modes, false)),
        ("captureMonitor", Str(cfg.capture_monitor.clone())),
        ("captureColumn", int(cfg.capture_column)),
    ]);
    let source = if cfg.palette_source == PaletteSource::RegionCapture { "region" } else { "default" };
    let palette = object(vec![("source", Str(source.into()))]);
    let idle = object(vec![
        ("enabled", Bool(cfg.idle_enabled)),
        ("ambientEnabled", Bool(cfg.ambient_enabled)),
        ("ambientTimeoutSeconds", uint(cfg.ambient_timeout)),
        ("screensaverEnabled", Bool(cfg.screensaver_enabled)),
        ("screensaverTimeoutSeconds", uint(cfg.screensaver_timeout)),
        ("logoPath", path(&cfg.idle_logo_path)),
    ]);
    let mut displays = vec![
        ("defaultOsd".to_string(), Bool(cfg.default_osd)),
        ("defaultNotifications".to_string(), Bool(cfg.default_notifications)),
        ("defaultWallpaper".to_string(), Bool(cfg.default_wallpaper)),
        ("defaultLock".to_string(), Bool(cfg.default_lock)),
        ("idle".to_string(), idle),
    ];
    for (name, ov) in &cfg.monitor_overrides {
        let mo = object(vec![
            ("_enabled", Bool(ov.enabled)),
            ("osd", Bool(ov.osd)),
            ("notifications", Bool(ov.notifications)),
            ("autohide", Bool(ov.autohide)),
            ("barOverride", Bool(ov.bar_override)),
            ("barDisabled", Bool(ov.bar_disabled)),
            ("expandWidgets", Bool(ov.expand_widgets)),
            ("ambientEnabled", Bool(ov.ambient_enabled)),
            ("ambientTimeoutSeconds", uint(ov.ambient_timeout)),
            ("screensaverEnabled", Bool(ov.screensaver_enabled)),
            ("screensaverTimeoutSeconds", uint(ov.screensaver_timeout)),
            ("lock", Bool(ov.lock)),
        ]);
        // A monitor named like a reserved key replaces it, as `Json::set` does.
        match displays.iter_mut().find(|(key, _)| key == name) {
            Some(slot) => slot.1 = mo,
            None => displays.push((name.clone(), mo)),
        }
    }
    let v = &cfg.visualizer;
    let shape = if v.visualizer_shape == VisualizerShape::Sphere { "sphere" } else { "bar" };
    let visualizer = object(vec![
        ("fps", int(v.fps)),
        ("particleThin", float(v.particle_thin)),
        ("particleSize", int(v.particle_size)),
        ("fractalComplexity", int(v.fractal_complexity)),
        ("glowDirections", float(v.glow_directions)),
        ("glowQuality", float(v.glow_quality)),
        ("visualizerShape", Str(shape.into())),
    ]);
    let mode = if cfg.rain.mode == RainMode::Stiletto { "stiletto" } else { "matrix" };
    let rain = object(vec![("mode", Str(mode.into())), ("asyncSpeed", Bool(cfg.rain.async_speed))]);
    let bar = object(vec![
        ("autohideEnabled", Bool(cfg.autohide)),
        ("disabled", Bool(cfg.bar_disabled)),
        ("expandWidgets", Bool(cfg.expand_widgets)),
        ("style", Str(cfg.bar_style.name().into())),
    ]);
    let general = object(vec![
        ("animationsDisabled", Bool(cfg.animations_disabled)),
        ("logoutLogoPath", path(&cfg.logout_logo_path)),
        ("syncCompositorBorders", Bool(cfg.sync_compositor_borders)),
    ]);
    let root = object(vec![
        ("ambient", object(vec![("rain", rain), ("visualizer", visualizer)])),
        ("bar", bar),
        ("displays", Json::Object(displays)),
        ("general", general),
        ("palette", palette),
        ("wallpaper", wallpaper),
    ]);
    let mut out = String::new();
    write_value(&mut out, &root, 0);
    out.push('\n');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const HOME: &str = "/home/tester";

    fn sample_config() -> &'static str {
        r#"{
  "animation": {"disabled": false},
  "bar": {"autohideEnabled": true, "expandWidgets": true, "style": "continuous"},
  "displays": {
    "defaultLock": true,
    "defaultNotifications": true,
    "defaultOsd": false,
    "defaultWallpaper": false,
    "eDP-1": {"_enabled": true, "osd": true, "lock": false, "ambientTimeoutSeconds": 90}
  },
  "idle": {"ambientEnabled": true, "ambientTimeoutSeconds": 150, "enabled": false, "screensaverEnabled": true, "screensaverTimeoutSeconds": 300},
  "rain": {"asyncSpeed": true, "mode": "stiletto"},
  "visualizer": {"fps": 30, "fractalComplexity": 3, "glowDirections": 16.0, "glowQuality": 6.0, "particleSize": 8, "particleThin": 0.11999999731779099, "visualizerShape": "sphere"},
  "wallpaper": {
    "animatedColumnCounts": {},
    "animatedColumns": {"eDP-1": ["~/clips/a.mp4"]},
    "animatedDir": "~/clips",
    "animatedEnabled": false,
    "animatedFillModes": {"eDP-1": ["crop"]},
    "columnCounts": {"eDP-1": 2},
    "columns": {"eDP-1": ["~/Pictures/a.jpg", "/abs/b.png"]},
    "dir": "~/Pictures",
    "fillModes": {"eDP-1": ["fit", "crop"]},
    "lockColumnCounts": {"eDP-1": 3},
    "lockColumns": {"eDP-1": ["~/Locks/a.jpg"]},
    "lockCustom": true,
    "lockDir": "~/Locks",
    "lockFillModes": {"eDP-1": ["fit"]}
  }
}"#
    }

    fn parse(text: &str) -> Config {
        parse_config_in(text, HOME)
    }

    fn strings(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn config_parse() {
        let defaults = parse("");
        assert_eq!(defaults, Config::with_home(HOME), "empty text gives the defaults");
        assert_eq!(parse("not json"), Config::with_home(HOME), "invalid json gives the defaults");
        assert_eq!(parse("[1,2]"), Config::with_home(HOME), "a non-object gives the defaults");

        let cfg = parse(sample_config());
        assert!(cfg.autohide && cfg.expand_widgets && cfg.bar_style == BarStyle::Continuous, "bar section");
        assert!(!defaults.expand_widgets, "expand widgets defaults off");
        assert!(!cfg.default_osd && !cfg.default_wallpaper && cfg.default_lock, "display defaults");
        let edp = &cfg.monitor_overrides["eDP-1"];
        assert!(edp.enabled && edp.osd && !edp.lock, "monitor override fields");
        assert!(edp.ambient_timeout == 90 && edp.screensaver_timeout == 300, "override seconds keep defaults when absent");
        assert!(
            edp.bar_override && !edp.bar_disabled && edp.expand_widgets,
            "an override without bar keys follows `_enabled` and the global expand"
        );
        let split = parse(r#"{"bar": {"disabled": true}, "displays": {"DP-1": {"_enabled": true, "barOverride": false, "expandWidgets": false}}}"#);
        let dp = &split.monitor_overrides["DP-1"];
        assert!(split.bar_disabled && !dp.bar_override && !dp.expand_widgets, "explicit bar keys win");
        assert!(!cfg.idle_enabled, "idle disabled");
        assert_eq!(cfg.wallpaper_columns["eDP-1"], strings(&["/home/tester/Pictures/a.jpg", "/abs/b.png"]), "home is expanded in columns");
        assert!(
            cfg.wallpaper_dir == "/home/tester/Pictures" && cfg.wallpaper_animated_dir == "/home/tester/clips",
            "home is expanded in directories"
        );
        assert!(
            cfg.wallpaper_column_counts["eDP-1"] == 2 && cfg.wallpaper_fill_modes["eDP-1"] == strings(&["fit", "crop"]),
            "counts and fill modes"
        );
        assert!(
            cfg.lock_wallpaper_custom
                && cfg.lock_wallpaper_dir == "/home/tester/Locks"
                && cfg.lock_wallpaper_columns["eDP-1"] == strings(&["/home/tester/Locks/a.jpg"])
                && cfg.lock_wallpaper_column_counts["eDP-1"] == 3
                && cfg.lock_wallpaper_fill_modes["eDP-1"] == strings(&["fit"]),
            "lock wallpaper keys load beside the desktop ones"
        );
        assert!(
            !defaults.lock_wallpaper_custom && defaults.lock_wallpaper_dir == defaults.wallpaper_dir && defaults.lock_wallpaper_columns.is_empty(),
            "the lock wallpaper defaults to Auto with the pictures directory"
        );

        let legacy = parse(r#"{"palette": {"source": "region", "scheme": "dark"}}"#);
        assert_eq!(legacy.palette_source, PaletteSource::RegionCapture, "a config with the retired scheme key still loads");

        assert_eq!(parse(r#"{"bar": {"style": "pop"}}"#).bar_style, BarStyle::Pop, "the pop style parses");
        assert_eq!(parse(r#"{"bar": {"style": "classic"}}"#).bar_style, BarStyle::Continuous, "an unknown style falls back");
        assert!(parse(r#"{"qixing": {"autohideEnabled": true}}"#).autohide, "legacy section names still load");
        assert!(!parse(r#"{"displays": {"defaultSpark": false}}"#).default_osd, "legacy display keys still load");
        let grouped = parse(
            r#"{
        "general": {"animationsDisabled": true},
        "displays": {"idle": {"enabled": false, "ambientTimeoutSeconds": 20}},
        "ambient": {"rain": {"mode": "stiletto"}, "visualizer": {"fps": 30}}}"#,
        );
        assert!(grouped.animations_disabled, "the general section loads");
        assert!(!grouped.idle_enabled && grouped.ambient_timeout == 20, "idle loads from the displays section");
        assert!(
            parse(r#"{"animation": {"disabled": true}, "idle": {"enabled": false}, "rain": {"mode": "stiletto"}}"#).animations_disabled
                && !parse(r#"{"idle": {"enabled": false}}"#).idle_enabled,
            "the previous top-level sections still load"
        );
        assert!(!parse(r#"{"bar": {"autohideEnabled": "yes"}}"#).autohide, "a wrongly typed value keeps the default");
        let capture = parse(r#"{"wallpaper": {"captureMonitor": "eDP-1", "captureColumn": 1}}"#);
        assert!(capture.capture_monitor == "eDP-1" && capture.capture_column == 1);
        assert!(cfg.rain.mode == RainMode::Stiletto && cfg.rain.async_speed, "rain");
        assert!(cfg.visualizer.visualizer_shape == VisualizerShape::Sphere && cfg.visualizer.fps == 30, "visualizer");
        assert!(
            grouped.rain.mode == RainMode::Stiletto && grouped.visualizer.fps == 30,
            "rain and visualizer load from the ambient section"
        );
        assert_eq!(parse(r#"{"rain": {"mode": "stiletto"}}"#).rain.mode, RainMode::Stiletto, "the previous top-level rain section still loads");
        assert_eq!(
            parse(r#"{"visualizer": {"fps": 9999, "glowQuality": 100}}"#).visualizer.fps,
            visualizer_config::FPS_MAX,
            "visualizer values are clamped"
        );
    }

    fn serialize(cfg: &Config) -> String {
        serialize_config_in(cfg, HOME)
    }

    #[test]
    fn config_round_trip() {
        let mut cfg = parse(sample_config());
        cfg.palette_source = PaletteSource::RegionCapture;
        cfg.capture_monitor = "eDP-1".into();
        cfg.capture_column = 1;
        let round = parse(&serialize(&cfg));
        let legacy = parse(r#"{"palette": {"source": "region", "scheme": "dark"}}"#);
        assert!(!serialize(&legacy).contains("scheme"), "the retired scheme key is not written back");
        assert_eq!(round.palette_source, PaletteSource::RegionCapture, "the palette source round-trips");
        assert!(round.capture_monitor == "eDP-1" && round.capture_column == 1, "region capture round-trips");
        assert_eq!(round, cfg, "serialize then parse round-trips");
        assert_eq!(serialize(&round), serialize(&cfg), "serialization is stable");
        let text = serialize(&cfg);
        assert!(text.contains("\"autohideEnabled\": true") && text.contains("\"expandWidgets\": true"), "bar keys are written");
        assert!(text.contains("~/Pictures/a.jpg"), "home is collapsed when saving");
        assert!(text.contains("\"lockCustom\": true") && text.contains("~/Locks/a.jpg"), "lock wallpaper keys are written");
        assert!(text.contains("\"particleThin\": 0.12"), "floats are written in their shortest form");
        let grouped = parse(
            r#"{"general": {"animationsDisabled": true}, "displays": {"idle": {"enabled": false, "ambientTimeoutSeconds": 20}},
            "ambient": {"rain": {"mode": "stiletto"}, "visualizer": {"fps": 30}}}"#,
        );
        assert_eq!(parse(&serialize(&grouped)), grouped, "the grouped layout round-trips");

        // The expected file for the same fixture, byte for byte.
        let expected = include_str!("testdata/serialized.json")
            .replace("/usr/share/astralia-shell/wallpaper/default.jpg", &default_wallpaper_path());
        assert_eq!(serialize(&parse(sample_config())), expected, "the file matches the expected layout");
    }

    #[test]
    fn json_writer_escapes() {
        let mut out = String::new();
        write_string(&mut out, "a\"b\\c\nd\u{1}é");
        assert_eq!(out, r#""a\"b\\c\nd\u0001é""#);
        let mut out = String::new();
        write_number(&mut out, 0.5);
        write_number(&mut out, -3.0);
        write_number(&mut out, f64::NAN);
        assert_eq!(out, "0.5-3null");
    }

    #[test]
    fn config_parse_tolerates_garbage() {
        assert_eq!(parse_config("not json"), Config::default());
        let c = parse(r#"{"bar": {"autohideEnabled": "yes"}, "general": {"animationsDisabled": true}, "displays": 7}"#);
        assert!(!c.autohide && c.animations_disabled && c.monitor_overrides.is_empty());
        let c = parse(r#"{"displays": {"eDP-1": {"_enabled": 1, "ambientTimeoutSeconds": -5}}, "wallpaper": {"columns": {"a": [1, "x"]}, "columnCounts": {"a": "2"}}}"#);
        let edp = &c.monitor_overrides["eDP-1"];
        assert!(!edp.enabled && edp.ambient_timeout == 0);
        assert!(c.wallpaper_columns["a"] == strings(&["x"]) && c.wallpaper_column_counts.is_empty());
    }

    #[test]
    fn monitor_overrides() {
        let mut cfg = Config::with_home(HOME);
        assert_eq!(osd_enabled_for(&cfg, "DP-1"), cfg.default_osd, "default osd");
        assert_eq!(notifications_enabled_for(&cfg, "DP-1"), cfg.default_notifications, "default notifications");
        assert_eq!(autohide_enabled_for(&cfg, "DP-1"), cfg.autohide, "default autohide");
        cfg.monitor_overrides.insert(
            "DP-1".into(),
            MonitorOverride { enabled: false, osd: false, notifications: false, autohide: true, ..Default::default() },
        );
        assert_eq!(osd_enabled_for(&cfg, "DP-1"), cfg.default_osd, "a disabled override is ignored");
        cfg.monitor_overrides.get_mut("DP-1").unwrap().enabled = true;
        assert!(!osd_enabled_for(&cfg, "DP-1") && !notifications_enabled_for(&cfg, "DP-1"), "an enabled override wins");
        assert_eq!(autohide_enabled_for(&cfg, "DP-1"), cfg.autohide, "the display override does not cover the bar");
        let o = cfg.monitor_overrides.get_mut("DP-1").unwrap();
        o.bar_override = true;
        o.bar_disabled = true;
        o.expand_widgets = true;
        assert!(
            autohide_enabled_for(&cfg, "DP-1") && bar_disabled_for(&cfg, "DP-1") && expand_widgets_for(&cfg, "DP-1"),
            "the bar override wins"
        );
        assert!(
            !bar_disabled_for(&cfg, "HDMI-1") && expand_widgets_for(&cfg, "HDMI-1") == cfg.expand_widgets,
            "other outputs keep the bar defaults"
        );
        cfg.monitor_overrides.get_mut("DP-1").unwrap().enabled = false;
        assert!(bar_disabled_for(&cfg, "DP-1"), "the bar override stands without the display override");
        cfg.monitor_overrides.get_mut("DP-1").unwrap().enabled = true;
        assert_eq!(osd_enabled_for(&cfg, "HDMI-1"), cfg.default_osd, "other outputs keep the default");
        assert_eq!(lock_enabled_for(&cfg, "HDMI-1"), cfg.default_lock, "default lock");
        cfg.monitor_overrides.get_mut("DP-1").unwrap().lock = false;
        assert!(!lock_enabled_for(&cfg, "DP-1"), "lock override");
        cfg.idle_enabled = false;
        assert!(
            !ambient_enabled_for(&cfg, "HDMI-1") && !screensaver_enabled_for(&cfg, "DP-1"),
            "idle management off disables ambient and screensaver everywhere"
        );
        cfg.idle_enabled = true;
        cfg.monitor_overrides.get_mut("DP-1").unwrap().ambient_timeout = 42;
        assert!(
            ambient_timeout_for(&cfg, "DP-1") == 42 && ambient_timeout_for(&cfg, "HDMI-1") == cfg.ambient_timeout,
            "ambient timeout override"
        );
        assert_eq!(screensaver_timeout_for(&cfg, "HDMI-1"), cfg.screensaver_timeout);
    }

    #[test]
    fn default_wallpaper_exists() {
        assert!(std::path::Path::new(&default_wallpaper_path()).exists());
    }
}
