#!/usr/bin/env bash
# Force-install the FocusOS Blocker extension via Chromium-family managed policy,
# pointing at the locally self-hosted updates.xml (file://). It writes under
# /etc, so it needs sudo the first time (and whenever a new Brave channel adds a
# target). The script is idempotent: it compares each target against the desired
# policy and exits without sudo when everything already matches, so apply-update.sh
# can safely call it on every `git pull`.
set -euo pipefail

if [ "$(uname -s)" != "Linux" ]; then
  echo "Managed-policy force-install is Linux-only. Skipping on $(uname -s)." >&2
  exit 0
fi

EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
# Served over a localhost HTTP origin (Chromium/Brave reject file:// update URLs
# for force-installed extensions). Port MUST match focusos-blocker-serve.sh and
# focusos-blocker-pack.sh.
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
UPDATES_URL="http://127.0.0.1:$PORT/updates.xml"

if [ ! -f "$HOME/.focusos/blocker/dist/updates.xml" ]; then
  echo "Run scripts/focusos-blocker-pack.sh first — no updates.xml found." >&2
  exit 1
fi

policy_json() {
  cat <<EOF
{
  "ExtensionInstallForcelist": ["$EXT_ID;$UPDATES_URL"],
  "ExtensionInstallSources": ["http://127.0.0.1:$PORT/*"],
  "ExtensionSettings": {
    "$EXT_ID": {
      "installation_mode": "force_installed",
      "update_url": "$UPDATES_URL",
      "toolbar_pin": "force_pinned"
    }
  },
  "IncognitoModeAvailability": 1,
  "BrowserGuestModeEnabled": false
}
EOF
}

# IncognitoModeAvailability=1 (disabled) + BrowserGuestModeEnabled=false close
# the obvious bypass: the extension does NOT run in private/incognito windows by
# default (and there's no policy to force an extension into incognito), and a
# normal window keeps the native-host heartbeat fresh — so the presence
# watchdog would NOT notice a private window browsing freely. Killing those two
# window types is the only reliable way to plug it. These are global browser
# settings (always on, not just during a routine); remove the keys if you want
# private/guest windows back on this machine.

# Install the policy for each browser detected on the machine; default to Brave
# (the user's primary) if none are detected yet.
#
# Brave ships several channels, each potentially reading its own managed-policy
# root: the classic Brave-Browser line (/etc/brave) and the newer Brave-Origin
# line (/etc/brave-origin*). We write the policy into ALL of them so whichever
# channel the user runs — including brave-origin-beta — force-installs the
# extension. Creating an unused /etc/<x>/policies/managed dir is harmless.
BRAVE_POLICY_DIRS=(
  "/etc/brave/policies/managed"
  "/etc/brave-beta/policies/managed"
  "/etc/brave-nightly/policies/managed"
  "/etc/brave-origin/policies/managed"
  "/etc/brave-origin-beta/policies/managed"
  "/etc/brave-origin-nightly/policies/managed"
)
# Also fold in any other Brave policy roots already present on the machine.
for existing in /etc/brave*/policies/managed; do
  [ -d "$existing" ] || continue
  case " ${BRAVE_POLICY_DIRS[*]} " in
    *" $existing "*) ;;
    *) BRAVE_POLICY_DIRS+=("$existing") ;;
  esac
done

declare -A POLICY_DIRS=(
  [chromium]="/etc/chromium/policies/managed"
  [chrome]="/etc/opt/chrome/policies/managed"
)
declare -A DETECT=(
  [chromium]="chromium chromium-browser"
  [chrome]="google-chrome google-chrome-stable"
)

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
policy_json > "$TMP"

# Assemble the full target list: every Brave channel root (always — Brave is the
# primary) plus chromium/chrome if their binaries are present.
TARGETS=("${BRAVE_POLICY_DIRS[@]}")
for key in "${!POLICY_DIRS[@]}"; do
  for bin in ${DETECT[$key]}; do
    if command -v "$bin" >/dev/null 2>&1; then
      TARGETS+=("${POLICY_DIRS[$key]}")
      break
    fi
  done
done

# Idempotent: only touch /etc (and only prompt for sudo) when a target is
# missing or out of date. This lets apply-update.sh call us on every `git pull`
# without nagging for a password once everything is already in place.
NEED_WRITE=()
for dir in "${TARGETS[@]}"; do
  dst="$dir/focusos-blocker.json"
  if [ -f "$dst" ] && cmp -s "$TMP" "$dst"; then
    continue
  fi
  NEED_WRITE+=("$dir")
done

if [ "${#NEED_WRITE[@]}" -eq 0 ]; then
  echo "Force-install policy already current for ${#TARGETS[@]} target(s); nothing to do."
  exit 0
fi

echo "Updating force-install policy for ${#NEED_WRITE[@]} target(s) (needs sudo)…"
for dir in "${NEED_WRITE[@]}"; do
  sudo mkdir -p "$dir"
  sudo install -m 644 "$TMP" "$dir/focusos-blocker.json"
  echo "  installed -> $dir/focusos-blocker.json"
done

echo "Done. Restart the browser; the FocusOS Blocker installs silently and stays pinned."
