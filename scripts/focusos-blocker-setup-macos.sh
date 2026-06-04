#!/usr/bin/env bash
# macOS blocker setup. Unlike Linux, macOS/Chromium HARD-GATE force-installing an
# extension from a non-Web-Store update URL behind MDM/DEP enrollment ("This
# computer is not detected as enterprise managed…"). A manually-installed
# configuration profile is NOT MDM enrollment, so the self-hosted force-install
# the Linux pipeline uses simply will not run on a normal personal Mac.
#
# So on macOS the model is Cold-Turkey-style: the user installs the unpacked
# extension by hand once, and FocusOS does everything else. This script:
#   1. Stages a clean UNPACKED copy of the extension to ~/.focusos/blocker/extension
#      (no private .pem / host scripts). The manifest's pinned `key` makes it load
#      with the SAME id the native host expects, so "Load unpacked" just works.
#   2. Registers the com.focusos.blocker native-messaging host for every
#      Chromium-family browser present (host/install-host.sh).
#   3. Generates + opens a Brave "hardening" configuration profile (Incognito /
#      Guest off, so a session can't be bypassed in a private window). Approve it
#      once in System Settings.
#   4. Prints the one-time "Load unpacked" steps.
#
# Then the user enables Developer mode in brave://extensions and Loads the staged
# folder. The extension connects to `focusos --native-host`, and during a
# network-locked routine FocusOS's presence clamp slams pf shut if the extension
# is ever disabled.
#
# MDM machines only: set INCLUDE_FORCELIST=1 to ALSO pack a signed CRX, run the
# localhost dist server, and emit force-install policy keys in the profile — the
# original always-on pipeline, which only works once `profiles status -type
# enrollment` reports the Mac is MDM/DEP managed.
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
EXT_STAGE="$HOME/.focusos/blocker/extension"
INCLUDE_FORCELIST="${INCLUDE_FORCELIST:-0}"

PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 is required." >&2; exit 1; }

echo "==> 1/4  Staging the unpacked extension to $EXT_STAGE"
# Exclude the private signing key, the native-host scripts, and dev cruft — only
# the loadable extension files go into the user-facing folder.
rm -rf "$EXT_STAGE"
mkdir -p "$EXT_STAGE"
rsync -a \
  --exclude='.DS_Store' \
  --exclude='.manifest-key.txt' \
  --exclude='focusos-blocker.pem' \
  --exclude='host/' \
  --exclude='README.md' \
  "$REPO_ROOT/resources/focusos-blocker/" "$EXT_STAGE/"
if ls "$EXT_STAGE"/*.pem >/dev/null 2>&1; then
  echo "    REFUSING TO CONTINUE: a .pem leaked into the staged extension." >&2
  exit 1
fi
echo "    staged $(ls -1 "$EXT_STAGE" | wc -l | tr -d ' ') files (pinned id: $EXT_ID)"

echo "==> 2/4  Registering the native-messaging host for Chromium browsers"
# Stage the app OUTSIDE the TCC-protected ~/Desktop/Documents/Downloads. A
# Dock-launched browser can be blocked from exec'ing a native host that lives
# there, so the extension would load but never receive the allowlist. We copy the
# built bundle to ~/Applications and point the host at that copy.
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

if [ "$INCLUDE_FORCELIST" = "1" ]; then
  echo "==> (MDM) Packing a signed CRX + running the localhost dist server"
  EXT_ID="$EXT_ID" FOCUSOS_BLOCKER_PORT="$PORT" "$SCRIPT_DIR/focusos-blocker-pack.sh"
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
  launchctl bootout "gui/$(id -u)/$AGENT_LABEL" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$AGENT_PLIST"
fi

echo "==> 3/4  Generating + opening the Brave hardening profile"
# Clamp-only by default (Incognito/Guest off). With INCLUDE_FORCELIST=1 it ALSO
# carries force-install keys — only honored on an MDM-managed Mac.
PROFILE_ID="com.focusos.blocker.brave-policy"
PROFILE_OUT="$HOME/.focusos/blocker/FocusOS-Blocker.mobileconfig"
EXT_ID="$EXT_ID" FOCUSOS_BLOCKER_PORT="$PORT" UPDATES_URL="$UPDATES_URL" \
  OUT="$PROFILE_OUT" PROFILE_ID="$PROFILE_ID" INCLUDE_FORCELIST="$INCLUDE_FORCELIST" \
  "$SCRIPT_DIR/focusos-blocker-profile.sh"
# macOS 26 removed CLI profile installs; replace any prior one then open it for
# approval. `profiles remove` still works, so a stale force-install profile (the
# one that showed [BLOCKED] in brave://policy) is cleared first.
sudo profiles remove -identifier "$PROFILE_ID" 2>/dev/null || true
open "$PROFILE_OUT" || true

echo "==> 4/4  Manual install (one time)"
cat <<EOF

The extension is installed BY HAND on macOS (force-install needs MDM, which a
personal Mac doesn't have). Two one-time steps:

  1) Approve the profile that just opened:
       System Settings → General → Device Management
       → "FocusOS Blocker — Brave Hardening" → Install
     (Disables private/guest windows so a focus session can't be bypassed there.)

  2) Load the extension in Brave (any channel):
       brave://extensions → turn ON "Developer mode" (top-right)
       → "Load unpacked" → choose:
         $EXT_STAGE

It loads as "FocusOS Blocker" (id $EXT_ID) and connects to the native host
automatically. brave://policy should show NO [BLOCKED] entry.

Verify the connection any time with:  scripts/blocker-doctor.sh
To undo: scripts/focusos-blocker-uninstall-macos.sh
EOF
