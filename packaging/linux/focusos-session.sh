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

# Point KWin/KGlobalShortcuts at FocusOS's locked-down config so the launcher,
# overview and activity shortcuts are stripped while Alt+Tab survives. install.sh
# stages these files; fall back to the user's config if they're absent.
if [[ -d "$FOCUSOS_LIB/config" ]]; then
    export XDG_CONFIG_HOME="$FOCUSOS_LIB/config"
fi

# The kiosk watchdog is the long-lived process: it keeps FocusOS respawned for
# the life of the login session (and respawns it across an in-app update). If
# the watchdog isn't installed, fall back to running FocusOS directly.
run_session() {
    if [[ -x "$WATCHDOG" ]]; then
        exec bash "$WATCHDOG" --kiosk --binary "$FOCUSOS_BIN"
    else
        exec "$FOCUSOS_BIN"
    fi
}

case "$FOCUSOS_MODE" in
    kwin)
        if [[ -x "$WATCHDOG" ]]; then
            exec dbus-run-session -- kwin_wayland --xwayland --exit-with-session \
                bash "$WATCHDOG" --kiosk --binary "$FOCUSOS_BIN"
        else
            exec dbus-run-session -- kwin_wayland --xwayland --exit-with-session "$FOCUSOS_BIN"
        fi
        ;;
    cage)
        if [[ -x "$WATCHDOG" ]]; then
            exec dbus-run-session -- cage -- bash "$WATCHDOG" --kiosk --binary "$FOCUSOS_BIN"
        else
            exec dbus-run-session -- cage -- "$FOCUSOS_BIN"
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
