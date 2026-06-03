#!/usr/bin/env bash
# ─── FocusOS installer (Linux / SDDM + Wayland) ─────────────────────────────
#
# Interactive installer with selectable STRICTNESS LEVELS so you can start at
# "low" to verify FocusOS launches before turning on the restrictive kiosk
# lockdown. Re-run with a higher level once you trust the build.
#
#   low     Testing posture. Installs FocusOS as ONE MORE selectable session
#           next to Plasma. No autologin, no session stashing, no VT masking, no
#           logind/power lockdown. Pick "FocusOS" at the SDDM login screen; your
#           normal Plasma session stays the default. Safest place to start.
#
#   medium  Default-to-FocusOS. Autologins into FocusOS, but logout returns to
#           the SDDM greeter where Plasma is still selectable, the TTYs stay
#           open (Ctrl+Alt+F2 escape), and the power/lid lockdown is on.
#
#   max     Permanent kiosk. Stashes every other session, hides the SDDM
#           selector, masks the gettys, full logind lockdown + scoped TOTP
#           recovery. Leaving requires the in-app TOTP recovery (or the crash
#           fallback below).
#
# SELF-HEAL: at every level the session wrapper falls back to the stock Plasma
# session if FocusOS crash-loops, so a bad build can never brick the machine
# (the failure mode that bricked the MacBook). See focusos-session.sh.
#
# UPDATER CONTRACT (do not break when editing this file): the in-app updater
# (src/core/Updater.cpp → focusos-update.sh) git-pulls and rebuilds the binary
# *in place* inside the home git checkout, and locates its helper scripts under
# /usr/local/lib/focusos. This installer therefore MUST keep (1) building the
# binary at "$REPO_DIR/build/focusos" and (2) installing the helper scripts into
# "$LIB_DIR". Both are preserved below regardless of strictness level.

set -euo pipefail

# ── Root / target-user resolution ───────────────────────────────────────────
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
MANIFEST="$LIB_DIR/install-state.conf"
WAYLAND_SESSIONS="/usr/share/wayland-sessions"
X_SESSIONS="/usr/share/xsessions"
LOCAL_WAYLAND_SESSIONS="/usr/local/share/wayland-sessions"
LOCAL_X_SESSIONS="/usr/local/share/xsessions"
SDDM_CONF_DIR="/etc/sddm.conf.d"
LOGIND_CONF_DIR="/etc/systemd/logind.conf.d"
SLEEP_CONF_DIR="/etc/systemd/sleep.conf.d"
SUDOERS_FILE="/etc/sudoers.d/focusos"
SESSION_BIN="/usr/local/bin/focusos-session"

# ── Feature flags (set from the level, then optionally customised) ──────────
# 1 = on, 0 = off.
FEAT_BUILD=1            # build the binary (off only for re-config of policy)
FEAT_STASH_SESSIONS=0   # move other login sessions out of the greeter
FEAT_AUTOLOGIN=0        # SDDM autologins straight into FocusOS
FEAT_HIDE_SELECTOR=0    # force the greeter Wayland-only / single session
FEAT_LOGIND_LOCK=0      # power/lid/suspend keys → lock, no spare VTs
FEAT_MASK_VT=0          # mask getty@/autovt@ tty1..6 (no Ctrl+Alt+F<n>)
FEAT_SLEEP_CONF=1       # pin opt-in suspend to s2idle (safe on S3-bad hardware)
FEAT_NET_CAP=1          # grant CAP_NET_ADMIN to nft for the outbound allowlist
FEAT_RECOVERY_SUDOERS=0 # scoped NOPASSWD rule for focusos-restore-sessions.sh

LEVEL=""
ASSUME_YES=0
CUSTOMIZE=""

# ── Arg parsing (also drives non-interactive installs / CI) ─────────────────
usage() {
    cat >&2 <<USAGE
Usage: sudo $0 [--level low|medium|max] [--yes] [feature overrides]

  --level <low|medium|max>   Strictness preset (prompted if omitted).
  --yes                      Non-interactive; accept the preset without prompts.
  --no-build                 Skip the cmake build (reuse the existing binary).

Per-feature overrides (override the preset; --no-* disables, --* enables):
  --[no-]stash-sessions   --[no-]autologin     --[no-]hide-selector
  --[no-]logind-lock      --[no-]mask-vt       --[no-]sleep-conf
  --[no-]net-cap          --[no-]recovery-sudoers
USAGE
    exit 2
}

