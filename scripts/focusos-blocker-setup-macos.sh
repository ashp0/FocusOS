#!/usr/bin/env bash
# macOS analog of the Linux force-install pipeline: get the FocusOS Blocker
# extension force-installed into Brave (the user's Chromium browser; Safari is
# not Chromium and can't host the extension) and wired to the native host.
#
# Steps, mirroring scripts/setup-testing-machine.sh on Linux:
#   1. Pack the extension into a signed .crx + Omaha updates.xml under
#      ~/.focusos/blocker/dist  (focusos-blocker-pack.sh).
#   2. Serve that dist over http://127.0.0.1:<port> via a launchd user agent
#      (Brave/Chromium reject file:// update URLs for force-installed items).
#   3. Register the com.focusos.blocker native-messaging host for every
#      Chromium-family browser present  (host/install-host.sh).
#   4. Write Brave managed policy (ExtensionInstallForcelist + ExtensionSettings)
#      under /Library/Managed Preferences  — needs sudo.
#
# After it runs, fully quit and reopen Brave: it then fetches the crx from the
# localhost manifest, force-installs + pins the extension, and the extension
# connects to `focusos --native-host`. Verify at brave://policy and
# brave://extensions (Developer mode shows the pinned ID).
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This installer is macOS-only. On Linux use scripts/setup-testing-machine.sh." >&2
  exit 1
fi

EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
UPDATES_URL="http://127.0.0.1:$PORT/updates.xml"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST="$HOME/.focusos/blocker/dist"

PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 is required." >&2; exit 1; }

echo "==> 1/4  Packing the extension into a signed .crx"
EXT_ID="$EXT_ID" FOCUSOS_BLOCKER_PORT="$PORT" "$SCRIPT_DIR/focusos-blocker-pack.sh"

echo "==> 2/4  Installing the localhost dist server (launchd user agent)"
AGENT_LABEL="com.focusos.blocker-dist"
AGENT_PLIST="$HOME/Library/LaunchAgents/$AGENT_LABEL.plist"
mkdir -p "$HOME/Library/LaunchAgents"
cat > "$AGENT_PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$AGENT_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string>
    <string>-m</string><string>http.server</string>
    <string>$PORT</string>
    <string>--bind</string><string>127.0.0.1</string>
    <string>--directory</string><string>$DIST</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
</dict>
</plist>
EOF
# Reload the agent (bootout is best-effort; it errors if not already loaded).
launchctl bootout "gui/$(id -u)/$AGENT_LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$AGENT_PLIST"
# Give it a moment, then confirm the manifest is reachable.
for _ in 1 2 3 4 5; do
  if curl -sf -o /dev/null "$UPDATES_URL"; then break; fi
  sleep 0.5
done
if curl -sf -o /dev/null "$UPDATES_URL"; then
  echo "    serving $DIST at $UPDATES_URL"
else
  echo "    WARNING: $UPDATES_URL is not responding yet — check the launchd agent." >&2
fi

echo "==> 3/4  Registering the native-messaging host for Chromium browsers"
# Stage the app OUTSIDE the TCC-protected ~/Desktop/Documents/Downloads. A
# Dock-launched browser can be blocked from exec'ing a native host that lives
# there, so the extension would install but never receive the allowlist. We
# copy the built bundle to ~/Applications and point the host at that copy.
BUILT_APP="$REPO_ROOT/build/focusos.app"
STAGED_APP="$HOME/Applications/FocusOS.app"
HOST_BIN="$BUILT_APP/Contents/MacOS/focusos"
case "$REPO_ROOT/" in
  "$HOME/Desktop/"*|"$HOME/Documents/"*|"$HOME/Downloads/"*)
    if [ -d "$BUILT_APP" ]; then
      mkdir -p "$HOME/Applications"
      echo "    repo is under a TCC-protected folder — staging app to $STAGED_APP"
      ditto "$BUILT_APP" "$STAGED_APP"
      HOST_BIN="$STAGED_APP/Contents/MacOS/focusos"
    fi
    ;;
esac
FOCUSOS_BIN="$HOST_BIN" EXT_ID="$EXT_ID" "$REPO_ROOT/resources/focusos-blocker/host/install-host.sh"

echo "==> 4/4  Writing Brave managed policy (needs sudo)"
# Brave reads mandatory policy from the per-user file under /Library/Managed
# Preferences. We also write the system-wide file so it applies regardless of
# which short name Brave resolves. IncognitoModeAvailability=1 + guest off close
# the private-window bypass (the extension doesn't run in incognito by default).
POLICY_XML="$(cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>ExtensionInstallForcelist</key>
  <array><string>$EXT_ID;$UPDATES_URL</string></array>
  <key>ExtensionInstallSources</key>
  <array><string>http://127.0.0.1:$PORT/*</string></array>
  <key>ExtensionSettings</key>
  <dict>
    <key>$EXT_ID</key>
    <dict>
      <key>installation_mode</key><string>force_installed</string>
      <key>update_url</key><string>$UPDATES_URL</string>
      <key>toolbar_pin</key><string>force_pinned</string>
    </dict>
  </dict>
  <key>IncognitoModeAvailability</key><integer>1</integer>
  <key>BrowserGuestModeEnabled</key><false/>
</dict>
</plist>
EOF
)"
TMP_PLIST="$(mktemp -t com.brave.Browser.plist.XXXXXX)"
printf '%s\n' "$POLICY_XML" > "$TMP_PLIST"
plutil -lint "$TMP_PLIST" >/dev/null

MANAGED_DIR="/Library/Managed Preferences"
USER_MANAGED_DIR="$MANAGED_DIR/$(id -un)"
sudo mkdir -p "$MANAGED_DIR" "$USER_MANAGED_DIR"
sudo cp "$TMP_PLIST" "$MANAGED_DIR/com.brave.Browser.plist"
sudo cp "$TMP_PLIST" "$USER_MANAGED_DIR/com.brave.Browser.plist"
sudo chmod 644 "$MANAGED_DIR/com.brave.Browser.plist" "$USER_MANAGED_DIR/com.brave.Browser.plist"
rm -f "$TMP_PLIST"
# Flush the prefs cache so Brave sees the new managed policy on next launch.
sudo killall cfprefsd 2>/dev/null || true

cat <<EOF

Done. Now FULLY quit Brave (Cmd-Q) and reopen it.
Verify:
  • brave://policy       → ExtensionInstallForcelist / ExtensionSettings present
  • brave://extensions   → "FocusOS Blocker" installed (ID $EXT_ID), force-pinned
To undo: scripts/focusos-blocker-uninstall-macos.sh
EOF
