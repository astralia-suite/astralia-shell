#!/usr/bin/env bash

# Build script for astralia-shell
# Use:
# <empty>: build and run the unit tests
# setup [wayland|x11|all]: install dependencies for the chosen backend (default: all)
# build: build only
# install: build and install system-wide
# run: kill the running shell, install and start it
# uninstall: remove the system-wide install
# ASTRALIA_SHELL_BUILD_JOBS=N: parallel compile jobs (default: all cores)
# ASTRALIA_BACKENDS=wayland,x11: backend plugins to build (default: both)
# ASTRALIA_NATIVE_CPU=1: tune the x11 backend for the ThinkPad X201 (-march=westmere)

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

BIN=astralia
JOBS="${ASTRALIA_SHELL_BUILD_JOBS:-$(nproc)}"

cmd_setup() {
    local backend="${1:-all}"
    local common=(meson ninja gcc clang pkgconf cairo pango fontconfig glib2 stb resvg libjpeg-turbo sdbus-cpp libxkbcommon polkit libpipewire pipewire wireplumber bluez networkmanager upower fd brightnessctl pam)
    local wayland=(ffmpeg mesa libglvnd libdrm wayland wayland-protocols nlohmann-json)
    local x11=(libxcb xcb-util-wm xcb-util-keysyms xcb-util-cursor libxkbcommon-x11 xorg-server-xephyr)
    case "$backend" in
    wayland) sudo pacman -Syu --needed "${common[@]}" "${wayland[@]}" ;;
    x11) sudo pacman -Syu --needed "${common[@]}" "${x11[@]}" ;;
    all) sudo pacman -Syu --needed "${common[@]}" "${wayland[@]}" "${x11[@]}" ;;
    *) echo "unknown backend: $backend" >&2; exit 2 ;;
    esac
}

cmd_build() {
    local opts=()
    [[ -n "${ASTRALIA_BACKENDS:-}" ]] && opts+=("-Dbackends=${ASTRALIA_BACKENDS}")
    [[ "${ASTRALIA_NATIVE_CPU:-}" == 1 ]] && opts+=("-Dnative_cpu=true")
    if [[ -f build/build.ninja ]]; then
        if ((${#opts[@]})); then meson configure build "${opts[@]}"; fi
    else
        meson setup build "${opts[@]}"
    fi
    meson compile -C build -j "$JOBS"
}

cmd_default() { cmd_build; meson test -C build --print-errorlogs; }
cmd_install() { cmd_build; sudo meson install -C build --no-rebuild; }
cmd_run() { "$BIN" kill 2>/dev/null || true; cmd_install; "$BIN"; }
cmd_uninstall() { sudo ninja -C build uninstall; }

main() {
    local cmd="${1:-default}"
    case "$cmd" in
    default | setup | build | install | run | uninstall) shift || true; "cmd_$cmd" "$@" ;;
    *) echo "unknown command: $cmd" >&2; exit 2 ;;
    esac
}

main "$@"