# Snapshot argv before the parse loop consumes it — the feature overrides are
# re-applied AFTER the preset (restore_overrides) so explicit flags win.
ORIG_ARGS=("$@")

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="${2:-}"; shift 2 ;;
        --level=*) LEVEL="${1#*=}"; shift ;;
        --yes|-y) ASSUME_YES=1; shift ;;
        --no-build) FEAT_BUILD=0; shift ;;
        --stash-sessions) FEAT_STASH_SESSIONS=1; shift ;;
        --no-stash-sessions) FEAT_STASH_SESSIONS=0; shift ;;
        --autologin) FEAT_AUTOLOGIN=1; shift ;;
        --no-autologin) FEAT_AUTOLOGIN=0; shift ;;
        --hide-selector) FEAT_HIDE_SELECTOR=1; shift ;;
        --no-hide-selector) FEAT_HIDE_SELECTOR=0; shift ;;
        --logind-lock) FEAT_LOGIND_LOCK=1; shift ;;
        --no-logind-lock) FEAT_LOGIND_LOCK=0; shift ;;
        --mask-vt) FEAT_MASK_VT=1; shift ;;
        --no-mask-vt) FEAT_MASK_VT=0; shift ;;
        --sleep-conf) FEAT_SLEEP_CONF=1; shift ;;
        --no-sleep-conf) FEAT_SLEEP_CONF=0; shift ;;
        --net-cap) FEAT_NET_CAP=1; shift ;;
        --no-net-cap) FEAT_NET_CAP=0; shift ;;
        --recovery-sudoers) FEAT_RECOVERY_SUDOERS=1; shift ;;
        --no-recovery-sudoers) FEAT_RECOVERY_SUDOERS=0; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown argument: $1" >&2; usage ;;
    esac
done

apply_preset() {
    case "$1" in
        low)
            FEAT_STASH_SESSIONS=0; FEAT_AUTOLOGIN=0; FEAT_HIDE_SELECTOR=0
            FEAT_LOGIND_LOCK=0; FEAT_MASK_VT=0; FEAT_SLEEP_CONF=0
            FEAT_NET_CAP=0; FEAT_RECOVERY_SUDOERS=0
            ;;
        medium)
            FEAT_STASH_SESSIONS=0; FEAT_AUTOLOGIN=1; FEAT_HIDE_SELECTOR=0
            FEAT_LOGIND_LOCK=1; FEAT_MASK_VT=0; FEAT_SLEEP_CONF=1
            FEAT_NET_CAP=1; FEAT_RECOVERY_SUDOERS=1
            ;;
        max)
            FEAT_STASH_SESSIONS=1; FEAT_AUTOLOGIN=1; FEAT_HIDE_SELECTOR=1
            FEAT_LOGIND_LOCK=1; FEAT_MASK_VT=1; FEAT_SLEEP_CONF=1
            FEAT_NET_CAP=1; FEAT_RECOVERY_SUDOERS=1
            ;;
        *) echo "Invalid level: $1 (expected low|medium|max)" >&2; exit 2 ;;
    esac
}

# ── Interactive level prompt ────────────────────────────────────────────────
prompt_level() {
    echo "── Choose a strictness level ─────────────────────────────" >&2
    echo "  1) low    — FocusOS is a selectable session; Plasma stays default" >&2
    echo "              (recommended for first install / verifying it launches)" >&2
    echo "  2) medium — autologin into FocusOS; logout/TTY/Plasma escapes remain" >&2
    echo "  3) max    — permanent kiosk; TOTP recovery is the only way out" >&2
    local reply
    while true; do
        read -r -p "Level [1/2/3] (default 1): " reply </dev/tty || reply=""
        case "${reply:-1}" in
            1|low)    LEVEL="low"; return ;;
            2|medium) LEVEL="medium"; return ;;
            3|max)    LEVEL="max"; return ;;
            *) echo "Please answer 1, 2, or 3." >&2 ;;
        esac
    done
}

