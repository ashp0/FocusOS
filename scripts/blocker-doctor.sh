#!/usr/bin/env bash
# blocker-doctor.sh — diagnose why the FocusOS Blocker extension isn't talking
# to its native host (the "extension is disabled or missing" nag).
#
# Run it and paste the WHOLE output back. Nothing here changes anything — it
# only reads. No sudo needed (a couple of /etc listings may say "permission
# denied"; that's fine).
EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
HOST="com.focusos.blocker"
sep() { printf '\n=== %s ===\n' "$1"; }

sep "OS"
uname -a

sep "Brave binaries / install type"
if [ "$(uname -s)" = "Darwin" ]; then
  for app in /Applications/Brave*.app "$HOME"/Applications/Brave*.app; do
    [ -d "$app" ] || continue
    bid="$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist" 2>/dev/null || echo unknown)"
    echo "  $app ($bid)"
  done
else
  for b in brave-browser brave brave-browser-beta brave-browser-nightly \
           brave-origin brave-origin-beta brave-origin-nightly; do
    p="$(command -v "$b" 2>/dev/null)" && echo "  $b -> $p"
  done
  echo "  flatpak: $(flatpak list --columns=application 2>/dev/null | grep -i brave || echo none)"
  echo "  snap:    $(snap list 2>/dev/null | awk 'NR>1{print $1}' | grep -i brave || echo none)"
fi

sep "Browser profile dirs (where the host manifest must live)"
if [ "$(uname -s)" = "Darwin" ]; then
  ls -d "$HOME"/Library/Application\ Support/BraveSoftware/*/ 2>/dev/null \
    || echo "  none under ~/Library/Application Support/BraveSoftware"
else
  ls -d "$HOME"/.config/BraveSoftware/*/ 2>/dev/null || echo "  none under ~/.config/BraveSoftware"
  if ls -d "$HOME"/.var/app/com.brave.Browser/config/BraveSoftware/*/ 2>/dev/null; then
    echo "  ^^^ FLATPAK sandbox — native messaging needs the manifest HERE, and the"
    echo "      host binary must be reachable from inside the sandbox. This breaks"
    echo "      the normal ~/.config registration."
  fi
fi

sep "Native-messaging host manifests actually on disk"
found=0
if [ "$(uname -s)" = "Darwin" ]; then
  SEARCH_ROOTS=("$HOME/Library/Application Support")
else
  SEARCH_ROOTS=("$HOME/.config" "$HOME/.var/app")
fi
while IFS= read -r f; do
  found=1
  echo "  -- $f"
  sed 's/^/     /' "$f"
done < <(find "${SEARCH_ROOTS[@]}" -name "$HOST.json" 2>/dev/null)
[ "$found" -eq 0 ] && echo "  NONE found — the browser has no way to launch the host."

sep "Host wrapper + target binary"
W="$HOME/.focusos/blocker/focusos-blocker-host"
if [ -e "$W" ]; then
  ls -la "$W"
  echo "  contents:"; sed 's/^/     /' "$W"
  BIN="$(awk '/--native-host/{print $2}' "$W" | tr -d '"' | head -1)"
  echo "  -> target binary: $BIN"
  [ -x "$BIN" ] && echo "     exists & executable: yes" || echo "     exists & executable: NO"
else
  echo "  $W does not exist — install-host.sh has not run successfully."
fi

sep "host.log — did the browser EVER spawn the host?"
if [ -f "$HOME/.focusos/blocker/host.log" ]; then
  tail -25 "$HOME/.focusos/blocker/host.log"
else
  echo "  no host.log — the host has NEVER been launched. The extension is not"
  echo "  connecting (manifest missing/wrong, extension not installed, or ID mismatch)."
fi

sep "Heartbeat beacon"
ls -la "$HOME/.focusos/blocker/host-alive" 2>/dev/null \
  || echo "  no host-alive file — host not running right now."

sep "Signed policy"
ls -la "$HOME/.focusos/blocker/policy.dat" 2>/dev/null || echo "  no policy.dat"

sep "Force-install managed policy (is the extension even installed?)"
if [ "$(uname -s)" = "Darwin" ]; then
  # Primary delivery on macOS is now a configuration profile, not a hand-written
  # managed-prefs file (a hardening profile would clobber the latter).
  PROFILE_OUT="$HOME/.focusos/blocker/FocusOS-Blocker.mobileconfig"
  if [ -f "$PROFILE_OUT" ]; then
    echo "  -- profile generated: $PROFILE_OUT"
    plutil -p "$PROFILE_OUT" 2>/dev/null | grep -E "PayloadType|ExtensionInstallForcelist|$EXT_ID" | sed 's/^/     /'
  else
    echo "  -- no FocusOS-Blocker.mobileconfig generated yet (run focusos-blocker-setup-macos.sh)"
  fi
  if command -v profiles >/dev/null 2>&1; then
    if profiles list 2>/dev/null | grep -qi "com.focusos.blocker.brave-policy"; then
      echo "  -- profile INSTALLED (profiles list shows com.focusos.blocker.brave-policy)"
    else
      echo "  -- profile not shown installed (need sudo, or approve it in System Settings → Device Management)"
    fi
  fi
  # The resulting per-channel managed-prefs files (merged by macOS from all
  # installed profiles). These are read-only output of the profile layer.
  for p in "/Library/Managed Preferences"/com.brave*.plist \
           "/Library/Managed Preferences/$(id -un)"/com.brave*.plist; do
    [ -f "$p" ] || continue
    echo "  -- $p"
    plutil -p "$p" 2>/dev/null | sed 's/^/     /' | grep -E "ExtensionInstall|ExtensionSettings|Incognito|Guest|$EXT_ID" \
      || echo "     (no FocusOS/extension policy keys found)"
  done
else
  for d in /etc/brave/policies/managed /etc/brave-origin*/policies/managed \
           /etc/chromium/policies/managed /etc/opt/chrome/policies/managed; do
    if [ -f "$d/focusos-blocker.json" ]; then
      echo "  -- $d/focusos-blocker.json"
      sed 's/^/     /' "$d/focusos-blocker.json" 2>/dev/null || echo "     (unreadable without sudo)"
    fi
  done
  find /etc -name "focusos-blocker.json" 2>/dev/null | sed 's/^/  found: /'
