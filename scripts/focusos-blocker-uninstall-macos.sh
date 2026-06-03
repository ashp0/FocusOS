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

echo "==> Removing Brave managed policy (needs sudo)"
sudo rm -f "/Library/Managed Preferences/com.brave.Browser.plist" \
           "/Library/Managed Preferences/$(id -un)/com.brave.Browser.plist"
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

echo "Done. Quit and reopen Brave to drop the extension."
