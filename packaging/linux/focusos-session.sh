#!/usr/bin/env bash
set -euo pipefail

export XDG_CURRENT_DESKTOP=FocusOS
export XDG_SESSION_DESKTOP=FocusOS
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland

FOCUSOS_BIN="${FOCUSOS_BIN:-/opt/focusos/bin/focusos}"
FOCUSOS_MODE="${FOCUSOS_MODE:-kwin}"
FOCUSOS_LIB="${FOCUSOS_LIB:-/usr/local/lib/focusos}"
WATCHDOG="${FOCUSOS_WATCHDOG:-$FOCUSOS_LIB/focusos-watchdog.sh}"

# ── FIX 1: Use XDG_CONFIG_DIRS instead of XDG_CONFIG_HOME ────────────
# This allows KWin/FocusOS to read your locked-down system configs
# as a fallback, while still being able to write their necessary caches
# and lock files to the user's writable ~/.config directory.
if [[ -d "$FOCUSOS_LIB/config" ]]; then
    export XDG_CONFIG_DIRS="$FOCUSOS_LIB/config:${XDG_CONFIG_DIRS:-/etc/xdg}"
fi
export KWIN_COMPOSE="${KWIN_COMPOSE:-O2}"

run_session() {
    if [[ -x "$WATCHDOG" ]]; then
        exec bash "$WATCHDOG" "$FOCUSOS_BIN"
    else
        exec "$FOCUSOS_BIN"
    fi
}

case "$FOCUSOS_MODE" in
    kwin)
        if [[ -x "$WATCHDOG" ]]; then
            exec dbus-run-session -- kwin_wayland --xwayland --exit-with-session bash "$WATCHDOG" --kiosk --binary "$FOCUSOS_BIN"
        else
            exec dbus-run-session -- kwin_wayland --xwayland --exit-with-session "$FOCUSOS_BIN"
        fi
        ;;
    direct)
        run_session
        ;;
    *)
        echo "Unknown FOCUSOS_MODE: $FOCUSOS_MODE" >&2
        exit 2
        ;;
esac
