# FocusOS Stability Audit — 2026-05 (daily-driver pass)

Scope: the focus-shell-improvements branch. This pass audited the network lock
(fail-closed + start/end races), app-quit logic (resistant / respawning apps),
Wayland compositor compatibility, and unlock-modal state transitions, with an eye
toward friction and data loss under normal daily use.

Severity legend: **[P1]** can lose data / strand the user · **[P2]** friction or
correctness · **[P3]** polish / future-proofing.

---

## 1. Network lock (NetGate / nftables)

### Findings

- **[OK] Fail-closed on start.** `RoutineManager::startRoutine` aborts the whole
  routine if `applyNetworkPolicy` fails (and only writes the extension policy
  *after* NetGate succeeds), so a routine never starts half-locked. The QML
  `networkLockPrompt` surfaces the failure. Good.

- **[OK] Crash cleanup.** `main.cpp` installs SIGSEGV/SIGABRT/SIGBUS/SIGFPE/
  SIGTERM/SIGINT handlers that call `dropNetworkPolicy()` + release sleep
  inhibitors, so a crash can't strand the machine behind the allowlist (the
  original wifi-brick bug). Good.

- **[P2] Full-access routines now bypass the lock by design (Task 4).** Engaging
  a `full_access` routine calls `startRoutine(..., applyNetworkLock=false)`. This
  is intended, but note: there is now a routine class that runs with NO outbound
  filtering. The TOTP gate in the engage-prep overlay is the only thing standing
  in front of it — make sure that gate is never bypassable from QML (it is
  validated via `totpEngine.validate`, side-effect free, before `engage()`).

- **[P2] Extension-presence watchdog debounce.** `enforceBlockerExtension`
  clamps to full-deny when the browser is up but the blocker beacon goes stale.
  The debounce (`m_extensionMissingSinceMs`) protects against startup lag, but
  the clamp + restore path shares the single 1.5 s `m_lockdownTimer`. If a
  routine is network-only (no app lockdown) the timer still runs — verify on the
  device that toggling the extension restores the routine allowlist promptly.

### Suggested follow-ups
- Add a NetworkManager connectivity chip (already noted in LinuxBackend FUTURE)
  so the user can tell allowed sites are reachable while locked.

---

## 2. App-quit logic (Task 1 strict enforcement)

### Findings

- **[P1 → mitigated] Mass-quit on engage can lose unsaved work.** The new
  `LinuxBackend::quitBackgroundApps` SIGTERMs every GUI client that isn't in the
  keep-set or allowlist. Mitigations in place: (a) SIGTERM (not SIGKILL) lets
  apps run save-on-quit handlers; (b) the engage-prep overlay shows a 5 s
  warning with CANCEL before the sweep; (c) a conservative keep-set protects the
  compositor, portal, audio, dbus, and FocusOS itself. **Residual risk:** an app
  with unsaved work that exits on SIGTERM without prompting will lose it. This is
  inherent to the requested behavior; the 5 s warning is the guard.

- **[P2] GUI heuristic can miss / over-match.** "Graphical = has
  WAYLAND_DISPLAY/DISPLAY in /proc/PID/environ" is a heuristic. A background
  daemon launched from a graphical session inherits those vars and *could* be
  killed if not in the keep-set; conversely a GUI app that scrubbed its environ
  would be spared. The keep-set covers the known session-critical daemons. **Test
  on the device**: engage a routine with Brave + an editor open alongside Slack/
  Discord/Spotify and confirm only the intended apps survive and the session
  stays up.

- **[P2] comm truncation.** `/proc/PID/comm` is capped at 15 chars. The sweep
  prefers the `exe` symlink basename (full name) and only falls back to comm; the
  keep-set also inserts `name.left(15)` for allowlisted apps. Watch for an app
  whose 15-char-truncated name collides with something you want killed.

- **[OK] Always-allowed + routine apps are spared** via
  `processNamesForCommandLines` + `alwaysAllowedProcessNames`.

