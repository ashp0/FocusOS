#!/usr/bin/env bash
# Generate the FocusOS Blocker macOS *configuration profile* (.mobileconfig).
#
# Why a profile instead of poking /Library/Managed Preferences directly:
# Chromium-family browsers read mandatory policy from their managed-preferences
# domain (com.brave.Browser, com.brave.Browser.beta, ...). Those files are owned
# by cfprefsd and any *other* configuration profile that manages the same domain
# (e.g. a separate Brave-hardening profile) regenerates them — silently dropping
# keys we wrote by hand. A profile is the only delivery that coexists: macOS
# merges all installed profiles' payloads for a domain, so the FocusOS
# force-install survives alongside an unrelated hardening profile.
#
# This emits one payload per Brave channel domain so the policy applies whichever
# Brave the user opens (release / Beta / Dev / Nightly).
#
# IMPORTANT — force-install is MDM-gated on macOS. Brave/Chromium refuse to
# auto-install an extension from a non-Web-Store update URL unless the Mac is
# enrolled in MDM/DEP ("This computer is not detected as enterprise managed").
# A manually-installed configuration profile is NOT MDM enrollment, so the
# force-install keys just get stamped [BLOCKED] and report "Invalid extension
# ID". We therefore DEFAULT to a clamp-only profile (Incognito/Guest off) and the
# user installs the unpacked extension by hand (Cold-Turkey style). Set
# INCLUDE_FORCELIST=1 to also emit the force-install keys — only useful once the
# Mac is genuinely MDM-managed.
#
# Output: $OUT (default ~/.focusos/blocker/FocusOS-Blocker.mobileconfig).
# Stable PayloadIdentifier + deterministic UUIDs mean reinstalling the profile
# REPLACES the previous one instead of stacking duplicates.
set -euo pipefail

[ "$(uname -s)" = "Darwin" ] || { echo "macOS only." >&2; exit 1; }

EXT_ID="${EXT_ID:-gkbnapcbaflmaaoimfonclabmglfiden}"
PORT="${FOCUSOS_BLOCKER_PORT:-48217}"
UPDATES_URL="${UPDATES_URL:-http://127.0.0.1:$PORT/updates.xml}"
SOURCE_URL="${SOURCE_URL:-http://127.0.0.1:$PORT/*}"
OUT="${OUT:-$HOME/.focusos/blocker/FocusOS-Blocker.mobileconfig}"
PROFILE_ID="${PROFILE_ID:-com.focusos.blocker.brave-policy}"
# Force-install keys are off by default — they only work on an MDM-managed Mac.
INCLUDE_FORCELIST="${INCLUDE_FORCELIST:-0}"

# The Brave channels we force the extension into. The managed-preferences domain
# must equal each channel app's CFBundleIdentifier. We list the standard set so a
# channel works the moment it is installed, even if it isn't present right now.
# Discover any installed Brave's real bundle id too, in case a build uses a
# domain not in the static list.
BRAVE_DOMAINS=(
  "com.brave.Browser"          # release
  "com.brave.Browser.beta"     # Beta
  "com.brave.Browser.dev"      # Dev
  "com.brave.Browser.nightly"  # Nightly / Origin-Nightly
)
shopt -s nullglob
for app in /Applications/Brave\ Browser*.app "$HOME"/Applications/Brave\ Browser*.app; do
  [ -d "$app" ] || continue
  bid="$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist" 2>/dev/null || true)"
  [ -n "$bid" ] && BRAVE_DOMAINS+=("$bid")
done
shopt -u nullglob

PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 is required." >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"

EXT_ID="$EXT_ID" UPDATES_URL="$UPDATES_URL" SOURCE_URL="$SOURCE_URL" \
PROFILE_ID="$PROFILE_ID" OUT="$OUT" DOMAINS="${BRAVE_DOMAINS[*]}" \
INCLUDE_FORCELIST="$INCLUDE_FORCELIST" \
"$PY" - <<'PY'
import os, plistlib, uuid

ext_id      = os.environ["EXT_ID"]
updates_url = os.environ["UPDATES_URL"]
source_url  = os.environ["SOURCE_URL"]
profile_id  = os.environ["PROFILE_ID"]
out_path    = os.environ["OUT"]
include_forcelist = os.environ.get("INCLUDE_FORCELIST", "0") == "1"
# De-dup the domain list while preserving order.
seen, domains = set(), []
for d in os.environ["DOMAINS"].split():
    if d and d not in seen:
        seen.add(d); domains.append(d)

# Deterministic UUIDs so reinstall replaces rather than stacks.
NS = uuid.uuid5(uuid.NAMESPACE_DNS, "blocker.focusos")
def stable_uuid(name):
    return str(uuid.uuid5(NS, f"{profile_id}:{name}")).upper()

def channel_payload(domain):
    payload = {
        "PayloadType": domain,
        "PayloadVersion": 1,
        "PayloadEnabled": True,
        "PayloadIdentifier": f"{profile_id}.{domain}",
        "PayloadUUID": stable_uuid(domain),
        "PayloadDisplayName": f"FocusOS Blocker policy ({domain})",
        # Close the obvious private-window bypass. Browser policy, not profile
        # prefs — does not touch search engine / profile defaults. This works
        # WITHOUT enterprise management, so it is always emitted.
        "IncognitoModeAvailability": 1,
        "BrowserGuestModeEnabled": False,
    }
    if include_forcelist:
        # Only honored on an MDM-managed Mac; otherwise Brave stamps [BLOCKED].
        payload["ExtensionInstallForcelist"] = [f"{ext_id};{updates_url}"]
        payload["ExtensionInstallSources"] = [source_url]
        payload["ExtensionSettings"] = {
            ext_id: {
                "installation_mode": "force_installed",
                "update_url": updates_url,
                "toolbar_pin": "force_pinned",
            }
        }
    return payload

profile = {
    "PayloadType": "Configuration",
    "PayloadVersion": 1,
    "PayloadIdentifier": profile_id,
    "PayloadUUID": stable_uuid("root"),
    "PayloadDisplayName": (
        "FocusOS Blocker — Brave Force-Install" if include_forcelist
        else "FocusOS Blocker — Brave Hardening"
    ),
    "PayloadDescription": (
        ("Force-installs and pins the FocusOS Blocker extension in every Brave "
         "channel and clamps Incognito/Guest. Managed by FocusOS.")
        if include_forcelist else
        ("Clamps Incognito/Guest off in every Brave channel so a focus session "
         "cannot be bypassed in a private window. The FocusOS Blocker extension "
         "itself is installed manually. Managed by FocusOS.")
    ),
    "PayloadOrganization": "FocusOS",
    "PayloadScope": "System",
    "PayloadRemovalDisallowed": False,
    "PayloadContent": [channel_payload(d) for d in domains],
}

with open(out_path, "wb") as fh:
    plistlib.dump(profile, fh, fmt=plistlib.FMT_XML, sort_keys=True)
print(out_path)
print("channels: " + ", ".join(domains))
PY

# Validate the emitted profile parses cleanly.
plutil -lint "$OUT" >/dev/null
echo "Wrote $OUT"
