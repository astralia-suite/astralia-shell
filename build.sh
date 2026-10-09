#!/usr/bin/env bash

# Build script for astralia-shell
# Use:
# <empty>: build normally
# setup [wayland|x11|all]: install dependencies for the chosen backend (default: all)
# test: build and run the unit tests
# install: build and deploy into /usr/bin/
# run: kill the running shell, install and start it
# uninstall: remove the installed files
# ASTRALIA_SHELL_BUILD_JOBS=N: parallel compile jobs (default: all cores)
# ASTRALIA_BACKENDS=wayland,x11: backend plugins to build (default: both)
# ASTRALIA_NATIVE_CPU=1: tune the x11 backend for the ThinkPad X201 (-march=westmere)

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

BIN=astralia
JOBS="${ASTRALIA_SHELL_BUILD_JOBS:-$(nproc)}"

cmd_setup() {
    local backend="${1:-all}"
    local common=(meson ninja gcc clang pkgconf cairo pango fontconfig glib2 stb resvg libjpeg-turbo sdbus-cpp libxkbcommon polkit libpipewire pipewire wireplumber bluez networkmanager upower fd brightnessctl)
    local wayland=(ffmpeg mesa wayland wayland-protocols nlohmann-json)
    local x11=(libxcb xcb-util-wm xcb-util-keysyms libxkbcommon-x11 xorg-server-xephyr)
    case "$backend" in
    wayland) sudo pacman -S --needed "${common[@]}" "${wayland[@]}" ;;
    x11) sudo pacman -S --needed "${common[@]}" "${x11[@]}" ;;
    all) sudo pacman -S --needed "${common[@]}" "${wayland[@]}" "${x11[@]}" ;;
    *) echo "unknown backend: $backend" >&2; exit 2 ;;
    esac
}

cmd_build() {
    local opts=()
    [[ -n "${ASTRALIA_BACKENDS:-}" ]] && opts+=("-Dbackends=${ASTRALIA_BACKENDS}")
    [[ "${ASTRALIA_NATIVE_CPU:-}" == 1 ]] && opts+=("-Dnative_cpu=true")
    if [[ -f build/build.ninja ]]; then
        meson configure build "${opts[@]}"
    else
        meson setup build --prefix=/usr "${opts[@]}"
    fi
    meson compile -C build -j "$JOBS"
}

cmd_test() { cmd_build; meson test -C build --print-errorlogs; }
cmd_install() { cmd_build; sudo meson install -C build --no-rebuild; }
cmd_run() { "$BIN" kill 2>/dev/null || true; cmd_install; "$BIN"; }
cmd_uninstall() { sudo ninja -C build uninstall; }

main() {
    local cmd="${1:-build}"
    case "$cmd" in
    setup | build | test | install | run | uninstall) shift || true; "cmd_$cmd" "$@" ;;
    *) echo "unknown command: $cmd" >&2; exit 2 ;;
    esac
}

main "$@"
