#!/usr/bin/env bash
# ─── FocusOS permanent install (Linux / SDDM + Wayland) ─────────────────────
#
# One-time, sudo-required installer that turns the box into a FocusOS-only
# machine: FocusOS becomes the sole selectable login session, the other
# Plasma/Sway/etc. session entries are stashed away, and a scoped sudoers rule
# lets the (unprivileged) FocusOS process restore those sessions later — but
# only by running the recovery script, which the app gates behind a valid TOTP
# code.
#
# Update model is no-sudo / rebuild-in-place: FocusOS runs out of this home
# checkout's build dir, so the in-app updater can `git pull` + rebuild without
# root. We therefore build here and point the session at <repo>/build/focusos.
#
# Run as:   sudo ./packaging/linux/install.sh
# (it figures out the invoking user from $SUDO_USER)

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

echo "── FocusOS install ───────────────────────────────────────"
echo "user:  $TARGET_USER ($TARGET_HOME)"
echo "repo:  $REPO_DIR"
echo "binary: $BIN_PATH"

# 1. Build the binary as the target user (keeps the checkout user-owned so the
#    no-sudo updater can rebuild later).
echo "── building FocusOS ──────────────────────────────────────"
GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
sudo -u "$TARGET_USER" cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
sudo -u "$TARGET_USER" cmake --build "$BUILD_DIR" --target focusos
[[ -x "$BIN_PATH" ]] || { echo "ERROR: build did not produce $BIN_PATH" >&2; exit 2; }

# 2. Install helper scripts + locked-down KDE config into $LIB_DIR.
echo "── installing scripts → $LIB_DIR ───────────────────────"
install -d "$LIB_DIR"
for s in focusos-watchdog.sh focusos-update.sh focusos-revert.sh \
         focusos-relaunch.sh focusos-restore-sessions.sh; do
    install -m 0755 "$SCRIPT_DIR/$s" "$LIB_DIR/$s"
done
install -d "$LIB_DIR/config"
if [[ -d "$SCRIPT_DIR/config" ]]; then
    cp -a "$SCRIPT_DIR/config/." "$LIB_DIR/config/"
fi

# 3. Install the session wrapper, baking in the binary + lib locations.
echo "── installing session wrapper ───────────────────────────"
SESSION_BIN="/usr/local/bin/focusos-session"
install -m 0755 "$SCRIPT_DIR/focusos-session.sh" "$SESSION_BIN"
# Prepend the install-specific defaults so the shipped script picks them up.
{
    echo "#!/usr/bin/env bash"
    echo "export FOCUSOS_BIN=\"$BIN_PATH\""
    echo "export FOCUSOS_LIB=\"$LIB_DIR\""
    echo "export FOCUSOS_MODE=\"\${FOCUSOS_MODE:-kwin}\""
    tail -n +2 "$SCRIPT_DIR/focusos-session.sh"
} > "$SESSION_BIN"
chmod 0755 "$SESSION_BIN"

# 4. Stash the OTHER login sessions so FocusOS is the only one offered, then
#    install the FocusOS session entry.
echo "── stashing other login sessions ────────────────────────"
install -d "$STASH_DIR/wayland" "$STASH_DIR/xsessions"
for dir_pair in "$WAYLAND_SESSIONS:wayland" "$X_SESSIONS:xsessions"; do
    src="${dir_pair%%:*}"; sub="${dir_pair##*:}"
    [[ -d "$src" ]] || continue
    for entry in "$src"/*.desktop; do
        [[ -e "$entry" ]] || continue
        base="$(basename "$entry")"
        [[ "$base" == "focusos.desktop" ]] && continue
        mv "$entry" "$STASH_DIR/$sub/$base"
        echo "  stashed $base"
    done
done
install -d "$WAYLAND_SESSIONS"
install -m 0644 "$SCRIPT_DIR/focusos.desktop" "$WAYLAND_SESSIONS/focusos.desktop"

# 5. logind lockdown (no spare VTs, kill leftover user processes on logout).
echo "── installing logind lockdown ───────────────────────────"
install -d /etc/systemd/logind.conf.d
install -m 0644 "$SCRIPT_DIR/90-focusos-logind.conf" /etc/systemd/logind.conf.d/90-focusos-logind.conf

# 6. Scoped sudoers: let the FocusOS user run ONLY the recovery script without a
#    password. The app gates calling it behind a valid TOTP code.
echo "── installing scoped sudoers ────────────────────────────"
SUDOERS_TMP="$(mktemp)"
cat > "$SUDOERS_TMP" <<EOF
# FocusOS recovery — allow $TARGET_USER to restore the stashed login sessions
# without a password. Gated in-app behind a valid TOTP code.
$TARGET_USER ALL=(root) NOPASSWD: $LIB_DIR/focusos-restore-sessions.sh
EOF
if visudo -cf "$SUDOERS_TMP" >/dev/null; then
    install -m 0440 "$SUDOERS_TMP" /etc/sudoers.d/focusos
    echo "  installed /etc/sudoers.d/focusos"
else
    echo "ERROR: generated sudoers failed validation; not installing." >&2
    rm -f "$SUDOERS_TMP"
    exit 3
fi
rm -f "$SUDOERS_TMP"

# 7. Give nft the capability to apply the network allowlist without root, so the
#    unprivileged FocusOS session can engage strict mode.
if command -v nft >/dev/null 2>&1; then
    echo "── granting CAP_NET_ADMIN to nft ────────────────────────"
    setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)" || \
        echo "WARNING: setcap on nft failed; network lock may need root." >&2
fi

systemctl restart systemd-logind 2>/dev/null || \
    echo "NOTE: restart systemd-logind (or reboot) to apply the logind config."

echo "── install complete ─────────────────────────────────────"
echo "Reboot, then pick FocusOS at the SDDM login screen (it should be the only"
echo "option). Recovery: open Settings → SYSTEM and use 'Restore other sessions'"
echo "after entering your TOTP code."