ask_yn() { # ask_yn "question" default(0/1) -> echoes 0/1
    local q="$1" def="$2" reply hint
    [[ "$def" == "1" ]] && hint="[Y/n]" || hint="[y/N]"
    read -r -p "$q $hint " reply </dev/tty || reply=""
    if [[ -z "$reply" ]]; then printf '%s' "$def"; return; fi
    case "$reply" in
        [Yy]*) printf '1' ;;
        [Nn]*) printf '0' ;;
        *) printf '%s' "$def" ;;
    esac
}

# Resolve the level: explicit --level wins; otherwise prompt (unless --yes).
if [[ -z "$LEVEL" ]]; then
    if [[ "$ASSUME_YES" -eq 1 ]]; then
        LEVEL="low"   # safest default for unattended installs
    else
        prompt_level
    fi
fi
apply_preset "$LEVEL"

# apply_preset just overwrote the feature flags with the level defaults. Re-apply
# any per-feature flags the user passed explicitly so they take precedence.
restore_overrides() {
    local arg
    for arg in "${ORIG_ARGS[@]:-}"; do
        case "$arg" in
            --no-build) FEAT_BUILD=0 ;;
            --stash-sessions) FEAT_STASH_SESSIONS=1 ;;
            --no-stash-sessions) FEAT_STASH_SESSIONS=0 ;;
            --autologin) FEAT_AUTOLOGIN=1 ;;
            --no-autologin) FEAT_AUTOLOGIN=0 ;;
            --hide-selector) FEAT_HIDE_SELECTOR=1 ;;
            --no-hide-selector) FEAT_HIDE_SELECTOR=0 ;;
            --logind-lock) FEAT_LOGIND_LOCK=1 ;;
            --no-logind-lock) FEAT_LOGIND_LOCK=0 ;;
            --mask-vt) FEAT_MASK_VT=1 ;;
            --no-mask-vt) FEAT_MASK_VT=0 ;;
            --sleep-conf) FEAT_SLEEP_CONF=1 ;;
            --no-sleep-conf) FEAT_SLEEP_CONF=0 ;;
            --net-cap) FEAT_NET_CAP=1 ;;
            --no-net-cap) FEAT_NET_CAP=0 ;;
            --recovery-sudoers) FEAT_RECOVERY_SUDOERS=1 ;;
            --no-recovery-sudoers) FEAT_RECOVERY_SUDOERS=0 ;;
        esac
    done
}
restore_overrides

# Optional interactive per-feature customisation.
onoff() { [[ "$1" -eq 1 ]] && echo "on" || echo "off"; }
if [[ "$ASSUME_YES" -ne 1 ]]; then
    CUSTOMIZE="$(ask_yn "Customise individual features beyond the '$LEVEL' preset?" 0)"
    if [[ "$CUSTOMIZE" == "1" ]]; then
        FEAT_AUTOLOGIN="$(ask_yn "  Autologin straight into FocusOS?" "$FEAT_AUTOLOGIN")"
        FEAT_STASH_SESSIONS="$(ask_yn "  Hide other login sessions (stash Plasma etc.)?" "$FEAT_STASH_SESSIONS")"
        FEAT_HIDE_SELECTOR="$(ask_yn "  Force the greeter Wayland-only / no selector?" "$FEAT_HIDE_SELECTOR")"
        FEAT_LOGIND_LOCK="$(ask_yn "  Lock down power/lid/suspend keys (logind)?" "$FEAT_LOGIND_LOCK")"
        FEAT_MASK_VT="$(ask_yn "  Mask the virtual terminals (Ctrl+Alt+F<n>)?" "$FEAT_MASK_VT")"
        FEAT_NET_CAP="$(ask_yn "  Grant CAP_NET_ADMIN to nft (network allowlist)?" "$FEAT_NET_CAP")"
        FEAT_SLEEP_CONF="$(ask_yn "  Install the safe-suspend (s2idle) sleep config?" "$FEAT_SLEEP_CONF")"
        FEAT_RECOVERY_SUDOERS="$(ask_yn "  Install the scoped TOTP-recovery sudoers rule?" "$FEAT_RECOVERY_SUDOERS")"
    fi
