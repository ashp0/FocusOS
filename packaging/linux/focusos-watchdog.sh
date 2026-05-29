#!/usr/bin/env bash
# ─── FocusOS respawn watchdog ───────────────────────────────────────────────
#
# A headless (no-window) supervisor that makes an engaged routine survive a
# kill / crash of the main FocusOS process. It NEVER opens a window, so it does
# not show up as a second app in the window manager.
#
# Two modes:
#
#   (default) routine mode — spawned by FocusOS itself when a routine engages.
#     Stays alive only while the session checkpoint (~/.focusos/active.json)
#     exists. While it does, it respawns `focusos` whenever it disappears. When
#     the checkpoint is removed (a legitimate end: expiry or TOTP-unlock past
#     the min-time floor) the watchdog exits on its own.
#
#   --kiosk — used as KWin's `--exit-with-session` command in the permanent
#     install. Keeps `focusos` alive unconditionally for the life of the
#     login session, so the box always boots back into FocusOS.
#
# Single-instance via flock on ~/.focusos/watchdog.lock — a second invocation
# (e.g. FocusOS arming the watchdog again on resume) is a no-op.

set -uo pipefail

FOCUS_DIR="${FOCUSOS_DIR:-$HOME/.focusos}"
ACTIVE_FILE="$FOCUS_DIR/active.json"
BINARY_FILE="$FOCUS_DIR/watchdog-binary"
LOCK_FILE="$FOCUS_DIR/watchdog.lock"
LOG_FILE="$FOCUS_DIR/watchdog.log"

mkdir -p "$FOCUS_DIR"

MODE="routine"
BIN_OVERRIDE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --kiosk) MODE="kiosk"; shift ;;
        --binary) BIN_OVERRIDE="${2:-}"; shift 2 ;;
        *) shift ;;
    esac
done

# Single-instance guard. FD 9 stays open for the life of the process; the lock
# releases automatically when we exit.
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
    exit 0
fi

log() {
    printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" >> "$LOG_FILE" 2>/dev/null || true
}

resolve_binary() {
    if [[ -n "$BIN_OVERRIDE" ]]; then
        printf '%s' "$BIN_OVERRIDE"
        return
    fi
    if [[ -s "$BINARY_FILE" ]]; then
        head -n1 "$BINARY_FILE"
        return
    fi
    command -v focusos 2>/dev/null || printf '%s' "/opt/focusos/bin/focusos"
}

focus_running() {
    pgrep -x focusos >/dev/null 2>&1
}

spawn_focus() {
    local bin
    bin="$(resolve_binary)"
    if [[ -x "$bin" ]]; then
        log "respawning $bin"
        setsid "$bin" >> "$LOG_FILE" 2>&1 &
    else
        log "binary not executable: $bin"
    fi
}

log "watchdog start mode=$MODE pid=$$"

while true; do
    if [[ "$MODE" == "kiosk" ]]; then
        focus_running || spawn_focus
    else
        if [[ ! -f "$ACTIVE_FILE" ]]; then
            log "checkpoint gone — watchdog exiting"
            break
        fi
        if ! focus_running; then
            # Grace beat: a clean shutdown removes active.json slightly before
            # the process exits. Re-check both before respawning so we don't
            # race a legitimate end.
            sleep 1
            if [[ -f "$ACTIVE_FILE" ]] && ! focus_running; then
                spawn_focus
            fi
        fi
    fi
    sleep 2
done