fi

sep "Installed extension ID(s) seen in Brave profiles"
if [ "$(uname -s)" = "Darwin" ]; then
  PREF_GLOBS=("$HOME"/Library/Application\ Support/BraveSoftware/*/*/Preferences
              "$HOME"/Library/Application\ Support/BraveSoftware/*/*/Secure\ Preferences)
else
  PREF_GLOBS=("$HOME"/.config/BraveSoftware/*/*/Preferences
              "$HOME"/.config/BraveSoftware/*/*/Secure\ Preferences
              "$HOME"/.var/app/com.brave.Browser/config/BraveSoftware/*/*/Preferences)
fi
for pref in "${PREF_GLOBS[@]}"; do
  [ -f "$pref" ] || continue
  if grep -q "$EXT_ID" "$pref" 2>/dev/null; then
    echo "  pinned ID $EXT_ID present in: $pref"
  fi
done
found_ext=0
if [ "$(uname -s)" = "Darwin" ]; then
  while IFS= read -r d; do found_ext=1; echo "  installed dir: $d"; done \
    < <(find "$HOME/Library/Application Support/BraveSoftware" -path "*/Extensions/$EXT_ID" -type d 2>/dev/null)
else
  while IFS= read -r d; do found_ext=1; echo "  installed dir: $d"; done \
    < <(find "$HOME/.config/BraveSoftware" "$HOME/.var/app" -path "*/Extensions/$EXT_ID" -type d 2>/dev/null)
fi
[ "$found_ext" -eq 0 ] && echo "  no installed Extensions/$EXT_ID directory found in Brave profiles"
echo "  (Compare against brave://extensions — Developer mode ON shows each ID.)"

sep "dist (self-hosted crx/updates.xml)"
ls -la "$HOME/.focusos/blocker/dist/" 2>/dev/null || echo "  no dist dir — focusos-blocker-pack.sh has not run."
echo "  -- updates.xml contents:"
sed 's/^/     /' "$HOME/.focusos/blocker/dist/updates.xml" 2>/dev/null || echo "     (none)"

sep "Local extension HTTP server (file:// URLs don't work — this must be up)"
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
if [ "$(uname -s)" = "Darwin" ]; then
  launchctl print "gui/$(id -u)/com.focusos.blocker-dist" 2>/dev/null | head -20 \
    || echo "  no launchd agent com.focusos.blocker-dist"
else
  systemctl --user status focusos-blocker-dist.service --no-pager 2>/dev/null | head -5 \
    || echo "  no systemd --user unit"
fi
if command -v curl >/dev/null 2>&1; then
  if curl -sf -o /dev/null "http://127.0.0.1:$PORT/updates.xml"; then
    echo "  http://127.0.0.1:$PORT/updates.xml -> REACHABLE (good)"
  else
    echo "  http://127.0.0.1:$PORT/updates.xml -> NOT reachable (run scripts/focusos-blocker-serve.sh)"
  fi
fi

echo
echo "Doctor finished. Paste everything above."
