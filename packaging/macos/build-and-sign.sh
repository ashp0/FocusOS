#!/usr/bin/env bash
#
# Build FocusOS for macOS and ad-hoc codesign the app bundle with the Endpoint
# Security entitlement, so the AUTH_EXEC process blocker can run on a SIP-off
# research machine WITHOUT an Apple Developer account ($99/yr saved).
#
# This script does NOT touch SIP or boot-args — that's a one-time manual step
# you do in Recovery. See packaging/macos/README.md for the full walkthrough.
#
# Usage:
#   packaging/macos/build-and-sign.sh            # configure (if needed) + build + sign
#   packaging/macos/build-and-sign.sh --clean    # wipe build/ first
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
ENTITLEMENTS="${REPO_ROOT}/packaging/macos/focusos.entitlements"
APP_BUNDLE="${BUILD_DIR}/focusos.app"
BINARY="${APP_BUNDLE}/Contents/MacOS/focusos"

if [[ "${1:-}" == "--clean" ]]; then
    echo "==> Removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

# --- Configure (only if the build dir isn't already a CMake cache) ----------
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "==> Configuring CMake"
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

# --- Build ------------------------------------------------------------------
echo "==> Building focusos"
cmake --build "${BUILD_DIR}" --target focusos

if [[ ! -d "${APP_BUNDLE}" ]]; then
    echo "ERROR: expected app bundle at ${APP_BUNDLE} but it does not exist." >&2
    echo "       (Check that qt_add_executable kept MACOSX_BUNDLE.)" >&2
    exit 1
fi

# --- Sign -------------------------------------------------------------------
# IMPORTANT: com.apple.developer.endpoint-security.client is a *restricted*
# entitlement. AMFI lets it through only when SIP is off (and, on newer Apple
# Silicon, AMFI relaxed). If you embed it while SIP is ON, the kernel SIGKILLs the
# process the instant it launches ("zsh: killed", exit 137) — the app won't even
# start. So we embed the entitlement ONLY when SIP is actually disabled; with SIP
# on we sign ad-hoc without it, leaving a binary that runs fine (kiosk + pf
# firewall + app-quitting) minus the pre-emptive Endpoint Security blocker.
#
# Override with --force-entitlement if you've relaxed AMFI another way.
SIP_OFF=false
if csrutil status 2>/dev/null | grep -qi "disabled"; then
    SIP_OFF=true
fi
FORCE_ENTITLEMENT=false
for arg in "$@"; do
    [[ "${arg}" == "--force-entitlement" ]] && FORCE_ENTITLEMENT=true
done

if ${SIP_OFF} || ${FORCE_ENTITLEMENT}; then
    echo "==> SIP is disabled (or --force-entitlement): signing WITH the Endpoint Security entitlement"
    codesign --force --sign - \
             --entitlements "${ENTITLEMENTS}" \
             --identifier com.focusos.shell \
             "${APP_BUNDLE}"
    SIGNED_WITH_ES=true
else
    echo "==> SIP is ENABLED: signing WITHOUT entitlements"
    echo "    (a restricted ES entitlement under SIP-on makes the kernel SIGKILL the app"
    echo "     at launch — 'Killed: 9'. The app still runs with kiosk + pf firewall +"
    echo "     app-quitting; the pre-emptive ES blocker turns on once you disable SIP.)"
    codesign --force --sign - \
             --identifier com.focusos.shell \
             "${APP_BUNDLE}"
    SIGNED_WITH_ES=false
fi

echo
echo "==> Embedded entitlements:"
codesign -d --entitlements - "${APP_BUNDLE}" 2>&1 | grep -iE "endpoint|entitl" || echo "    (none — ES blocker disabled this build)"

echo
echo "Build + sign complete."
echo "  App:    ${APP_BUNDLE}"
echo "  Binary: ${BINARY}"
echo
echo "Run with the firewall + (if signed) the process blocker — needs root:"
echo "  sudo \"${BINARY}\""
if ! ${SIGNED_WITH_ES}; then
    echo
    echo "To enable the pre-emptive app-launch blocker, disable SIP (see"
    echo "packaging/macos/README.md) and re-run this script — it will then sign with"
    echo "the entitlement automatically."
fi
