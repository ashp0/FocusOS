#!/usr/bin/env bash
# Force-install the FocusOS Blocker extension via Chromium-family managed policy,
# pointing at the locally self-hosted updates.xml (file://). Run ONCE per machine
# — it needs sudo to write under /etc. After this, the extension installs and
# auto-updates silently; the per-pull refresh (apply-update.sh) never needs sudo.
set -euo pipefail

if [ "$(uname -s)" != "Linux" ]; then
  echo "Managed-policy force-install is Linux-only. Skipping on $(uname -s)." >&2
  exit 0
fi

EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
UPDATES_URL="file://$HOME/.focusos/blocker/dist/updates.xml"

if [ ! -f "$HOME/.focusos/blocker/dist/updates.xml" ]; then
  echo "Run scripts/focusos-blocker-pack.sh first — no updates.xml found." >&2
  exit 1
fi

policy_json() {
  cat <<EOF
{
  "ExtensionInstallForcelist": ["$EXT_ID;$UPDATES_URL"],
  "ExtensionInstallSources": ["file:///*"],
  "ExtensionSettings": {
    "$EXT_ID": {
      "installation_mode": "force_installed",
      "update_url": "$UPDATES_URL",
      "toolbar_pin": "force_pinned"
    }
  }
}
EOF
}

# Install the policy for each browser detected on the machine; default to Brave
# (the user's primary) if none are detected yet.
declare -A POLICY_DIRS=(
  [brave]="/etc/brave/policies/managed"
  [chromium]="/etc/chromium/policies/managed"
  [chrome]="/etc/opt/chrome/policies/managed"
)
declare -A DETECT=(
  [brave]="brave-browser brave"
  [chromium]="chromium chromium-browser"
  [chrome]="google-chrome google-chrome-stable"
)

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
policy_json > "$TMP"

installed=0
for key in "${!POLICY_DIRS[@]}"; do
  present=0
  for bin in ${DETECT[$key]}; do
    if command -v "$bin" >/dev/null 2>&1; then present=1; break; fi
  done
  if [ "$present" -eq 0 ] && [ "$key" != "brave" ]; then
    continue
  fi
  dir="${POLICY_DIRS[$key]}"
  sudo mkdir -p "$dir"
  sudo install -m 644 "$TMP" "$dir/focusos-blocker.json"
  echo "  installed -> $dir/focusos-blocker.json"
  installed=$((installed + 1))
done

echo "Force-install policy written for $installed browser(s)."
echo "Restart the browser; the FocusOS Blocker installs silently and stays pinned."
