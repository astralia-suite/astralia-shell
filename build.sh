#!/usr/bin/env bash

# Build script for astralia-shell
# Use:
# <empty>: release build
# test: run unit tests
# setup: install build and runtime dependencies
# install: build and install system-wide (DESTDIR=dir stages into dir without sudo)
# uninstall: stop the running shell and remove everything install placed (honours DESTDIR)
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
    sudo pacman -Syu --needed rust clang pkgconf cairo pango libxkbcommon mesa libglvnd wayland wayland-protocols pipewire wireplumber pam polkit bluez networkmanager upower brightnessctl fd
}
cmd_default() { cargo build --release; }
cmd_test() { cargo test; }
cmd_restart() { "$BIN" kill 2>/dev/null || true; "$BIN"; }
cmd_install() {
    cmd_default
    as_root install -Dm755 "target/release/$BIN" "$DESTDIR/usr/bin/$BIN"
    local asset
    for asset in "${ASSETS[@]}"; do
        as_root install -Dm644 "assets/$asset" "$DESTDIR$DATA/$asset"
    done
    sed "s|@icon@|$DATA/bamboo/bamboo.png|" assets/bamboo/astralia-shell-bamboo.desktop.in |
        as_root install -Dm644 /dev/stdin "$DESTDIR/usr/share/applications/astralia-shell-bamboo.desktop"
}
cmd_uninstall() {
    [[ -n "$DESTDIR" ]] || "$BIN" kill 2>/dev/null || true
    as_root rm -rf "$DESTDIR/usr/bin/$BIN" "$DESTDIR$DATA" "$DESTDIR/usr/share/applications/astralia-shell-bamboo.desktop"
}
cmd_run() { cmd_install; cmd_restart; }
main() {
    local cmd="${1:-default}"
    case "$cmd" in
    default | test | setup | install | uninstall | run) shift || true; "cmd_$cmd" "$@" ;;
    *) echo "unknown command: $cmd" >&2; exit 2 ;;
    esac
}

main "$@"
