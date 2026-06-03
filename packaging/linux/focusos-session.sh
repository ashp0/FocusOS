#!/usr/bin/env bash
# ─── FocusOS session entry point ────────────────────────────────────────────
#
# Launched by SDDM via focusos.desktop (Exec=/usr/local/bin/focusos-session).
# Brings up the bare kwin_wayland session and runs FocusOS as the shell.
#
# SELF-HEALING (the headline behaviour): a bad FocusOS build must never brick the
# machine. The MacBook brick happened because the session looped focusos forever
# with no escape. This wrapper now detects a crash loop and falls back to the
# stock Plasma session instead, so the user is never stranded on a black screen:
#
#   * Per-login crash counter — every session start is timestamped in
#     $XDG_RUNTIME_DIR (survives the autologin re-login loop, wiped on reboot).
#     If FocusOS/kwin dies and we re-enter too many times within a short window,
#     we exec the stock session (startplasma-wayland) instead.
#   * Same-session fallback — if kwin (and the kiosk watchdog inside it) exits
#     unexpectedly, we fall back here too rather than dropping to a blank greeter.
#   * Manual override — `~/.focusos/boot-to-plasma` (or FOCUSOS_FORCE_PLASMA=1)
#     forces the stock session on the next login. Recovery writes this file.
#
# NOTE: `set -e` is deliberately OFF — a failing kwin must NOT abort the script
# before it can reach the fallback.

set -uo pipefail

export XDG_CURRENT_DESKTOP=FocusOS
export XDG_SESSION_DESKTOP=FocusOS
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland

FOCUSOS_BIN="${FOCUSOS_BIN:-/opt/focusos/bin/focusos}"
FOCUSOS_MODE="${FOCUSOS_MODE:-kwin}"
FOCUSOS_LIB="${FOCUSOS_LIB:-/usr/local/lib/focusos}"
WATCHDOG="${FOCUSOS_WATCHDOG:-$FOCUSOS_LIB/focusos-watchdog.sh}"

# ── Crash-loop fallback configuration ───────────────────────────────────────
FOCUSOS_CRASH_LIMIT="${FOCUSOS_CRASH_LIMIT:-3}"      # starts within the window…
FOCUSOS_CRASH_WINDOW="${FOCUSOS_CRASH_WINDOW:-120}"  # …before we fall back (sec)
FOCUS_DIR="${FOCUSOS_DIR:-$HOME/.focusos}"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-$FOCUS_DIR}"
ATTEMPT_LOG="$RUNTIME_DIR/focusos-boot-attempts"
mkdir -p "$FOCUS_DIR" 2>/dev/null || true

# ── FIX 1: Use XDG_CONFIG_DIRS instead of XDG_CONFIG_HOME ────────────
# This allows KWin/FocusOS to read your locked-down system configs
# as a fallback, while still being able to write their necessary caches
# and lock files to the user's writable ~/.config directory.
if [[ -d "$FOCUSOS_LIB/config" ]]; then
    export XDG_CONFIG_DIRS="$FOCUSOS_LIB/config:${XDG_CONFIG_DIRS:-/etc/xdg}"
fi
# NOTE: KWIN_COMPOSE (an X11-era backend override) is intentionally NOT forced
# here. On KWin 6 / Wayland it is at best ignored and at worst pins a GL path the
# GPU stack rejects (a plausible cause of the silent abort on Asahi/Apple
# silicon). Let KWin auto-select; an operator can still export it to override.

# Resolve the stock session we fall back to when FocusOS won't run. Prefer an
# install-baked value (FOCUSOS_FALLBACK_SESSION, written by install.sh), then a
# live PATH lookup of the standard Plasma launchers.
find_fallback() {
    if [[ -n "${FOCUSOS_FALLBACK_SESSION:-}" ]] \
        && command -v "${FOCUSOS_FALLBACK_SESSION%% *}" >/dev/null 2>&1; then
        printf '%s' "$FOCUSOS_FALLBACK_SESSION"
        return 0
    fi
    local c
    for c in startplasma-wayland startplasma-x11 plasma-dbus-run-session-if-needed; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return 0
        fi
    done
    return 1
}

