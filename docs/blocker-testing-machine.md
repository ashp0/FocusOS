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

It is **self-hosted locally**, not from the Web Store:

- `scripts/focusos-blocker-pack.sh` packs `resources/focusos-blocker/` into
  `~/.focusos/blocker/dist/focusos-blocker.crx` (signed with
  `focusos-blocker.pem`, so the ID stays pinned) and writes an Omaha update
  manifest `updates.xml` next to it.
- `scripts/focusos-policy-install.sh` drops a managed policy into
  `/etc/brave/policies/managed/` (and chromium/chrome if present) with
  `ExtensionInstallForcelist` + `ExtensionSettings:force_installed` pointing at
  the `file://` update manifest, plus `ExtensionInstallSources` to permit the
  local source. This is the only step that needs `sudo`, and only once.

> If a future Brave build rejects `file://` update URLs, the fallback is to
> serve `~/.focusos/blocker/dist/` over `http://127.0.0.1` via a `systemd --user`
> static-file unit and point the policy URLs there. Not needed today.

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

## Diagnostics

- `~/.focusos/blocker/host.log` — every policy push, with the active flag and
  allowlist.
- `focusos --write-policy <0|1> [host ...]` — write the signed rules manually
  (e.g. to reproduce a state); normal operation drives this from routines.
