# System integration

Talking to the system outside D-Bus: backlight, icon themes, `sysfs` sensors, PipeWire and idle timing. Sources: `src/service/brightness_service.cpp`, `src/service/icon_service.cpp`, `src/service/telemetry_service.cpp`, `src/service/audio_service.cpp`, `src/modules/idle/`.

- Set brightness through `brightnessctl`, never a direct `sysfs` write. The file is root-only without a `uaccess` rule.
- Resolve a dock icon through a `.desktop` id or `StartupWMClass` to `Icon`. The raw window class collapses unresolved apps onto one placeholder.
- A bounded `stat()` search beats a subprocess over a handful of candidates. Icon lookup needs existence checks only.
- Intel iGPUs often lack a GPU `hwmon` yet expose `gt_act_freq_mhz`. `find_gpu_clock_sensor` falls back to scanning `/sys/class/drm`.
- Reset the activity clock when idle management is re-enabled or its timeout drops. A stale `last_activity` fires the screensaver instantly.
- Binding a PipeWire node listener alone delivers no live parameter updates. Call `pw_node_subscribe_params()` explicitly.
- A client's callback confirming a write is not proof the state changed. Device-backed nodes need writes routed through the parent Device's Route.
