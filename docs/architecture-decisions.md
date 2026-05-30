# Architecture Decisions

This file records current decisions that shape FocusOS. Older scratch notes and
discarded ideas belong in git history, not in the active documentation surface.

## Product Boundary

- FocusOS is a session-level productivity shell, not a theme for a normal
  desktop.
- The strict deployment target is Linux/KDE Wayland with a dedicated non-admin
  daily-use account and a separate admin recovery path.
- macOS remains useful for UI development and softer focus sessions, but it is
  not treated as an equivalent lockdown platform.
- The built-in browser/information-terminal idea was removed. Research routines
  can allow external apps and domains, but the shell itself should not advertise
  a general web surface.
- Routine editing is protected behind TOTP-gated Settings. The main routine
  list presents sealed profiles, not day-to-day configuration controls.

## Platform Model

- All OS-specific behavior goes through `PlatformBackend`.
- Linux owns the strict path: SDDM session replacement, KWin Wayland control,
  nftables policy, watchdog, session recovery, and updater integration.
- macOS owns the lighter shell path: fullscreen always-on-top windowing and
  softer app access behavior.
- Linux desktop control intentionally uses external KWin/qdbus tooling instead
  of linking Qt DBus into the app process, because the Fedora/KWin path has been
  more stable that way.

## State And Persistence

- FocusOS creates `~/.focusos/` on first launch so the app is immediately usable.
- `RoutineManager` owns routine admin persistence and writes
  `~/.focusos/routines.json` atomically from Save All.
- Malformed or structurally invalid routine files are regenerated from shipped
  defaults. An intentionally empty routines array is preserved as an explicit
  admin action.
- `~/.focusos/config.json` is shared by independent Settings surfaces. Writers
  preserve unknown keys.
- Routine statistics live in `~/.focusos/stats.json`. Active routine progress is
  persisted while the timer runs so a crash or power loss becomes interrupted
  work instead of lost work.
- `~/.focusos/active.json` is the armed-routine checkpoint used by the Linux
  watchdog and recovery paths.

## Auth And Settings

- TOTP enrollment happens on first launch and is only marked complete after the
  first valid code, preventing lockout if the app exits mid-pairing.
- The TOTP label is `FocusOS`; the setup key is shown in grouped
  four-character chunks for manual entry in password managers.
- First-launch enrollment remains visible before unlock because no valid code
  can exist yet. After enrollment, QR/secret reset belongs behind unlocked
  Settings.
- Settings access can open temporary unrestricted access, but routine minimum
  time rules still apply before a legitimate early end.

## Routine Lockdown

- An engaged routine checkpoints routine id, start time, remaining time,
  minimum-time state, and network-lock state.
- The Linux respawn watchdog is external and headless. It keeps FocusOS alive
  during a kiosk login session and during an armed routine without showing a
  second app window.
- Desktop lockdown keeps Alt+Tab so users can switch among allowed routine
  windows, while launcher, overview, activity, run-command, dock, file-manager,
  and drop-down terminal escape routes are stripped or killed during routines.
- Routine expiry records the session, releases policy, returns the shell to
  sealed mode, and opens a completion prompt.
- Completion feedback is visual and reflective; no alarm tone plays at expiry.

## Network And Browser Policy

- Linux routine network policy uses nftables. It allows loopback, DNS, and
  resolved addresses for the active routine allowlist, then drops the rest.
- Routine engagement fails closed if FocusOS cannot apply the nftables table.
- The browser blocker exists because network allowlists are too coarse for
  modern web work. It blocks document navigations outside the routine allowlist
  while leaving subresources for allowed sites alone.
- Browser policy is stored as a signed binary container under
  `~/.focusos/blocker/policy.dat`. During an active routine, missing or invalid
  policy fails closed in the native host.
- A browser-extension heartbeat lets the Linux watchdog clamp the network to
  full deny if a connected Chromium-family browser loses the blocker mid-session.

## UI And Media

- The main production board is the primary right-panel surface. It keeps the
  desktop biased toward current state, progress, notes, and stats.
- Notes are per-routine scratch space and can be archived by day.
- The production graph draws from recent history and supports inspection,
  zooming, scrubbing, and range selection.
- Ambient inspiration media comes from `~/.focusos/inspiration/` and is rendered
  as a muted background layer that settles over time.
- Ambient music comes from `~/.focusos/music/` and falls back to bundled audio.
  Only `.mp3` and `.ogg` files are considered playable.
- Device controls such as audio, brightness, and battery belong in the shell so
  a locked routine does not require Plasma.

## Updating And Recovery

- Linux updates are no-sudo and rebuild in place from the user-owned checkout.
- The updater snapshots the previous binary, relaunches into a probation
  window, auto-reverts on crash loops, and auto-keeps the new build after a
  clean probation.
- Permanent install stashes other display-manager sessions under
  `/usr/local/lib/focusos/stashed-sessions/`.
- Recovery is exposed through TOTP-gated Settings and implemented by one scoped
  sudoers rule that permits only `focusos-restore-sessions.sh`.
- A root shell or recovery boot remains the last-resort fallback; FocusOS does
  not claim to constrain a root user.

## Hardening Roadmap

- Make FocusOS the Wayland compositor, or put a small supervisor between the
  shell and all child app surfaces.
- Launch allowed routine apps in owned cgroups and terminate unmanaged apps when
  a routine ends.
- Replace direct `nft` capabilities with a tiny privileged helper limited to
  FocusOS policy operations.
- Add per-routine AppArmor or Landlock profiles so allowed apps cannot freely
  spawn terminals, browsers, package managers, or chat clients.
- Add stronger artifact capture and weekly review views that separate completed,
  unlocked, and interrupted time.
