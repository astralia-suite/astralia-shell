# Astralia Shell

One shell, one executable, `astralia`. It detects the running session, Wayland (Hyprland, or Sway through its i3 IPC) or X11 (i3), and loads the matching backend plugin, so a machine with both sessions installed needs one install.

## Requirements

- Wayland backend: Hyprland and OpenGL ES 2.0; Sway uses the i3 compositor service and has not been run.
- X11 backend: i3 and no GL; it draws with `cairo-xcb`.
- Both: `sdbus-c++`, PipeWire, `polkit`, `fd` and `brightnessctl`.

## Installation

Install the dependencies for the backends you want, on Arch:

```bash
./build.sh setup all
```

Use `./build.sh setup wayland` or `./build.sh setup x11` to install for one backend. Then build and install:

```bash
./build.sh install
```

The executable goes to `/usr/bin/astralia` and the backends to `/usr/lib/astralia/` as `libastralia-wayland.so` and `libastralia-x11.so`.

## Build options

- `ASTRALIA_BACKENDS=wayland`, `x11` or `wayland,x11` selects which backends build; the default is both.
- `ASTRALIA_NATIVE_CPU=1` tunes the X11 backend for the ThinkPad X201 with `-march=westmere`.
- `ASTRALIA_SHELL_BUILD_JOBS=N` sets the number of compile jobs.
- `./build.sh test` builds and runs the unit tests and the backend smoke test.

## Running the shell

Regular mode, in the background:

```bash
astralia
```

Debug mode, in the foreground with logs on the terminal:

```bash
astralia debug
```

Logs also go to `~/.local/state/astralia/astralia.log`. Send a verb to the running shell with `astralia <verb>`, for example `astralia launcher`, and list them with `astralia help`. `astralia kill` quits it.

## Backend selection

The session is detected from `HYPRLAND_INSTANCE_SIGNATURE` or `SWAYSOCK` (Wayland), then `I3SOCK` (X11), then `XDG_SESSION_TYPE`. Set `ASTRALIA_BACKEND=wayland` or `ASTRALIA_BACKEND=x11` to override it. `ASTRALIA_BACKEND_DIR` makes the loader look in another directory first, which lets a build tree run without installing.

## Module availability

A backend registers a module or verb only for the capabilities it reports. `astralia modules` prints these tables, and `astralia status` shows the running backend, its capabilities and the open modules.

| Module or feature | Wayland | X11 |
| --- | --- | --- |
| `bar` | yes | yes |
| `launcher` | yes | yes |
| `logout` | yes | yes |
| `overview` | yes | yes |
| `settings` | yes | yes |
| `notification` | yes | yes |
| `osd` | yes | yes |
| `polkit` | yes | yes |
| `wallpaper` | yes | yes |
| `lock` | yes | yes |
| `idle` | yes | no |
| `dashboard` | yes | no |
| `rain` | yes | no |
| `visualizer` | yes | no |
| `animated_wallpaper` | yes | no |
| `resource_panel` | yes | no |
| `animations` | yes | no |

| Verb | Description | Wayland | X11 |
| --- | --- | --- | --- |
| `launcher` | toggle the launcher, searching from $HOME | yes | yes |
| `launcher global` | toggle the launcher, searching from / | yes | yes |
| `logout` | toggle the logout overlay | yes | yes |
| `overview` | toggle the overview | yes | yes |
| `settings` | toggle the settings overlay | yes | yes |
| `dashboard` | toggle the dashboard window | yes | no |
| `rain` | toggle the rain overlay | yes | no |
| `visualizer` | toggle the audio visualizer overlay | yes | no |
| `lock` | lock the session | yes | yes |
| `panel-tray` | toggle the tray panel | yes | yes |
| `panel-resource` | toggle the resource panel | yes | no |
| `panel-network` | toggle the network panel | yes | yes |
| `panel-bluetooth` | toggle the bluetooth panel | yes | yes |
| `panel-volume` | toggle the volume panel | yes | yes |
| `panel-battery` | toggle the battery panel | yes | yes |
| `panel-media` | toggle the media panel | yes | yes |
| `panel-brightness` | toggle the brightness panel | yes | yes |
| `panel-clock` | toggle the clock panel | yes | yes |

## Configuration

The config is `~/.config/astralia/config.json`. When it does not exist, the old X11 files `~/.config/astralia-shell/settings.conf` and `wallpaper.conf` are imported once.
