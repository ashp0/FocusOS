# Browser Blocker Operations

FocusOS uses a Chromium-family extension to enforce browser allowlists at the
navigation layer. This complements nftables: the firewall gives a system-level
backstop, while the extension lets allowed sites load their normal scripts,
images, media, and API subresources without opening the rest of the web.

## Components

1. Extension source: `resources/focusos-blocker/`.
2. Native messaging host: production mode is `focusos --native-host`; the
   development fallback script lives in `resources/focusos-blocker/host/`.
3. Signed policy: `~/.focusos/blocker/policy.dat`, written by FocusOS when a
   routine's network lock engages.
4. Local extension dist: `~/.focusos/blocker/dist/`, served from localhost for
   Chromium/Brave managed-extension updates.

The pinned extension ID is:

```text
gkbnapcbaflmaaoimfonclabmglfiden
```

## One-Time Testing-Machine Setup

The public repo does not contain the CRX signing key. Place it once at:

```text
resources/focusos-blocker/focusos-blocker.pem
```

Then run:

```bash
scripts/setup-testing-machine.sh
```

That script:

- Builds FocusOS.
- Packs the extension into a signed CRX.
- Starts or refreshes the local extension HTTP server.
- Registers the native messaging host.
- Installs managed browser policy on Linux.
- Points git hooks at `.githooks/` so post-merge updates run automatically.

Fully restart Brave/Chromium after the first setup so the browser loads the
policy and native-host manifest.

## Daily Update Workflow

After setup, the intended testing-machine update path is:

```bash
git pull
```

The post-merge hook runs `scripts/apply-update.sh`, which rebuilds FocusOS,
packs a higher-version CRX (`4.9.<commit-count>`), refreshes the local dist
server, and reinstalls the native-host manifest. The browser updates the
extension on its next check or restart.

## Extension Delivery

Chromium-family browsers refuse `file://` update URLs for force-installed
extensions. FocusOS therefore self-hosts the CRX and Omaha update manifest at:

```text
http://127.0.0.1:48217/updates.xml
```

Relevant scripts:

| Script | Purpose |
|--------|---------|
| `scripts/focusos-blocker-pack.sh` | Builds `focusos-blocker.crx` and `updates.xml`. |
| `scripts/focusos-blocker-serve.sh` | Runs a user systemd service for the localhost dist server. |
| `scripts/focusos-policy-install.sh` | Installs managed browser policy pointing at the localhost manifest. |
| `resources/focusos-blocker/host/install-host.sh` | Registers `com.focusos.blocker` native messaging host manifests. |
| `scripts/blocker-doctor.sh` | Reads browser, policy, host, log, and dist state for diagnostics. |

The policy installer writes to Brave, Brave Origin, Chromium, and Chrome policy
roots when present. The host installer scans Chromium-family profile locations
under `~/.config` and Flatpak-style paths where possible.

## Runtime Policy Flow

1. `RoutineManager` engages a routine and writes the active browser allowlist.
2. FocusOS serializes the policy into `policy.dat`.
3. The native host verifies the signed policy.
4. The host pushes a Chromium native-messaging frame to the extension.
5. The extension blocks top-frame and iframe navigations outside the allowlist.

Subresources are not blocked by the extension. For example, allowing
`youtube.com` permits the page navigation and lets the page fetch video streams,
thumbnails, scripts, and XHR from the domains the page normally needs.

## Tamper Resistance

`policy.dat` is not editable JSON. It is a signed binary container with:

- `FOSB` magic.
- Version.
- Serialized active flag, allowlist, and issue time.
- HMAC-SHA256 over the payload.

If the file is edited, corrupted, or deleted during an active routine, the
native host fails closed and blocks all browser navigations. With no active
routine, a missing or invalid policy allows browsing.

This is a practical tamper bar, not a cryptographic defense against someone who
can reverse engineer the local binary and patch their own tools.

## Presence Clamp

While a browser is connected, the native host refreshes:

```text
~/.focusos/blocker/host-alive
```

During a network-locked routine, the Linux watchdog checks this heartbeat. If a
Chromium-family browser is running after the extension has checked in once, but
the heartbeat goes stale, the watchdog assumes the blocker was disabled and
clamps network policy to full deny. Re-enabling the extension reconnects the
host and restores the routine allowlist.

Temporary debug escape hatch:

```bash
touch ~/.focusos/blocker/presence-check-off
```

Remove that file to re-enable presence enforcement.

## Diagnostics

Run:

```bash
scripts/blocker-doctor.sh
```

Useful files and checks:

- `~/.focusos/blocker/host.log` - native-host connections and policy pushes.
- `~/.focusos/blocker/host-alive` - current heartbeat.
- `~/.focusos/blocker/policy.dat` - signed policy existence and timestamp.
- `~/.focusos/blocker/dist/updates.xml` - CRX update manifest.
- `systemctl --user status focusos-blocker-dist.service` - local HTTP server.
- Browser `extensions` page with Developer mode enabled - confirms the pinned ID.

Confirm the local manifest is reachable:

```bash
curl -sf http://127.0.0.1:48217/updates.xml
```

For protocol and source-tree details, see
[resources/focusos-blocker/README.md](../resources/focusos-blocker/README.md).
