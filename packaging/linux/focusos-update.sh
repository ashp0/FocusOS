#!/usr/bin/env bash
# ─── FocusOS in-app updater ─────────────────────────────────────────────────
#
# Usage: focusos-update.sh <repo_dir> <binary_path>
#
# Snapshots the current binary, pulls the latest source, and rebuilds in place
# — no sudo required because everything lives in the user's home checkout. The
# caller (Updater) reads ~/.focusos/pending-snapshot afterwards and arms the
# 30-minute probation window before relaunching.
#
# Output is streamed to stdout (merged) so the UI can show build progress.

set -uo pipefail

REPO_DIR="${1:-}"
BINARY_PATH="${2:-}"
FOCUS_DIR="${FOCUSOS_DIR:-$HOME/.focusos}"
SNAPSHOT_ROOT="$FOCUS_DIR/snapshots"
SNAPSHOT_POINTER="$FOCUS_DIR/pending-snapshot"

if [[ -z "$REPO_DIR" || -z "$BINARY_PATH" ]]; then
    echo "usage: focusos-update.sh <repo_dir> <binary_path>" >&2
    exit 2
fi
if [[ ! -d "$REPO_DIR/.git" ]]; then
    echo "ERROR: $REPO_DIR is not a git checkout — cannot update in place." >&2
    exit 3
fi

mkdir -p "$SNAPSHOT_ROOT"

# 1. Snapshot the current binary so a bad build can be reverted.
TS="$(date -u +%Y%m%dT%H%M%SZ)"
SNAPSHOT_DIR="$SNAPSHOT_ROOT/snap-$TS"
mkdir -p "$SNAPSHOT_DIR"
if [[ -f "$BINARY_PATH" ]]; then
    cp -a "$BINARY_PATH" "$SNAPSHOT_DIR/$(basename "$BINARY_PATH")"
    printf '%s\n' "$SNAPSHOT_DIR/$(basename "$BINARY_PATH")" > "$SNAPSHOT_POINTER"
    echo "Snapshotted current build → $SNAPSHOT_DIR"
else
    echo "WARNING: current binary $BINARY_PATH not found; no snapshot taken." >&2
    : > "$SNAPSHOT_POINTER"
fi

cd "$REPO_DIR"

# 2. Pull the latest source. --ff-only keeps the update predictable; a diverged
#    local tree aborts rather than silently merging.
echo "── git pull ──────────────────────────────────────────────"
git fetch --all --prune || { echo "ERROR: git fetch failed" >&2; exit 4; }
git pull --ff-only || { echo "ERROR: git pull --ff-only failed (local changes?)" >&2; exit 4; }

# 3. Rebuild in place. Derive the build dir from the running binary's location
#    so we rebuild exactly what is being run.
BUILD_DIR="$(dirname "$BINARY_PATH")"
echo "── build (${BUILD_DIR}) ──────────────────────────────────"
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    GENERATOR="Unix Makefiles"
    command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
    cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release \
        || { echo "ERROR: cmake configure failed" >&2; exit 5; }
fi
cmake --build "$BUILD_DIR" --target focusos \
    || { echo "ERROR: build failed" >&2; exit 5; }

echo "── update complete ───────────────────────────────────────"
exit 0
