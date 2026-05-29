# FocusOS Blocker

A FocusOS-controlled fork of the Cold Turkey browser extension. During a focus
session it enforces an **allowlist**: the entire internet is blocked except the
domains FocusOS explicitly permits.

Upstream is kept pristine at `resources/Cold Turkey/`. This directory is the
fork that FocusOS ships and controls.

## Why a browser extension (vs. blocking subprocesses / the network)

The previous approach clamped the network / killed browser subprocesses, which
broke too many sites (modern pages pull from dozens of hosts). This extension
instead matches **navigations** — the document URL of the top frame and each
iframe — and leaves sub-resources (images, scripts, XHR, media) alone. So:

- Allowlisting `youtube.com` loads the page **and** its video stream
  (`googlevideo.com`) and thumbnails (`ytimg.com`), because those are
  sub-resources, not navigations. No per-CDN / CIDR bookkeeping.
- Subdomains of an allowed domain are allowed automatically
  (`m.youtube.com`, `www.youtube.com`).
- Everything else — any navigation to a non-allowlisted document — is replaced
  by the offline `blocked.html` page (no break / unblock / password bypass).

## Architecture

```
  Browser (Chrome / Brave / Chromium / Edge)
    └─ FocusOS Blocker extension  (ctBackground.js service worker)
         │  chrome.runtime.connectNative('com.focusos.blocker')
         ▼
  Native messaging host  ── pushes the allowlist policy over stdio
```

The extension is a pure policy *enforcer*; it holds no allowlist of its own. The
**native host** owns the policy and pushes it to the extension. Today that host
is the standalone dev script in `host/`; in production it becomes a
`--native-host` mode of the `focusos` binary, speaking the identical protocol.

### Extension ID

Pinned by the `key` in `manifest.json` (so the ID is stable when dev-loaded):

```
gkbnapcbaflmaaoimfonclabmglfiden
```

## Native messaging protocol

Standard Chromium native messaging framing: each message is a **4-byte
little-endian length prefix** followed by that many bytes of UTF-8 JSON.
`stdout` carries framed messages only — never log to it.

### Host → extension (the policy)

The host pushes a **version 5** `blockListInfo`. To block everything except an
allowlist, use a single block whose `blockList` is `["*"]` (the `*` compiles to
`.*`, matching every navigation) and whose `exceptionList` is the allowlist:

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

Push it once on connect, and again whenever the policy changes. The extension
diffs `blockListInfo` and re-checks open tabs on every change.

### Extension → host (ignorable)

The extension emits chatter the host can safely ignore: `port-check` (a
once-a-minute liveness probe — no reply needed; the extension only reconnects if
its own `postMessage` throws), plus `counter@…`, `stats@…`, `blocked@…`, and
`open-blocker`. The host just needs to **stay alive** (keep stdin open) to hold
the port; it exits on stdin EOF when the browser closes the port.

## Dev setup (confirm end-to-end blocking)

1. **Load the unpacked extension.** In Chrome/Brave open `…/extensions`, enable
   *Developer mode*, click *Load unpacked*, and select this
   `resources/focusos-blocker/` directory. Confirm the ID matches the one above.

2. **Register the native host.** Run the installer (auto-detects which
   Chromium-family browsers are present and writes the host manifest into each):

   ```sh
   ./host/install-host.sh
   ```

   Then fully **restart the browser** so it picks up the new host.

3. **Edit the allowlist.** `host/allowlist.txt` — one domain per line. Edits are
   picked up live (~2s) and pushed to the extension; no restart needed.

4. **Verify.** A non-listed site (e.g. `reddit.com`) shows the offline blocked
   page; a listed site (e.g. `youtube.com`) loads fully, video included.

### Files in `host/`

| File | Purpose |
|------|---------|
| `focusos_blocker_host.py` | Standalone native messaging host (dev backstop). Pushes the v5 policy and live-watches the allowlist. |
| `allowlist.txt` | The allowed domains. One per line; `#` comments allowed. |
| `install-host.sh` | Registers `com.focusos.blocker` for Chrome/Brave/Chromium/Edge on macOS & Linux. `EXT_ID=… ./install-host.sh` to override the ID. |

## Phase 1 changes vs. upstream (done)

- Native host retargeted from `com.getcoldturkey` → `com.focusos.blocker`; own
  signing key / pinned extension ID.
- Offline `blocked.html` / `blocked.js` replace the remote
  `getcoldturkey.com/blocked` iframe — no network needed, no bypass UI.
- Telemetry, permission/consent nag tabs, remote pause-key fetch, the web-store
  `_metadata` signature, and the `ctFrame` bridge removed.
- Core navigation-matching engine untouched.

## Phase 2 — integration into FocusOS (next)

- Promote the native host into the `focusos` binary (`--native-host` mode, same
  stdio protocol as `host/`).
- `RoutineManager` exports the active session's allowlist as the v5 policy.
- Force-install the extension (enterprise policy) + NetGate backstop so the
  session can't be trivially bypassed by removing the extension.