# Hand the session over to the stock desktop. exec so the fallback fully replaces
# us (and so logind/SDDM treat it as the live session).
exec_fallback() {
    local reason="$1"
    printf '%s FocusOS session falling back to stock desktop: %s\n' \
        "$(date -u +%FT%TZ)" "$reason" >&2
    : > "$FOCUS_DIR/fell-back-to-plasma" 2>/dev/null || true
    local fb
    if fb="$(find_fallback)"; then
        echo "FocusOS: launching stock session → $fb" >&2
        # shellcheck disable=SC2086
        exec $fb
    fi
    # No stock session available — exit non-zero so SDDM returns to the greeter
    # rather than leaving a black screen.
    echo "FocusOS: no stock session found to fall back to; ending session." >&2
    exit 1
}

# ── Manual override: boot straight to Plasma this time ──────────────────────
if [[ -n "${FOCUSOS_FORCE_PLASMA:-}" || -e "$FOCUS_DIR/boot-to-plasma" ]]; then
    rm -f "$FOCUS_DIR/boot-to-plasma" 2>/dev/null || true
    exec_fallback "boot-to-plasma override requested"
fi

# ── Crash-loop guard: prune stale timestamps, record this start, count ──────
now="$(date +%s)"
tmp="${ATTEMPT_LOG}.$$"
if [[ -f "$ATTEMPT_LOG" ]]; then
    while read -r t; do
        [[ "$t" =~ ^[0-9]+$ ]] || continue
        (( now - t < FOCUSOS_CRASH_WINDOW )) && printf '%s\n' "$t"
    done < "$ATTEMPT_LOG" > "$tmp" 2>/dev/null || true
fi
printf '%s\n' "$now" >> "$tmp" 2>/dev/null || true
mv -f "$tmp" "$ATTEMPT_LOG" 2>/dev/null || true
attempts="$(wc -l < "$ATTEMPT_LOG" 2>/dev/null | tr -d ' ')"
if [[ "${attempts:-0}" -ge "$FOCUSOS_CRASH_LIMIT" ]]; then
    # Reset so the fallback session start isn't itself counted next login.
    : > "$ATTEMPT_LOG" 2>/dev/null || true
    exec_fallback "FocusOS crash-looped ${attempts}× within ${FOCUSOS_CRASH_WINDOW}s"
fi

# ── Only request Xwayland when it's actually installed ──────────────────────
# kwin_wayland --xwayland aborts immediately if the Xwayland binary is missing
# (common on minimal Asahi installs), which silently kills the whole session.
KWIN_ARGS=()
if command -v Xwayland >/dev/null 2>&1; then
    KWIN_ARGS+=(--xwayland)
fi

session_command() {
    if [[ -x "$WATCHDOG" ]]; then
        printf '%s' "bash $WATCHDOG --kiosk --binary $FOCUSOS_BIN"
    else
        printf '%s' "$FOCUSOS_BIN"
    fi
}

case "$FOCUSOS_MODE" in
    kwin)
        # kwin_wayland's --exit-with-session takes a SINGLE value that it
        # word-splits into command + args. Passing the watchdog + its flags as
        # separate argv words made kwin reject `--kiosk`/`--binary` as its own
        # unknown options and abort instantly — quote the whole command into one
        # --exit-with-session value instead.
        #
        # We intentionally do NOT exec kwin: when it (or the kiosk watchdog it
        # runs) exits unexpectedly we want control back here so exec_fallback can
        # hand the user to the stock desktop instead of a blank screen.
        dbus-run-session -- kwin_wayland "${KWIN_ARGS[@]}" \
            --exit-with-session="$(session_command)"
        rc=$?
        exec_fallback "kwin/session exited (rc=$rc)"
        ;;
    direct)
        if [[ -x "$WATCHDOG" ]]; then
            bash "$WATCHDOG" --kiosk --binary "$FOCUSOS_BIN"
        else
            "$FOCUSOS_BIN"
        fi
        exec_fallback "FocusOS session exited"
        ;;
    *)
        echo "Unknown FOCUSOS_MODE: $FOCUSOS_MODE" >&2
        exit 2
        ;;
esac
