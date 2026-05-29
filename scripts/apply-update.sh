#!/usr/bin/env bash
# Bring the freshly-pulled tree live: rebuild FocusOS, repack the blocker
# extension (new version -> Brave auto-updates), and refresh the native-host
# registration. No sudo — the one-time privileged setup is done separately.
#
# This is what the post-merge git hook runs, so `git pull` is all it takes.
set -euo pipefail

REPO_ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$REPO_ROOT"

echo "▸ Building FocusOS…"
if [ ! -d build ]; then
  cmake -S . -B build >/dev/null
fi
cmake --build build

echo "▸ Packing blocker extension…"
if [ "$(uname -s)" = "Linux" ]; then
  bash scripts/focusos-blocker-pack.sh
else
  echo "  (skipped — extension self-hosting is Linux-only)"
fi

echo "▸ Refreshing native messaging host…"
bash resources/focusos-blocker/host/install-host.sh

if [ "$(uname -s)" = "Linux" ]; then
  echo "▸ Ensuring force-install policy is current…"
  # Idempotent — only prompts for sudo when a managed-policy target is missing
  # or stale (e.g. the first pull after a new Brave channel like brave-origin is
  # added). Best-effort: a no-tty / declined sudo must not abort the rebuild.
  bash scripts/focusos-policy-install.sh \
    || echo "  (skipped — run scripts/focusos-policy-install.sh manually if the extension isn't force-installed)"
fi

VERSION="4.9.$(git rev-list --count HEAD)"
echo "✓ FocusOS blocker updated to $VERSION — new rules are live."
