#!/usr/bin/env bash
# ─── FocusOS non-permanent install (Linux / SDDM + Wayland) ─────────────────
#
# Builds FocusOS and installs the session entry ALONGSIDE your existing
# Plasma/Sway sessions. It does NOT lock down logind or stash other sessions.

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "This installer must run as root: sudo $0" >&2
    exit 1
fi

TARGET_USER="${SUDO_USER:-}"
if [[ -z "$TARGET_USER" || "$TARGET_USER" == "root" ]]; then
    echo "Run via sudo from your normal user account (need \$SUDO_USER)." >&2
    exit 1
fi
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
BIN_PATH="$BUILD_DIR/focusos"

LIB_DIR="/usr/local/lib/focusos"
WAYLAND_SESSIONS="/usr/share/wayland-sessions"

echo "── FocusOS install (Non-Permanent) ───────────────────────"
echo "user:  $TARGET_USER ($TARGET_HOME)"
echo "repo:  $REPO_DIR"
echo "binary: $BIN_PATH"

# 1. Build the binary
echo "── building FocusOS ──────────────────────────────────────"
GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
sudo -u "$TARGET_USER" cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
sudo -u "$TARGET_USER" cmake --build "$BUILD_DIR" --target focusos
[[ -x "$BIN_PATH" ]] || { echo "ERROR: build did not produce $BIN_PATH" >&2; exit 2; }

# 2. Install helper scripts + configs
echo "── installing scripts → $LIB_DIR ───────────────────────"
install -d "$LIB_DIR"
# Note: focusos-restore-sessions.sh is removed from this list as it is no longer needed.
for s in focusos-watchdog.sh focusos-update.sh focusos-revert.sh focusos-relaunch.sh; do
    install -m 0755 "$SCRIPT_DIR/$s" "$LIB_DIR/$s"
done
install -d "$LIB_DIR/config"
if [[ -d "$SCRIPT_DIR/config" ]]; then
    cp -a "$SCRIPT_DIR/config/." "$LIB_DIR/config/"
fi

# 3. Install the session wrapper
echo "── installing session wrapper ───────────────────────────"
SESSION_BIN="/usr/local/bin/focusos-session"
install -m 0755 "$SCRIPT_DIR/focusos-session.sh" "$SESSION_BIN"
{
    echo "#!/usr/bin/env bash"
    echo "export FOCUSOS_BIN=\"$BIN_PATH\""
    echo "export FOCUSOS_LIB=\"$LIB_DIR\""
    echo "export FOCUSOS_MODE=\"\${FOCUSOS_MODE:-kwin}\""
    tail -n +2 "$SCRIPT_DIR/focusos-session.sh"
} > "$SESSION_BIN"
chmod 0755 "$SESSION_BIN"

# 4. Install the FocusOS session entry (Leaves others untouched)
echo "── installing session entry ─────────────────────────────"
install -d "$WAYLAND_SESSIONS"
install -m 0644 "$SCRIPT_DIR/focusos.desktop" "$WAYLAND_SESSIONS/focusos.desktop"

# 5. Network admin privileges
if command -v nft >/dev/null 2>&1; then
    echo "── granting CAP_NET_ADMIN to nft ────────────────────────"
    setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)" || \
        echo "WARNING: setcap on nft failed; network lock may need root." >&2
fi

echo "── install complete ─────────────────────────────────────"
echo "You can now select FocusOS at the SDDM login screen alongside Plasma."
