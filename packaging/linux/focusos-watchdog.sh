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
#     install. Brings `focusos` up as the session shell and keeps it alive
#     across crashes WHILE A ROUTINE IS ARMED (~/.focusos/active.json). If
#     FocusOS exits while idle (no routine), the watchdog releases the session
#     so the user drops back to the login manager and can pick Plasma — the
#     deliberate "fall back to Plasma" recovery path. During boot/startup (before
#     FocusOS has ever come up) it keeps relaunching so a crash loop doesn't
#     strand the user on a black screen.
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

# Tracks whether we've ever confirmed FocusOS actually running. Until we have,
# a "not running" reading means it's still starting (or crash-looping at boot),
# so we keep (re)launching. Once it's been up, a later disappearance while idle
# is treated as a deliberate exit and we release the session.
SEEN_RUNNING=0

while true; do
    if [[ "$MODE" == "kiosk" ]]; then
        if focus_running; then
            SEEN_RUNNING=1
        elif [[ "$SEEN_RUNNING" -eq 0 ]]; then
            # Initial boot / startup window — bring FocusOS up and give it a
            # moment to appear before judging it again.
            spawn_focus
            sleep 3
        elif [[ -f "$ACTIVE_FILE" ]]; then
            # A routine is armed — protect it across a crash/kill. Grace beat so
            # we don't race a legitimate end that removes the checkpoint just
            # before the process exits.
            sleep 1
            if [[ -f "$ACTIVE_FILE" ]] && ! focus_running; then
                spawn_focus
            fi
        else
            # FocusOS was up, is now gone, and no routine is armed: treat it as a
            # deliberate exit and release the session so the login manager (and
            # Plasma) is reachable.
            log "kiosk: idle exit — releasing session to login manager"
            break
        fi
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