fi

# ── Summary + confirmation ──────────────────────────────────────────────────
echo "── FocusOS install plan ──────────────────────────────────"
echo "user:            $TARGET_USER ($TARGET_HOME)"
echo "repo:            $REPO_DIR"
echo "binary:          $BIN_PATH"
echo "level:           $LEVEL"
echo "build binary:    $(onoff "$FEAT_BUILD")"
echo "autologin:       $(onoff "$FEAT_AUTOLOGIN")"
echo "stash sessions:  $(onoff "$FEAT_STASH_SESSIONS")"
echo "hide selector:   $(onoff "$FEAT_HIDE_SELECTOR")"
echo "logind lockdown: $(onoff "$FEAT_LOGIND_LOCK")"
echo "mask VTs:        $(onoff "$FEAT_MASK_VT")"
echo "safe-suspend:    $(onoff "$FEAT_SLEEP_CONF")"
echo "nft net cap:     $(onoff "$FEAT_NET_CAP")"
echo "recovery sudoers:$(onoff "$FEAT_RECOVERY_SUDOERS")"
echo "──────────────────────────────────────────────────────────"

if [[ "$ASSUME_YES" -ne 1 ]]; then
    if [[ "$(ask_yn "Proceed with this install?" 1)" != "1" ]]; then
        echo "Aborted." >&2
        exit 1
    fi
fi

# ── 1. Build the binary (in place — updater contract) ───────────────────────
if [[ "$FEAT_BUILD" -eq 1 ]]; then
    echo "── building FocusOS ──────────────────────────────────────"
    GENERATOR="Unix Makefiles"
    command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
    sudo -u "$TARGET_USER" cmake -S "$REPO_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
    sudo -u "$TARGET_USER" cmake --build "$BUILD_DIR" --target focusos
fi
[[ -x "$BIN_PATH" ]] || { echo "ERROR: $BIN_PATH not found (build it, or drop --no-build)." >&2; exit 2; }

# ── 2. Install helper scripts + KDE config (always — updater + recovery) ────
echo "── installing scripts → $LIB_DIR ───────────────────────"
install -d "$LIB_DIR"
for s in focusos-watchdog.sh focusos-update.sh focusos-revert.sh focusos-relaunch.sh focusos-restore-sessions.sh; do
    install -m 0755 "$SCRIPT_DIR/$s" "$LIB_DIR/$s"
done
install -d "$LIB_DIR/config"
if [[ -d "$SCRIPT_DIR/config" ]]; then
    cp -a "$SCRIPT_DIR/config/." "$LIB_DIR/config/"
fi

# ── 3. Session wrapper ──────────────────────────────────────────────────────
# Discover the stock session to fall back to (baked into the wrapper so the
# crash-fallback is deterministic even if PATH differs under SDDM).
FALLBACK_SESSION=""
for c in startplasma-wayland startplasma-x11; do
    if command -v "$c" >/dev/null 2>&1; then
        FALLBACK_SESSION="$(command -v "$c")"
        break
    fi
done
if [[ -z "$FALLBACK_SESSION" ]]; then
    echo "WARNING: no startplasma-* found; crash fallback will rely on a runtime" >&2
    echo "         PATH lookup. Install Plasma so a bad build can fall back to it." >&2
fi

echo "── installing session wrapper → $SESSION_BIN ────────────"
{
    echo "#!/usr/bin/env bash"
    echo "export FOCUSOS_BIN=\"$BIN_PATH\""
    echo "export FOCUSOS_LIB=\"$LIB_DIR\""
    echo "export FOCUSOS_MODE=\"\${FOCUSOS_MODE:-kwin}\""
    if [[ -n "$FALLBACK_SESSION" ]]; then
        echo "export FOCUSOS_FALLBACK_SESSION=\"\${FOCUSOS_FALLBACK_SESSION:-$FALLBACK_SESSION}\""
    fi
    tail -n +2 "$SCRIPT_DIR/focusos-session.sh"
} > "$SESSION_BIN"
chmod 0755 "$SESSION_BIN"

