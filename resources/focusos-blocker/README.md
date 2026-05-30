# FocusOS Blocker Component

This directory contains the FocusOS-controlled fork of the Cold Turkey browser
extension. The upstream snapshot is kept separately in `resources/Cold Turkey/`
so extension changes can be reviewed against the original source.

For installation, local CRX delivery, managed browser policy, and diagnostics,
use [../../docs/browser-blocker.md](../../docs/browser-blocker.md).

## Job

During a FocusOS routine, the extension blocks browser document navigations
outside the active allowlist. It intentionally leaves subresources alone:
scripts, images, XHR, video segments, and other page assets can load when the
top-level site itself is allowed.

This avoids the brittle CDN bookkeeping that comes from trying to express
modern websites as firewall-only host lists.

## Source Tree

| Path | Purpose |
|------|---------|
| `manifest.json` | Chromium extension manifest with the pinned key/ID. |
| `ctBackground.js` | Service worker and navigation-blocking engine. |
| `ctContent.js` | Content-side behavior inherited from the fork base. |
| `ctMenu.*` | Minimal extension UI surface. |
| `blocked.html`, `blocked.js` | Offline blocked page shown for denied navigations. |
| `host/` | Development native-host script, allowlist file, and installer. |
| `assets/`, icons, fonts | Static extension assets. |

Pinned extension ID:

```text
gkbnapcbaflmaaoimfonclabmglfiden
```

## Architecture

```text
Chromium / Brave / Chrome / Edge
  FocusOS Blocker extension
    chrome.runtime.connectNative("com.focusos.blocker")
      Native messaging host
        focusos --native-host
          signed policy from ~/.focusos/blocker/policy.dat
```

The extension is only the enforcer. It does not own the allowlist. The native
host verifies the signed policy written by FocusOS and pushes the current
policy to the extension over the native-messaging port.

The Python host in `host/focusos_blocker_host.py` remains a development
backstop. Production should use `focusos --native-host` so policy parsing,
signature verification, and runtime behavior match the app.

## Native Messaging Protocol

Chromium native messaging uses:

- 4-byte little-endian payload length.
- UTF-8 JSON payload.
- Framed messages on stdout only.

Never write logs to stdout from a native host. Use stderr or a log file.

### Host To Extension

The host sends a version 5 `blockListInfo`. To block everything except an
allowlist, send one block with `blockList: ["*"]` and the allowed domains in
`exceptionList`:

```json
{
  "version": 5,
  "isPro": "true",
  "statsEnabled": "false",
  "blockListInfo": {
    "blocks": {
      "FocusOS": {
        "blockList": ["*"],
        "exceptionList": ["youtube.com", "github.com"],
        "titleList": [],
        "allowanceUrlList": [],
        "lock": "",
        "password": "",
        "randomTextLength": 0
      }
    }
  }
}
```

Push once on connect and again whenever policy changes. The extension diffs the
policy and re-checks open tabs.

### Extension To Host

The extension may send chatter such as:

- `port-check`
- `counter@...`
- `stats@...`
- `blocked@...`
- `open-blocker`

The host can ignore these messages. It only needs to keep stdin open and push
policy updates. It exits when the browser closes the port.

## Development Host Flow

For unpacked extension testing:

1. Open the browser extensions page.
2. Enable Developer mode.
3. Load this directory as an unpacked extension.
4. Confirm the extension ID matches the pinned ID above.
5. Run:

   ```bash
   ./host/install-host.sh
   ```

6. Fully restart the browser.
7. Edit `host/allowlist.txt`; the development host picks up changes and pushes
   a new policy.

The production testing-machine path uses the scripts documented in
[../../docs/browser-blocker.md](../../docs/browser-blocker.md) instead of manual
unpacked loading.

## Changes From Upstream

- Native host retargeted from `com.getcoldturkey` to `com.focusos.blocker`.
- FocusOS-owned signing key and pinned extension ID.
- Offline blocked page replaces remote blocked-page dependencies.
- Telemetry, pause-key fetches, permission/consent nag tabs, the web-store
  metadata signature, and the `ctFrame` bridge were removed.
- Core navigation matching remains close to the upstream behavior.

## Files In `host/`

| File | Purpose |
|------|---------|
| `focusos_blocker_host.py` | Standalone development native messaging host. |
| `allowlist.txt` | Development allowlist, one domain per line with `#` comments allowed. |
| `install-host.sh` | Registers `com.focusos.blocker` host manifests for Chromium-family browsers. |
