#!/usr/bin/env bash
# One-time setup on the testing machine. After this, the workflow is just:
#   git pull   →   new build + blocking rules go live automatically.
#
# Steps: build + pack + register native host (apply-update.sh), force-install
# the extension via managed policy (sudo, once), and route git hooks to the
# version-controlled .githooks dir so post-merge fires on every pull.
set -euo pipefail

REPO_ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$REPO_ROOT"

echo "═══ FocusOS testing-machine setup ═══"

# The signing key is NOT in the repo (public). It must be hand-placed once; the
# ID stays pinned by it so this is a one-time copy, not a per-pull step.
PEM="resources/focusos-blocker/focusos-blocker.pem"
if [ ! -f "$PEM" ]; then
  echo "✗ Signing key missing: $PEM" >&2
  echo "  Copy it onto this machine once (it is gitignored and never changes):" >&2
  echo "    scp you@dev:/path/to/focusos-blocker.pem $REPO_ROOT/$PEM" >&2
  exit 1
fi

bash scripts/apply-update.sh

if [ "$(uname -s)" = "Linux" ]; then
  echo "▸ Installing force-install policy (needs sudo, one time)…"
  bash scripts/focusos-policy-install.sh
else
  echo "▸ Skipping force-install policy (Linux-only)."
fi

echo "▸ Routing git hooks to .githooks…"
git config core.hooksPath .githooks
chmod +x .githooks/* 2>/dev/null || true

echo ""
echo "✓ Setup complete. From now on: git pull is all you need."