# ── 4. Session entry (always — FocusOS must be selectable/launchable) ───────
echo "── installing FocusOS session entry ─────────────────────"
install -d "$WAYLAND_SESSIONS"
install -m 0644 "$SCRIPT_DIR/focusos.desktop" "$WAYLAND_SESSIONS/focusos.desktop"

# ── 5. Optionally stash all other sessions (kiosk) ──────────────────────────
if [[ "$FEAT_STASH_SESSIONS" -eq 1 ]]; then
    echo "── stashing other login sessions ────────────────────────"
    install -d "$STASH_DIR/wayland" "$STASH_DIR/xsessions"
    for pair in \
        "$WAYLAND_SESSIONS:$STASH_DIR/wayland" \
        "$X_SESSIONS:$STASH_DIR/xsessions" \
        "$LOCAL_WAYLAND_SESSIONS:$STASH_DIR/wayland" \
        "$LOCAL_X_SESSIONS:$STASH_DIR/xsessions"; do
        src="${pair%%:*}"; dst="${pair##*:}"
        [[ -d "$src" ]] || continue
        shopt -s nullglob
        for entry in "$src"/*.desktop; do
            base="$(basename "$entry")"
            [[ "$base" == "focusos.desktop" ]] && continue
            if [[ ! -e "$dst/$base" ]]; then
                mv "$entry" "$dst/$base"; echo "stashed $base"
            else
                rm -f "$entry"
            fi
        done
        shopt -u nullglob
    done
else
    echo "── leaving other login sessions in place (Plasma stays selectable) ──"
fi

# ── 6. SDDM configuration ───────────────────────────────────────────────────
install -d "$SDDM_CONF_DIR"
SDDM_CONF="$SDDM_CONF_DIR/10-focusos.conf"
if [[ "$FEAT_AUTOLOGIN" -eq 1 || "$FEAT_HIDE_SELECTOR" -eq 1 ]]; then
    echo "── writing SDDM config ($SDDM_CONF) ────────────"
    {
        if [[ "$FEAT_AUTOLOGIN" -eq 1 ]]; then
            echo "[Autologin]"
            echo "User=$TARGET_USER"
            echo "Session=focusos.desktop"
            # Relogin=false so logging OUT returns to the greeter (an escape to
            # Plasma at medium) instead of immediately re-entering FocusOS.
            echo "Relogin=false"
            echo ""
        fi
        if [[ "$FEAT_HIDE_SELECTOR" -eq 1 ]]; then
            echo "[General]"
            echo "DisplayServer=wayland"
            echo "GreeterEnvironment=QT_WAYLAND_DISABLE_WINDOWDECORATION=1"
        fi
    } > "$SDDM_CONF"
else
    # Low strictness: ensure no stale FocusOS SDDM pin remains from a prior run.
    rm -f "$SDDM_CONF"
    echo "── no SDDM autologin/selector changes (pick FocusOS at the greeter) ──"
fi

# ── 7. logind / VT lockdown ─────────────────────────────────────────────────
if [[ "$FEAT_LOGIND_LOCK" -eq 1 ]]; then
    echo "── installing logind lockdown ───────────────────────────"
    install -d "$LOGIND_CONF_DIR"
    install -m 0644 "$SCRIPT_DIR/90-focusos-logind.conf" "$LOGIND_CONF_DIR/90-focusos-logind.conf"
    systemctl restart systemd-logind 2>/dev/null || true
else
    rm -f "$LOGIND_CONF_DIR/90-focusos-logind.conf" 2>/dev/null || true
fi

if [[ "$FEAT_SLEEP_CONF" -eq 1 ]]; then
    echo "── installing safe-suspend (s2idle) sleep config ────────"
    install -d "$SLEEP_CONF_DIR"
    install -m 0644 "$SCRIPT_DIR/90-focusos-sleep.conf" "$SLEEP_CONF_DIR/90-focusos-sleep.conf"
fi

if [[ "$FEAT_MASK_VT" -eq 1 ]]; then
    echo "── masking virtual terminals ────────────────────────────"
    for n in 1 2 3 4 5 6; do
        systemctl mask --now "getty@tty${n}.service" "autovt@tty${n}.service" >/dev/null 2>&1 || true
    done
else
    # Make sure a previous max install didn't leave the gettys masked.
    for n in 1 2 3 4 5 6; do
        systemctl unmask "getty@tty${n}.service" "autovt@tty${n}.service" >/dev/null 2>&1 || true
    done
fi

# ── 8. Scoped recovery sudoers rule ─────────────────────────────────────────
if [[ "$FEAT_RECOVERY_SUDOERS" -eq 1 ]]; then
    echo "── installing scoped recovery sudoers rule ──────────────"
    cat > "$SUDOERS_FILE" <<EOF
$TARGET_USER ALL=(root) NOPASSWD: $LIB_DIR/focusos-restore-sessions.sh
EOF
    chmod 0440 "$SUDOERS_FILE"
    if command -v visudo >/dev/null 2>&1; then
        visudo -cf "$SUDOERS_FILE" >/dev/null
    fi
else
    rm -f "$SUDOERS_FILE" 2>/dev/null || true
fi

# ── 9. Network admin privileges ─────────────────────────────────────────────
if [[ "$FEAT_NET_CAP" -eq 1 ]] && command -v nft >/dev/null 2>&1; then
    echo "── granting CAP_NET_ADMIN to nft ────────────────────────"
    setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)" || \
        echo "WARNING: setcap on nft failed; network lock may need root." >&2
fi

# ── 10. Install manifest (records posture for re-runs / uninstall / support) ─
{
    echo "# FocusOS install manifest — written by install.sh"
    echo "FOCUSOS_INSTALL_LEVEL=$LEVEL"
    echo "FOCUSOS_INSTALL_DATE=$(date -u +%FT%TZ)"
    echo "FOCUSOS_TARGET_USER=$TARGET_USER"
    echo "FOCUSOS_BIN=$BIN_PATH"
    echo "FOCUSOS_REPO=$REPO_DIR"
    echo "FOCUSOS_FALLBACK_SESSION=$FALLBACK_SESSION"
    echo "FEAT_STASH_SESSIONS=$FEAT_STASH_SESSIONS"
    echo "FEAT_AUTOLOGIN=$FEAT_AUTOLOGIN"
    echo "FEAT_HIDE_SELECTOR=$FEAT_HIDE_SELECTOR"
    echo "FEAT_LOGIND_LOCK=$FEAT_LOGIND_LOCK"
    echo "FEAT_MASK_VT=$FEAT_MASK_VT"
    echo "FEAT_SLEEP_CONF=$FEAT_SLEEP_CONF"
    echo "FEAT_NET_CAP=$FEAT_NET_CAP"
    echo "FEAT_RECOVERY_SUDOERS=$FEAT_RECOVERY_SUDOERS"
} > "$MANIFEST"
chmod 0644 "$MANIFEST"

# ── Done ────────────────────────────────────────────────────────────────────
echo "── install complete ($LEVEL) ─────────────────────────────"
case "$LEVEL" in
    low)
        echo "FocusOS is installed as a SELECTABLE session. At the SDDM login"
        echo "screen, click the session menu and choose \"FocusOS\". Your normal"
        echo "Plasma session is unchanged and stays the default."
        ;;
    medium)
        echo "SDDM will autologin into FocusOS. Log OUT to return to the greeter"
        echo "where Plasma is still selectable; Ctrl+Alt+F2 gives a TTY escape."
        ;;
    max)
        echo "FocusOS is the only SDDM session (kiosk). Other sessions are stashed"
        echo "and the selector is hidden. Leave via in-app TOTP recovery."
        ;;
esac
echo "Self-heal: if a build crash-loops, the session falls back to the stock"
echo "desktop automatically (touch ~/.focusos/boot-to-plasma to force it)."
echo "Reboot (or re-login) to enter FocusOS."
