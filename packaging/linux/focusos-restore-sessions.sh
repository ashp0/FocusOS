#!/usr/bin/env bash
# ─── FocusOS recovery: restore the stashed login sessions ───────────────────
#
# Moves the Plasma/Sway/etc. session entries that install.sh stashed back into
# /usr/share/{wayland-sessions,xsessions} so they're selectable at SDDM again.
# Also relaxes the logind lockdown so the user can switch sessions / VTs.
#
# Runs as root via the scoped sudoers rule. The FocusOS app only invokes this
# AFTER a valid TOTP code, so the six-digit code is the sole gate on leaving the
# FocusOS-only session.

set -uo pipefail

LIB_DIR="/usr/local/lib/focusos"
STASH_DIR="$LIB_DIR/stashed-sessions"
WAYLAND_SESSIONS="/usr/share/wayland-sessions"
X_SESSIONS="/usr/share/xsessions"
SDDM_CONF="/etc/sddm.conf.d/10-focusos.conf"

if [[ "${EUID}" -ne 0 ]]; then
    echo "must run as root (via the focusos sudoers rule)" >&2
    exit 1
fi

restored=0
for sub_pair in "wayland:$WAYLAND_SESSIONS" "xsessions:$X_SESSIONS"; do
    sub="${sub_pair%%:*}"; dest="${sub_pair##*:}"
    src="$STASH_DIR/$sub"
    [[ -d "$src" ]] || continue
    mkdir -p "$dest"
    for entry in "$src"/*.desktop; do
        [[ -e "$entry" ]] || continue
        base="$(basename "$entry")"
        mv -f "$entry" "$dest/$base"
        echo "restored $base → $dest"
        restored=$((restored + 1))
    done
done

# Relax the logind lockdown so session/VT switching works for recovery.
if [[ -f /etc/systemd/logind.conf.d/90-focusos-logind.conf ]]; then
    rm -f /etc/systemd/logind.conf.d/90-focusos-logind.conf
    systemctl restart systemd-logind 2>/dev/null || true
    echo "removed logind lockdown"
fi

rm -f "$SDDM_CONF"
for n in 1 2 3 4 5 6; do
    systemctl unmask "getty@tty${n}.service" "autovt@tty${n}.service" >/dev/null 2>&1 || true
done

if [[ "$restored" -eq 0 ]]; then
    echo "WARNING: no stashed sessions found to restore." >&2
    exit 4
fi

echo "── recovery complete — other sessions are selectable at login ──"
exit 0
