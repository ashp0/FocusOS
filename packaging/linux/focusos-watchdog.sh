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
#     install. Brings `focusos` up as the session shell and keeps it alive for
#     the whole login session, idle or armed. This is strict kiosk posture:
#     quitting or crashing the shell never drops through to a desktop or session
#     selector.
#
# Single-instance via flock on ~/.focusos/watchdog.lock — a second invocation
# (e.g. FocusOS arming the watchdog again on resume) is a no-op.

set -uo pipefail

FOCUS_DIR="${FOCUSOS_DIR:-$HOME/.focusos}"
ACTIVE_FILE="$FOCUS_DIR/active.json"
# Cross-process "this exit is intentional, stop respawning" marker. FocusOS drops
# it on sign out / restart / shut down (LinuxBackend::writeSessionExitMarker).
# When present we exit the supervise loop in BOTH modes — in kiosk mode that ends
# kwin's --exit-with-session command, handing control back to focusos-session.sh
# which then ends the login session (greeter) instead of respawning the shell.
EXIT_MARKER="$FOCUS_DIR/session-exit"
BINARY_FILE="$FOCUS_DIR/watchdog-binary"
LOCK_FILE="$FOCUS_DIR/watchdog.lock"
LOG_FILE="$FOCUS_DIR/watchdog.log"
CRASHLOOP_FILE="$FOCUS_DIR/crash-loop"

# Kiosk crash-loop guard: if focusos dies and is respawned this many times within
# the window, stop respawning and exit. In the permanent install this watchdog is
# kwin's --exit-with-session command, so exiting ends the kwin session — which
# hands control back to focusos-session.sh, which falls back to the stock desktop
# instead of looping a broken build forever. A respawn that survives at least
# KIOSK_MIN_HEALTHY seconds is treated as healthy and clears the window.
KIOSK_CRASH_LIMIT="${FOCUSOS_KIOSK_CRASH_LIMIT:-4}"
KIOSK_CRASH_WINDOW="${FOCUSOS_KIOSK_CRASH_WINDOW:-90}"
KIOSK_MIN_HEALTHY="${FOCUSOS_KIOSK_MIN_HEALTHY:-20}"

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

# Sliding window of recent respawn timestamps (kiosk crash-loop detection).
declare -a SPAWN_TIMES=()

record_spawn_and_check_loop() {
    local now pruned t
    now="$(date +%s)"
    pruned=()
    for t in "${SPAWN_TIMES[@]:-}"; do
        [[ "$t" =~ ^[0-9]+$ ]] || continue
        (( now - t < KIOSK_CRASH_WINDOW )) && pruned+=("$t")
    done
    pruned+=("$now")
    SPAWN_TIMES=("${pruned[@]}")
    (( ${#SPAWN_TIMES[@]} >= KIOSK_CRASH_LIMIT ))
}

while true; do
    # Intentional exit (sign out / restart / shut down): stop respawning and let
    # the loop end. Checked first so it wins over a respawn in either mode.
    if [[ -e "$EXIT_MARKER" ]]; then
        log "session-exit marker present — watchdog exiting (mode=$MODE)"
        break
    fi
    if [[ "$MODE" == "kiosk" ]]; then
        if ! focus_running; then
            if record_spawn_and_check_loop; then
                log "kiosk crash loop: ${#SPAWN_TIMES[@]} respawns in ${KIOSK_CRASH_WINDOW}s — giving up so the session can fall back to the stock desktop"
                printf '%s crash-loop give-up after %s respawns\n' \
                    "$(date -u +%FT%TZ)" "${#SPAWN_TIMES[@]}" > "$CRASHLOOP_FILE" 2>/dev/null || true
                exit 0
            fi
            spawn_focus
            # If this respawn survives the grace period it counts as healthy:
            # clear the window so a single later crash doesn't trip the loop.
            sleep "$KIOSK_MIN_HEALTHY"
            if focus_running; then
                SPAWN_TIMES=()
            fi
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