- **[P2] Respawning launchers** are still handled by the existing 1.5 s lockdown
  watchdog deny-list (krunner/plasmashell/kickoff/rofi/…). The mass-quit is a
  one-shot at engage; the watchdog is the steady-state. Apps that aggressively
  respawn themselves (not in the deny-list) would survive — acceptable, since the
  switcher is disabled and launchers are dead, so the user can't bring new ones
  forward.

### Suggested follow-ups
- Consider escalating to SIGKILL after a grace period for processes that ignore
  SIGTERM, *but* only after the warning window — leave as-is for now to avoid
  data loss.

---

## 3. Wayland / KWin compatibility

### Findings

- **[OK] Single window.** Progress overlay is a `Qt::Tool` +
  `WindowTransparentForInput` + `WindowDoesNotAcceptFocus` window kept above via
  the server-side `kwinrulesrc` rule (Wayland can't self-restack). No extra
  QQuickViews. Matches the "one window" constraint.

- **[P2] Alt+Tab disabled at the config layer (Task 1).** Unbinding Walk-Through-
  Windows in `kglobalshortcutsrc` is static for the session — it is NOT toggled
  per unlock-modal state. This is fine because the unlock modal renders *inside*
  the FocusOS window, so reaching it never needs the switcher. Documented in the
  config.

- **[P2] Screen lock / DPMS is best-effort.** `lockScreen()` tries
  `loginctl lock-session` then `kscreen-doctor --dpms off`. On a bare
  kwin_wayland session neither may be present; the QML black overlay is the
  guaranteed fallback, but it only covers the screen when the FocusOS window is
  frontmost. During a routine (shell minimized) only DPMS actually blanks the
  panel. **Test on the device** which of loginctl / kscreen-doctor is available.

- **[P3] Power-key → lock via logind DBus.** Wired in `main.cpp` against the
  resolved login1 session path; if resolution fails nothing is wired (the in-app
  button still works). Verify `HandlePowerKey=lock` actually emits the session
  `Lock` signal on the target distro.

---

## 4. Unlock-modal state transitions

### Findings

- **[OK] Access revocation re-locks the modal.** `UnlockModal` listens for
  `accessChanged` and drops `adminUnlocked` + refocuses the code field when
  access is revoked (inactivity auto-lock, manual end).

- **[P2] Open-ended continuation vs. settings unlock (Task 5).** Entering
  open-ended mode sets `active()` true with no checkpoint. `unlockOtherAccess`
  now special-cases `m_openEnded` so it does NOT write a phantom "unlocked"
  session record (the open-ended timer reads remaining=0). `endActiveRoutine`
  also early-returns for open-ended without applying the min-time floor or
  re-recording. Verify the streak/stats don't double-count when you Continue then
  End.

- **[P3] Daily-target editor moved (Task 3).** The +/- control left InfoPanel for
  the APPEARANCE tab; the main screen keeps a read-only `TARGET …` readout. No
  state-machine impact.

- **[P3] Screen lock over an open modal.** Locking from the Settings header
  leaves the modal open underneath the black overlay; unlocking returns to it.
  Intended.

---

## Device test checklist (Linux/KDE)

1. Engage a normal routine with Slack/Spotify/Files open → 5 s warning fires →
   only routine + always-allowed apps remain; session stays alive.
2. Alt+Tab / Meta does nothing during a routine.
3. Full-access routine demands the 6-digit code before starting; wrong code
   blocks; right code starts with full internet.
4. Let a timer expire → Continue → ambient open-ended screen (no countdown) →
   End → back to console, streak intact, no duplicate stat row.
5. Settings ▸ LOCK SCREEN blanks the panel; a key/click restores it.
6. Power button locks/blanks instead of shutting down.
7. Reboot → SDDM shows only FocusOS, no session selector, Wayland only.
8. Daily target edits only from Settings ▸ APPEARANCE; main screen shows it
   read-only.
