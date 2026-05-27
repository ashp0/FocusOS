#!/usr/bin/env bash
set -euo pipefail

export XDG_CURRENT_DESKTOP=FocusOS
export XDG_SESSION_DESKTOP=FocusOS
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland

FOCUSOS_BIN="${FOCUSOS_BIN:-/opt/focusos/bin/focusos}"
FOCUSOS_MODE="${FOCUSOS_MODE:-kwin}"

case "$FOCUSOS_MODE" in
    kwin)
        exec dbus-run-session -- kwin_wayland --xwayland --exit-with-session "$FOCUSOS_BIN"
        ;;
    cage)
        exec dbus-run-session -- cage -- "$FOCUSOS_BIN"
        ;;
    direct)
        exec "$FOCUSOS_BIN"
        ;;
    *)
        echo "Unknown FOCUSOS_MODE: $FOCUSOS_MODE" >&2
        exit 2
        ;;
esac
