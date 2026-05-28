#!/usr/bin/env bash
# ─── FocusOS relauncher ─────────────────────────────────────────────────────
#
# Usage: focusos-relaunch.sh <binary_path>
#
# Detached helper spawned by the in-app updater right before FocusOS quits. It
# waits for the old process (and its QLockFile single-instance lock) to clear,
# then starts the new/reverted binary. On the permanent install the kiosk
# watchdog would also respawn FocusOS; this covers the dev / non-kiosk case.

set -uo pipefail

BINARY_PATH="${1:-}"
if [[ -z "$BINARY_PATH" ]]; then
    echo "usage: focusos-relaunch.sh <binary_path>" >&2
    exit 2
fi

# Wait for the running instance to exit (lock release) before respawning, up
# to ~10s. pgrep -x matches the process name exactly.
for _ in $(seq 1 50); do
    pgrep -x focusos >/dev/null 2>&1 || break
    sleep 0.2
done

# A small extra beat so the QLockFile is definitely released.
sleep 0.5

if [[ -x "$BINARY_PATH" ]]; then
    setsid "$BINARY_PATH" >/dev/null 2>&1 &
fi
exit 0
