#!/usr/bin/env bash
# Undo focusos-blocker-setup-macos.sh: stop the dist server, remove the Brave
# managed policy and the native-host manifests. Leaves ~/.focusos/blocker/dist
# in place (harmless). Run after quitting Brave; reopen Brave to drop the
# force-installed extension.
set -euo pipefail

[ "$(uname -s)" = "Darwin" ] || { echo "macOS only." >&2; exit 1; }

AGENT_LABEL="com.focusos.blocker-dist"
AGENT_PLIST="$HOME/Library/LaunchAgents/$AGENT_LABEL.plist"

echo "==> Stopping the localhost dist server"
launchctl bootout "gui/$(id -u)/$AGENT_LABEL" 2>/dev/null || true
rm -f "$AGENT_PLIST"

echo "==> Removing the Brave force-install configuration profile"
PROFILE_ID="${PROFILE_ID:-com.focusos.blocker.brave-policy}"
PROFILE_OUT="$HOME/.focusos/blocker/FocusOS-Blocker.mobileconfig"
if sudo profiles remove -identifier "$PROFILE_ID" 2>/dev/null; then
  echo "    removed profile $PROFILE_ID"
else
  echo "    profile $PROFILE_ID not installed via CLI — if it shows in"
  echo "    System Settings → General → Device Management, remove it there."
fi
rm -f "$PROFILE_OUT"

echo "==> Cleaning up any legacy direct-written managed prefs (older setups)"
EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 is required." >&2; exit 1; }

POLICY_DOMAINS=("com.brave.Browser" "com.brave.browser")
if [ -d "/Applications/Brave Browser.app" ]; then
  bundle_id="$(plutil -extract CFBundleIdentifier raw -o - "/Applications/Brave Browser.app/Contents/Info.plist" 2>/dev/null || true)"
  [ -n "$bundle_id" ] && POLICY_DOMAINS+=("$bundle_id")
fi

UNIQUE_DOMAINS=()
for domain in "${POLICY_DOMAINS[@]}"; do
  present=0
  for existing in "${UNIQUE_DOMAINS[@]}"; do
    [ "$existing" = "$domain" ] && present=1 && break
  done
  [ "$present" -eq 0 ] && UNIQUE_DOMAINS+=("$domain")
done

remove_policy_entries() {
  local target="$1"
  [ -f "$target" ] || return 0
  sudo "$PY" - "$target" "$EXT_ID" <<'PY'
import os, plistlib, sys

target, ext_id = sys.argv[1:3]
with open(target, "rb") as fh:
    data = plistlib.load(fh)
if not isinstance(data, dict):
    raise SystemExit(0)

forcelist = data.get("ExtensionInstallForcelist")
if isinstance(forcelist, list):
    data["ExtensionInstallForcelist"] = [x for x in forcelist if not str(x).startswith(ext_id + ";")]
    if not data["ExtensionInstallForcelist"]:
        data.pop("ExtensionInstallForcelist", None)

settings = data.get("ExtensionSettings")
if isinstance(settings, dict):
    settings.pop(ext_id, None)
    if not settings:
        data.pop("ExtensionSettings", None)

sources = data.get("ExtensionInstallSources")
if isinstance(sources, list):
    data["ExtensionInstallSources"] = [x for x in sources if "127.0.0.1:48217" not in str(x)]
    if not data["ExtensionInstallSources"]:
        data.pop("ExtensionInstallSources", None)

if data:
    with open(target, "wb") as fh:
        plistlib.dump(data, fh, fmt=plistlib.FMT_XML, sort_keys=True)
else:
    os.remove(target)
PY
  [ ! -f "$target" ] || sudo chmod 644 "$target"
  [ ! -f "$target" ] || plutil -lint "$target" >/dev/null
}

for domain in "${UNIQUE_DOMAINS[@]}"; do
  remove_policy_entries "/Library/Managed Preferences/$domain.plist"
  remove_policy_entries "/Library/Managed Preferences/$(id -un)/$domain.plist"
done
sudo killall cfprefsd 2>/dev/null || true

echo "==> Removing native-messaging host manifests"
HOST_NAME="com.focusos.blocker"
BASE="$HOME/Library/Application Support"
shopt -s nullglob
for profile in "$BASE"/Google/Chrome* "$BASE"/BraveSoftware/* "$BASE"/Chromium "$BASE"/Microsoft\ Edge* "$BASE"/Vivaldi; do
  m="$profile/NativeMessagingHosts/$HOST_NAME.json"
  [ -f "$m" ] && { rm -f "$m"; echo "  removed $m"; }
done
shopt -u nullglob

echo "==> Removing the staged unpacked extension"
rm -rf "$HOME/.focusos/blocker/extension"

cat <<EOF
Done. Remaining manual step:
  • brave://extensions → remove "FocusOS Blocker" (it was loaded unpacked by hand,
    so the browser keeps it until you remove it there).
EOF
