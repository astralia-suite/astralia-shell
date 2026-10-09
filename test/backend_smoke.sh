#!/usr/bin/env bash

# Starts the x11 backend nested in Xephyr when one is available, then asks it to quit over IPC.
# The wayland backend needs a compositor, so it is only checked to load and to fail cleanly without a display.
# Usage: backend_smoke.sh <astralia executable>

set -euo pipefail

exe="$1"
host_display="${DISPLAY:-}"
tmp="$(mktemp -d)"
xephyr_pid=""
cleanup() {
    [ -n "$xephyr_pid" ] && kill "$xephyr_pid" 2>/dev/null || true
    rm -rf "$tmp"
}
trap cleanup EXIT

unset WAYLAND_DISPLAY DISPLAY HYPRLAND_INSTANCE_SIGNATURE I3SOCK XDG_SESSION_TYPE
export XDG_RUNTIME_DIR="$tmp" XDG_STATE_HOME="$tmp/state" ASTRALIA_BACKEND_DIR="$(dirname "$exe")"

run_backend() {
    backend="$1"
    ASTRALIA_BACKEND="$backend" "$exe" debug &
    pid=$!
    for _ in $(seq 100); do
        [ -S "$XDG_RUNTIME_DIR"/astralia*.sock ] 2>/dev/null && break
        sleep 0.05
    done
    ls "$XDG_RUNTIME_DIR"/astralia*.sock >/dev/null 2>&1 || { echo "$backend: no socket" >&2; kill "$pid" 2>/dev/null || true; exit 1; }
    "$exe" help | grep -q "kill" || { echo "$backend: help lacks kill" >&2; kill "$pid" 2>/dev/null || true; exit 1; }
    "$exe" kill
    wait "$pid" || { echo "$backend: exited with $?" >&2; exit 1; }
    echo "$backend: started and stopped"
}

set +e
ASTRALIA_BACKEND=wayland "$exe" debug > "$tmp/wayland.log" 2>&1
status=$?
set -e
if [ "$status" -eq 0 ] || ! grep -q "failed to connect to Wayland display" "$tmp/wayland.log"; then
    echo "wayland: expected a clean failure without a display (exit $status)" >&2
    cat "$tmp/wayland.log" >&2
    exit 1
fi
echo "wayland: loaded and failed cleanly without a display"

if [ -n "$host_display" ] && command -v Xephyr >/dev/null; then
    DISPLAY="$host_display" Xephyr :97 -screen 800x600 -ac >/dev/null 2>&1 &
    xephyr_pid=$!
    sleep 1
    export DISPLAY=:97
    run_backend x11
else
    echo "x11: skipped, no X server to nest in"
fi
