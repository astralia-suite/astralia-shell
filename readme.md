# Astralia Shell

A colourful wallaper-themed desktop shell

Supports Hyprland, ShojiWM and i3.

## Requirements

- Wayland: 
    - Hyprland or ShojiWM, and OpenGL ES 2.0.
    - ShojiWM needs a config that serves the workspace IPC (`workspaces.*` and `windows.*` methods) on `$XDG_RUNTIME_DIR/shojiwm-$WAYLAND_DISPLAY.sock`.
- X11: i3.
- Both: `sdbus-c++`, PipeWire, `polkit`, `fd` and `brightnessctl`.

## Installation

On Arch, install the dependencies, then build and install:

```bash
./build.sh setup all
./build.sh install
```

Use `./build.sh setup wayland` or `./build.sh setup x11` to install for one backend.

### Build options

- `ASTRALIA_BACKENDS=wayland`, `x11` or `wayland,x11` selects which backends build; the default is both.

## Usage

Start the shell in the background:

```bash
astralia
```

Run it in the foreground with logs on the terminal:

```bash
astralia debug
```

Control the running shell with `astralia <verb>`, for example `astralia launcher`, `astralia settings`, `astralia overview`, `astralia wallpaper` or `astralia lock`. List all verbs with `astralia help`, show what the current session supports with `astralia modules` and `astralia status`, and quit with `astralia kill`.

Bind verbs to keys in your compositor or window manager. For example, in i3:

```
bindsym $mod+d exec astralia launcher
```

Logs go to `~/.local/state/astralia/astralia.log`.

The logout screen and the idle screensaver draw bundled logos by default. Set `general.logoutLogoPath` and `displays.idle.logoPath` to an image path in the config to use your own, and leave them empty to keep the bundled ones.

## Configuration

The config is `~/.config/astralia/config.json`. When it does not exist, the defaults apply and the file is written on the first change. Most options can also be changed from `astralia settings`.

The session is detected automatically. Set `ASTRALIA_BACKEND=wayland` or `ASTRALIA_BACKEND=x11` to force one.

## Development

```bash
./build.sh test
```

This builds and runs the unit tests and a backend smoke test.

## Credits

Astralia-shell can never be built without the inspiration of existing major works, and i am grateful to all of them.

**The foundations**
- `noctalia` (MIT): The only public desktop shell with C++ and opengl es 2.0. I had learned a lot about communication with wayland through this shell.
- `caelestia` (GPL-3.0): One of the most beautiful desktop shell to ever released. The lock screen design mostly adopted from here.
- `end4pc` (GPL-3.0): Settings panel was inspired by this project's settings panel.
- `quickshell-overview` (GPL): Where the overview's initial design was adopted from.
- `sekaipop` (GPL-3.0): A relatively new shell with gorgeous eyecandy and animations.
- `GLava` (GPL-3.0): The audio visualizer transforms and FFT are adapted from it.
- `pocket-dots` (GPL-3.0): The ink spread wallpaper transition is ported from it.

## License

Astralia Shell is licensed under GPL-3.0, see `LICENSE`. Bundled third-party notices are in `THIRD_PARTY_LICENSES` and `assets/shaders/NOTICE`.
