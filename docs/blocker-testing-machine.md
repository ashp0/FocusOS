# FocusOS Blocker — testing-machine setup

The browser blocker has three moving parts:

1. **Extension** (`resources/focusos-blocker/`) — enforces a per-routine
   allowlist inside Brave/Chromium. Pinned ID `gkbnapcbaflmaaoimfonclabmglfiden`.
2. **Native host** — the `focusos` binary itself, run as `focusos --native-host`.
   The browser spawns it; it reads the signed rules and pushes them to the
   extension over the native-messaging protocol.
3. **Signed rules** — `~/.focusos/blocker/policy.dat`, written by FocusOS when a
   routine's network lock engages.

## One-time setup

On the testing machine, from the repo root:

```sh
scripts/setup-testing-machine.sh
```

This builds FocusOS, packs + force-installs the extension (one `sudo` prompt for
the managed policy under `/etc`), registers the native host, and routes git
hooks to `.githooks/`. Restart Brave once afterward — the extension installs
silently and stays pinned; there is no manual "load unpacked" step.

## The daily workflow

```sh
git pull
```

That's it. A `post-merge` hook runs `scripts/apply-update.sh`, which rebuilds
FocusOS, repacks the extension with a new version (`4.9.<commit-count>`), and
refreshes the native host. Brave sees the higher version in the local update
manifest and auto-updates the extension on its next check / restart. The native
host changes are live immediately (the registered wrapper execs the rebuilt
binary at a stable path). No terminal reconfiguration, ever.

## How the extension is delivered

It is **self-hosted locally over `http://127.0.0.1`**, not from the Web Store:

- `scripts/focusos-blocker-pack.sh` packs `resources/focusos-blocker/` into
  `~/.focusos/blocker/dist/focusos-blocker.crx` (signed with
  `focusos-blocker.pem`, so the ID stays pinned) and writes an Omaha update
  manifest `updates.xml` next to it, with the crx `codebase` and the manifest's
  `update_url` pointing at `http://127.0.0.1:<port>` (default `48217`).
- `scripts/focusos-blocker-serve.sh` serves `~/.focusos/blocker/dist/` on that
  localhost port via a `systemd --user` unit (`focusos-blocker-dist.service`).
  **This is mandatory:** Chromium/Brave refuse `file://` update URLs for
  force-installed extensions — the policy is read but the crx is never fetched,
  so the extension never loads and the native host never spawns. A localhost
  HTTP origin is a trusted secure context and works. `apply-update.sh` keeps the
  server running on every pull.
- `scripts/focusos-policy-install.sh` drops a managed policy into every Brave
  channel policy root — `/etc/brave/policies/managed/` **and** the Brave-Origin
  roots (`/etc/brave-origin*/policies/managed/`), plus chromium/chrome if
  present — with `ExtensionInstallForcelist` +
  `ExtensionSettings:force_installed` pointing at the `http://127.0.0.1:<port>`
  update manifest, plus `ExtensionInstallSources` to permit that local source.
  It is idempotent and only needs `sudo` when a target is missing or changed.

> **Brave channels (incl. Brave Origin / brave-origin-beta).** Both the
> force-install policy and the native-messaging host are registered per browser
> channel. `host/install-host.sh` now auto-discovers *every* profile dir under
> `~/.config/BraveSoftware/*` (and the Chrome/Chromium/Edge equivalents), so any
> Brave channel — including the Brave-Origin line — gets the
> `com.focusos.blocker` host without hardcoding its name. If the extension is
> installed but enforces nothing (you can browse freely during a routine), the
> native host failed to connect for that channel: re-run `install-host.sh` and
> fully restart the browser.

> **Diagnosing a non-installing extension.** Run `scripts/blocker-doctor.sh` —
> it prints the install type, host-manifest locations, `host.log`, the managed
> policy, and whether the local HTTP server is reachable. The classic failure is
> an empty `host.log` (host never spawned) because the crx couldn't be fetched —
> historically from `file://` URLs, which is why delivery now goes over
> `http://127.0.0.1` (see above). Confirm the server with
> `curl -sf http://127.0.0.1:48217/updates.xml`.

## How the rules resist tampering

`policy.dat` is **not** a plain-text file. It is a signed binary container:
`"FOSB"` magic + version + serialized `{active, allowlist, issuedAt}` +
HMAC-SHA256 over all of it (key embedded in the binary — see
`src/blocker/BlockerSecret.h`). The native host verifies the signature before
trusting it.

Failure handling is **fail-closed during a live session**: if `policy.dat` is
edited, corrupted, or deleted while a routine is running (detected via the
`~/.focusos/active.json` checkpoint), the host blocks *everything* rather than
falling open. With no active session, a missing/invalid file simply allows all
browsing. So hand-editing the file to escape a lock doesn't work — it just locks
harder.

A determined user with a disassembler can still recover the HMAC key from the
binary; defeating that is out of scope. The bar here is "not trivially editable,"
which this clears.

## If the extension is disabled mid-session

Disabling or removing the extension is the obvious way to defeat an in-browser
allowlist, so FocusOS watches for it and refuses to let it work:

- The native host rewrites `~/.focusos/blocker/host-alive` every ~1.5s while a
  browser is connected to it, and deletes it when the port closes. A fresh file
  == the extension is enabled and enforcing.
- While a routine's network lock is live, the Linux watchdog checks (every
  1.5s, after a ~20s start-up grace) whether a Chromium-family browser is
  running while that beacon is stale. If so, it assumes the extension was turned
  off and **clamps the network to a full deny** (`NetGate::applyFullDeny` —
  loopback only, no allowlist, no DNS) and pops a dialog (kdialog / zenity /
  xmessage — chosen because the routine kills plasmashell, so notify-send may
  have no daemon) telling the user to re-enable it. The dialog re-shows every
  30s while clamped.
- Re-enabling the extension reconnects the host, refreshes the beacon, and the
  watchdog restores the routine's normal allowlist automatically. Closing the
  browser also lifts the clamp (nothing left to escape through).

**The clamp only arms after the extension has checked in at least once during
the session.** If it never connects — broken host/extension wiring, not user
tampering — the watchdog stays silent (no clamp, no nag) rather than stranding
you. So a persistent "extension is disabled or missing" popup means the
extension genuinely can't reach the native host; run `scripts/blocker-doctor.sh`
to find out why.

**Mute switch:** `touch ~/.focusos/blocker/presence-check-off` disables the
presence enforcement entirely (and lifts any active clamp) until you remove the
file — an escape hatch while debugging.

## Diagnostics

- `~/.focusos/blocker/host.log` — every policy push, with the active flag and
  allowlist.
- `focusos --write-policy <0|1> [host ...]` — write the signed rules manually
  (e.g. to reproduce a state); normal operation drives this from routines.
