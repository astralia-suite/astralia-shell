#!/usr/bin/env bash

# Build script for astralia-shell
# Use:
# <empty>: run the tests with and without default features
# setup: install build and runtime dependencies
# build: release build only
# install: build and install system-wide (DESTDIR=dir stages into dir without sudo)
# run: kill the running shell, install and start it

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

BIN=astralia
DESTDIR="${DESTDIR:-}"
DATA=/usr/share/astralia-shell
ASSETS=(
    fonts/tabler-icons.ttf
    fonts/ComicShannsMono-Regular.otf
    fonts/YujiMai.ttf
    stellar-restoration.svg
    stiletto.svg
    wallpaper/default.jpg
    bamboo/bamboo.png
    logout/logo.png
    polkit/electro.png
    pam/astralia-shell
    constellation/C1.png
    constellation/C2.png
    constellation/C3.png
    constellation/C4.png
    constellation/C5.png
    constellation/C6.png
)

as_root() { if [[ -z "$DESTDIR" ]]; then sudo "$@"; else "$@"; fi; }

cmd_setup() {
    sudo pacman -Syu --needed rust clang pkgconf cairo pango libxkbcommon mesa libglvnd wayland wayland-protocols \
        pipewire wireplumber pam ffmpeg polkit bluez networkmanager upower brightnessctl fd
}

cmd_build() { cargo build --release; }

cmd_default() { cargo test; cargo test --no-default-features; }

cmd_install() {
    cmd_build
    as_root install -Dm755 "target/release/$BIN" "$DESTDIR/usr/bin/$BIN"
    local asset
    for asset in "${ASSETS[@]}"; do
        as_root install -Dm644 "assets/$asset" "$DESTDIR$DATA/$asset"
    done
    sed "s|@icon@|$DATA/bamboo/bamboo.png|" assets/bamboo/astralia-shell-bamboo.desktop.in |
        as_root install -Dm644 /dev/stdin "$DESTDIR/usr/share/applications/astralia-shell-bamboo.desktop"
}

cmd_run() { "$BIN" kill 2>/dev/null || true; cmd_install; "$BIN"; }

main() {
    local cmd="${1:-default}"
    case "$cmd" in
    default | setup | build | install | run) shift || true; "cmd_$cmd" "$@" ;;
    *) echo "unknown command: $cmd" >&2; exit 2 ;;
    esac
}

main "$@"
