#!/usr/bin/env bash
# Pack the FocusOS Blocker extension into a self-hosted .crx + update manifest
# under ~/.focusos/blocker/dist, ready for force-install via managed policy.
#
# The version is derived from the git commit count (4.9.<N>) so every push
# yields a strictly higher version and Brave/Chromium detects the update. The
# extension's embedded `key` keeps the ID pinned regardless of version.
set -euo pipefail

EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
# Chromium/Brave refuse file:// update URLs for force-installed extensions, so
# the crx + update manifest are served over a localhost HTTP origin (trusted as
# a secure context). This port MUST match focusos-blocker-serve.sh and
# focusos-policy-install.sh.
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
BASE_URL="http://127.0.0.1:$PORT"
REPO_ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
EXT_SRC="$REPO_ROOT/resources/focusos-blocker"
PEM="$EXT_SRC/focusos-blocker.pem"

DIST_DIR="$HOME/.focusos/blocker/dist"
CRX_PATH="$DIST_DIR/focusos-blocker.crx"
UPDATES_PATH="$DIST_DIR/updates.xml"

if [ ! -f "$PEM" ]; then
  echo "Signing key not found: $PEM" >&2
  exit 1
fi

# Locate a Chromium-family browser to do the packing (it exits without a window).
BROWSER=""
for cand in brave-browser brave chromium chromium-browser google-chrome google-chrome-stable; do
  if command -v "$cand" >/dev/null 2>&1; then BROWSER="$(command -v "$cand")"; break; fi
done
if [ -z "$BROWSER" ]; then
  echo "No Chromium-family browser found to pack the .crx (need brave/chromium/chrome)." >&2
  exit 1
fi

VERSION="4.9.$(git -C "$REPO_ROOT" rev-list --count HEAD)"
mkdir -p "$DIST_DIR"

# Stage the extension WITHOUT the private key or host tooling, then stamp the
# derived version into the staged manifest (source manifest stays untouched).
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
( cd "$EXT_SRC" && tar --exclude='host' --exclude='*.pem' --exclude='.manifest-key.txt' \
    -cf - . ) | ( cd "$STAGE" && tar -xf - )
python3 - "$STAGE/manifest.json" "$VERSION" "$BASE_URL" <<'PY'
import json, sys
path, version, base_url = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as fh:
    data = json.load(fh)
data["version"] = version
data["update_url"] = base_url + "/updates.xml"
with open(path, "w") as fh:
    json.dump(data, fh, indent=2)
PY

# --pack-extension with an existing key reuses the key (pinned ID) and writes
# <stage>.crx in the stage's parent directory.
"$BROWSER" --pack-extension="$STAGE" --pack-extension-key="$PEM" \
  --no-message-box >/dev/null 2>&1 || true
PACKED="${STAGE}.crx"
if [ ! -f "$PACKED" ]; then
  echo "Packing failed — no .crx produced at $PACKED" >&2
  exit 1
fi
mv -f "$PACKED" "$CRX_PATH"
rm -f "${STAGE}.pem" 2>/dev/null || true

cat > "$UPDATES_PATH" <<EOF
<?xml version='1.0' encoding='UTF-8'?>
<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>
  <app appid='$EXT_ID'>
    <updatecheck codebase='$BASE_URL/focusos-blocker.crx' version='$VERSION' />
  </app>
</gupdate>
EOF

echo "Packed FocusOS Blocker $VERSION"
echo "  crx:     $CRX_PATH"
echo "  updates: $UPDATES_PATH"
echo "  served at: $BASE_URL/updates.xml"
