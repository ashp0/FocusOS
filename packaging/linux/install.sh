#!/usr/bin/env bash
# ─── FocusOS permanent kiosk install (Linux / SDDM + Wayland) ───────────────
#
# Builds FocusOS and installs it as the only selectable SDDM session. Existing
# sessions are stashed under /usr/local/lib/focusos/stashed-sessions and can be
# restored from the in-app SYSTEM tab after TOTP unlock.

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
STASH_DIR="$LIB_DIR/stashed-sessions"
WAYLAND_SESSIONS="/usr/share/wayland-sessions"
X_SESSIONS="/usr/share/xsessions"
SDDM_CONF_DIR="/etc/sddm.conf.d"
LOGIND_CONF_DIR="/etc/systemd/logind.conf.d"
SUDOERS_FILE="/etc/sudoers.d/focusos"

echo "── FocusOS install (Permanent Kiosk) ─────────────────────"
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
for s in focusos-watchdog.sh focusos-update.sh focusos-revert.sh focusos-relaunch.sh focusos-restore-sessions.sh; do
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

# 4. Stash all other sessions, then install the FocusOS session entry
echo "── stashing other login sessions ────────────────────────"
install -d "$STASH_DIR/wayland" "$STASH_DIR/xsessions"
for pair in "$WAYLAND_SESSIONS:$STASH_DIR/wayland" "$X_SESSIONS:$STASH_DIR/xsessions"; do
    src="${pair%%:*}"
    dst="${pair##*:}"
    [[ -d "$src" ]] || continue
    shopt -s nullglob
    for entry in "$src"/*.desktop; do
        base="$(basename "$entry")"
        [[ "$base" == "focusos.desktop" ]] && continue
        if [[ ! -e "$dst/$base" ]]; then
            mv "$entry" "$dst/$base"
            echo "stashed $base"
        else
            rm -f "$entry"
        fi
    done
    shopt -u nullglob
done

echo "── installing FocusOS session entry ─────────────────────"
install -d "$WAYLAND_SESSIONS"
install -m 0644 "$SCRIPT_DIR/focusos.desktop" "$WAYLAND_SESSIONS/focusos.desktop"

# 5. Pin SDDM to FocusOS when autologin is used; with other sessions stashed,
# the greeter has no alternate desktop to offer.
echo "── pinning SDDM session ─────────────────────────────────"
install -d "$SDDM_CONF_DIR"
cat > "$SDDM_CONF_DIR/10-focusos.conf" <<EOF
[Autologin]
Session=focusos.desktop

[General]
DisplayServer=wayland
EOF

# 6. Install logind + getty lockdown
echo "── installing logind / VT lockdown ──────────────────────"
install -d "$LOGIND_CONF_DIR"
install -m 0644 "$SCRIPT_DIR/90-focusos-logind.conf" "$LOGIND_CONF_DIR/90-focusos-logind.conf"
for n in 1 2 3 4 5 6; do
    systemctl mask --now "getty@tty${n}.service" "autovt@tty${n}.service" >/dev/null 2>&1 || true
done
systemctl restart systemd-logind 2>/dev/null || true

# 7. Scoped recovery sudoers rule
echo "── installing scoped recovery sudoers rule ──────────────"
cat > "$SUDOERS_FILE" <<EOF
$TARGET_USER ALL=(root) NOPASSWD: $LIB_DIR/focusos-restore-sessions.sh
EOF
chmod 0440 "$SUDOERS_FILE"
if command -v visudo >/dev/null 2>&1; then
    visudo -cf "$SUDOERS_FILE" >/dev/null
fi

# 8. Network admin privileges
if command -v nft >/dev/null 2>&1; then
    echo "── granting CAP_NET_ADMIN to nft ────────────────────────"
    setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)" || \
        echo "WARNING: setcap on nft failed; network lock may need root." >&2
fi

echo "── install complete ─────────────────────────────────────"
echo "FocusOS is now the only selectable SDDM session. Reboot to enter kiosk mode."
