#!/usr/bin/env bash
# Register the FocusOS Blocker native messaging host for every Chromium-family
# browser found on this machine (Chrome, Brave, Chromium, Edge).
#
# The host is now the focusos binary itself, run in `--native-host` mode. Since
# a native-messaging manifest's `path` can't carry arguments, we install a tiny
# exec wrapper and point the manifests at it.
#
# Run AFTER you have dev-loaded the unpacked extension, because the extension ID
# must match. The default below is the ID derived from the bundled manifest key
# (resources/focusos-blocker/.manifest-key.txt). Override with: EXT_ID=... ./install-host.sh
#
# The focusos binary is located via FOCUSOS_BIN, else the usual build outputs.
# Override with: FOCUSOS_BIN=/path/to/focusos ./install-host.sh
set -euo pipefail

HOST_NAME="com.focusos.blocker"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"

# Resolve the focusos binary.
FOCUSOS_BIN="${FOCUSOS_BIN:-}"
if [ -z "$FOCUSOS_BIN" ]; then
  for candidate in \
    "$REPO_ROOT/build/focusos" \
    "$REPO_ROOT/build/focusos.app/Contents/MacOS/focusos" \
    "$(command -v focusos || true)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
      FOCUSOS_BIN="$candidate"
      break
    fi
  done
fi
if [ -z "$FOCUSOS_BIN" ] || [ ! -x "$FOCUSOS_BIN" ]; then
  echo "Could not find the focusos binary. Build it, or set FOCUSOS_BIN=/path/to/focusos." >&2
  exit 1
fi

# The wrapper (and the host process it execs) must live OUTSIDE TCC-protected
# folders (~/Desktop, ~/Documents, ~/Downloads). A browser launched from
# Finder/Dock can't exec there without an explicit per-app grant, so
# connectNative silently fails to launch the host. We write the wrapper into the
# stable FocusOS data dir and register the manifests against it.
HOST_DIR="$HOME/.focusos/blocker"
WRAPPER_PATH="$HOST_DIR/focusos-blocker-host"
mkdir -p "$HOST_DIR"
cat > "$WRAPPER_PATH" <<EOF
#!/bin/sh
exec "$FOCUSOS_BIN" --native-host "\$@"
EOF
chmod +x "$WRAPPER_PATH"

manifest_json() {
  cat <<EOF
{
  "name": "$HOST_NAME",
  "description": "FocusOS Blocker native host",
  "path": "$WRAPPER_PATH",
  "type": "stdio",
  "allowed_origins": [
    "chrome-extension://$EXT_ID/"
  ]
}
EOF
}

case "$(uname -s)" in
  Darwin)
    BASE="$HOME/Library/Application Support"
    TARGET_DIRS=(
      "$BASE/Google/Chrome/NativeMessagingHosts"
      "$BASE/Google/Chrome Beta/NativeMessagingHosts"
      "$BASE/Google/Chrome Dev/NativeMessagingHosts"
      "$BASE/Google/Chrome Canary/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser-Beta/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser-Dev/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser-Nightly/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Origin-Nightly/NativeMessagingHosts"
      "$BASE/Chromium/NativeMessagingHosts"
      "$BASE/Microsoft Edge/NativeMessagingHosts"
    ) ;;
  Linux)
    BASE="$HOME/.config"
    TARGET_DIRS=(
      "$BASE/google-chrome/NativeMessagingHosts"
      "$BASE/google-chrome-beta/NativeMessagingHosts"
      "$BASE/google-chrome-unstable/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser-Beta/NativeMessagingHosts"
      "$BASE/BraveSoftware/Brave-Browser-Nightly/NativeMessagingHosts"
      "$BASE/chromium/NativeMessagingHosts"
      "$BASE/microsoft-edge/NativeMessagingHosts"
    ) ;;
  *)
    echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac

echo "Extension ID: $EXT_ID"
echo "focusos bin:  $FOCUSOS_BIN"
echo "Host wrapper: $WRAPPER_PATH"
installed=0
for dir in "${TARGET_DIRS[@]}"; do
  parent="$(dirname "$dir")"
  # Only install for browsers that are actually present on this machine.
  if [ -d "$parent" ]; then
    mkdir -p "$dir"
    manifest_json > "$dir/$HOST_NAME.json"
    echo "  installed -> $dir/$HOST_NAME.json"
    installed=$((installed + 1))
  fi
done

if [ "$installed" -eq 0 ]; then
  echo "No Chromium-family browsers found. Nothing installed." >&2
  exit 1
fi
echo "Done. Restart the browser if it was already running."
