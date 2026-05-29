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
for b in brave-browser brave brave-browser-beta brave-browser-nightly \
         brave-origin brave-origin-beta brave-origin-nightly; do
  p="$(command -v "$b" 2>/dev/null)" && echo "  $b -> $p"
done
echo "  flatpak: $(flatpak list --columns=application 2>/dev/null | grep -i brave || echo none)"
echo "  snap:    $(snap list 2>/dev/null | awk 'NR>1{print $1}' | grep -i brave || echo none)"

sep "Browser profile dirs (where the host manifest must live)"
ls -d "$HOME"/.config/BraveSoftware/*/ 2>/dev/null || echo "  none under ~/.config/BraveSoftware"
if ls -d "$HOME"/.var/app/com.brave.Browser/config/BraveSoftware/*/ 2>/dev/null; then
  echo "  ^^^ FLATPAK sandbox — native messaging needs the manifest HERE, and the"
  echo "      host binary must be reachable from inside the sandbox. This breaks"
  echo "      the normal ~/.config registration."
fi

sep "Native-messaging host manifests actually on disk"
found=0
while IFS= read -r f; do
  found=1
  echo "  -- $f"
  sed 's/^/     /' "$f"
done < <(find "$HOME/.config" "$HOME/.var/app" -name "$HOST.json" 2>/dev/null)
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
for d in /etc/brave/policies/managed /etc/brave-origin*/policies/managed \
         /etc/chromium/policies/managed /etc/opt/chrome/policies/managed; do
  if [ -f "$d/focusos-blocker.json" ]; then
    echo "  -- $d/focusos-blocker.json"
    sed 's/^/     /' "$d/focusos-blocker.json" 2>/dev/null || echo "     (unreadable without sudo)"
  fi
done
find /etc -name "focusos-blocker.json" 2>/dev/null | sed 's/^/  found: /'

sep "Installed extension ID(s) seen in Brave profiles"
for pref in "$HOME"/.config/BraveSoftware/*/*/Preferences \
            "$HOME"/.config/BraveSoftware/*/*/Secure\ Preferences \
            "$HOME"/.var/app/com.brave.Browser/config/BraveSoftware/*/*/Preferences; do
  [ -f "$pref" ] || continue
  if grep -q "$EXT_ID" "$pref" 2>/dev/null; then
    echo "  pinned ID $EXT_ID present in: $pref"
  fi
done
echo "  (Compare against brave://extensions — Developer mode ON shows each ID.)"

sep "dist (self-hosted crx/updates.xml)"
ls -la "$HOME/.focusos/blocker/dist/" 2>/dev/null || echo "  no dist dir — focusos-blocker-pack.sh has not run."
echo "  -- updates.xml contents:"
sed 's/^/     /' "$HOME/.focusos/blocker/dist/updates.xml" 2>/dev/null || echo "     (none)"

sep "Local extension HTTP server (file:// URLs don't work — this must be up)"
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
systemctl --user status focusos-blocker-dist.service --no-pager 2>/dev/null | head -5 \
  || echo "  no systemd --user unit"
if command -v curl >/dev/null 2>&1; then
  if curl -sf -o /dev/null "http://127.0.0.1:$PORT/updates.xml"; then
    echo "  http://127.0.0.1:$PORT/updates.xml -> REACHABLE (good)"
  else
    echo "  http://127.0.0.1:$PORT/updates.xml -> NOT reachable (run scripts/focusos-blocker-serve.sh)"
  fi
fi

echo
echo "Doctor finished. Paste everything above."
