#!/usr/bin/env bash
# Serve ~/.focusos/blocker/dist over http://127.0.0.1:<port> so Brave/Chromium
# can force-install and auto-update the FocusOS Blocker extension.
#
# Why this exists: modern Chromium/Brave refuse `file://` update URLs for
# force-installed extensions — the policy is read but the crx is never fetched,
# so the extension never loads (and the native host never spawns). A localhost
# HTTP origin is treated as a secure context and works. Idempotent; safe to call
# on every `git pull`.
set -euo pipefail

PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
DIST="$HOME/.focusos/blocker/dist"
mkdir -p "$DIST"

PY="$(command -v python3 || true)"
if [ -z "$PY" ]; then
  echo "python3 is required to serve the extension locally." >&2
  exit 1
fi

# Already serving? Nothing to do.
if command -v curl >/dev/null 2>&1 && curl -sf -o /dev/null "http://127.0.0.1:$PORT/updates.xml"; then
  echo "Extension server already up at http://127.0.0.1:$PORT"
  exit 0
fi

UNIT_DIR="$HOME/.config/systemd/user"
UNIT_NAME="focusos-blocker-dist.service"

# Prefer a systemd --user unit so it survives logout/reboot and restarts on
# failure. Fall back to a detached background server otherwise.
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
  mkdir -p "$UNIT_DIR"
  cat > "$UNIT_DIR/$UNIT_NAME" <<EOF
[Unit]
Description=FocusOS Blocker local extension server
After=default.target

[Service]
ExecStart=$PY -m http.server $PORT --bind 127.0.0.1 --directory $DIST
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
EOF
  systemctl --user daemon-reload
  systemctl --user enable --now "$UNIT_NAME"
  echo "Serving $DIST at http://127.0.0.1:$PORT (systemd --user: $UNIT_NAME)."
else
  nohup "$PY" -m http.server "$PORT" --bind 127.0.0.1 --directory "$DIST" \
    >/dev/null 2>&1 &
  disown 2>/dev/null || true
  echo "Serving $DIST at http://127.0.0.1:$PORT (background — no systemd --user;"
  echo "it won't survive logout. Re-run this script or apply-update.sh after login.)"
fi
