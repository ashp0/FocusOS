#!/usr/bin/env bash
# ─── FocusOS revert ─────────────────────────────────────────────────────────
#
# Usage: focusos-revert.sh <binary_path> <snapshot_path>
#
# Restores the snapshotted previous binary over the current one. Used both for
# a user-initiated revert and the automatic crash-loop revert during the
# 30-minute probation window.

set -uo pipefail

BINARY_PATH="${1:-}"
SNAPSHOT_PATH="${2:-}"

if [[ -z "$BINARY_PATH" || -z "$SNAPSHOT_PATH" ]]; then
    echo "usage: focusos-revert.sh <binary_path> <snapshot_path>" >&2
    exit 2
fi
if [[ ! -f "$SNAPSHOT_PATH" ]]; then
    echo "ERROR: snapshot not found: $SNAPSHOT_PATH" >&2
    exit 3
fi

echo "Restoring $SNAPSHOT_PATH → $BINARY_PATH"
cp -a "$SNAPSHOT_PATH" "$BINARY_PATH" || { echo "ERROR: copy failed" >&2; exit 4; }
chmod +x "$BINARY_PATH" || true
echo "── revert complete ───────────────────────────────────────"
exit 0
