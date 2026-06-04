#!/usr/bin/env bash
# Revive macOS Aqua UI agents that FocusOS's (legacy) SIP-off lockdown could have
# disabled + booted out of launchd — most importantly the Dock, which also powers
# ⌘-Tab (the app switcher) and Mission Control.
#
# The important bit: a service that was `launchctl bootout`ed is DEAD. `enable` +
# `kickstart` alone cannot bring it back — it must first be `bootstrap`ed from its
# plist back into the GUI domain. This script does that for every known UI agent
# that is not currently running, and leaves healthy ones untouched. Safe to run
# repeatedly.
set -euo pipefail

[ "$(uname -s)" = "Darwin" ] || { echo "macOS only." >&2; exit 1; }

UID_VALUE="$(id -u)"
DOMAIN="gui/$UID_VALUE"
AGENT_DIR="/System/Library/LaunchAgents"

# The agents FocusOS's lockdown could ever touch.
LABELS="
com.apple.Dock.agent
com.apple.Finder
com.apple.Spotlight
com.apple.SystemUIServer.agent
com.apple.controlcenter
com.apple.notificationcenterui.agent
com.apple.Siri.agent
com.apple.talagent
"

# Resolve a label to its LaunchAgent plist. Usually the label with a trailing
# ".agent" stripped (com.apple.Dock.agent -> com.apple.Dock.plist), but a few keep
# the full label (com.apple.Siri.agent -> com.apple.Siri.agent.plist).
plist_for_label() {
  local label="$1"
  if [ -f "$AGENT_DIR/$label.plist" ]; then
    echo "$AGENT_DIR/$label.plist"; return 0
  fi
  local stripped="${label%.agent}"
  if [ -f "$AGENT_DIR/$stripped.plist" ]; then
    echo "$AGENT_DIR/$stripped.plist"; return 0
  fi
  echo ""
}

echo "Reviving FocusOS-affected macOS UI agents…"
for label in $LABELS; do
  [ -n "$label" ] || continue
  launchctl enable "$DOMAIN/$label" 2>/dev/null || true
  if launchctl print "$DOMAIN/$label" >/dev/null 2>&1; then
    echo "  $label: already running"
    continue
  fi
  plist="$(plist_for_label "$label")"
  if [ -n "$plist" ]; then
    launchctl bootstrap "$DOMAIN" "$plist" 2>/dev/null || true
  fi
  launchctl kickstart -k "$DOMAIN/$label" 2>/dev/null || true
  echo "  $label: revived"
done

# Clear any leftover lockdown state file (no longer used; kept tidy).
rm -f "${FOCUSOS_UI_LOCKDOWN_STATE:-$HOME/.focusos/macos-ui-lockdown.state}"
echo "Done. The Dock, app switcher (⌘-Tab) and Mission Control should be back."
