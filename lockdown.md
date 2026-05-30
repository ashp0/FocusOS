# FocusOS Lockdown Notes

This file documents the kiosk hardening applied for the permanent Linux/SDDM
install. The target posture is a single FocusOS session on KDE/KWin Wayland:
the login manager offers no alternate desktop, routine sessions cannot fall
through to a shell, and common launcher/terminal escape surfaces are closed.

## Login Session Selector

- `packaging/linux/install.sh` is now a permanent kiosk installer, not a
  side-by-side session installer.
- During install, every non-FocusOS `*.desktop` session entry in
  `/usr/share/wayland-sessions` and `/usr/share/xsessions` is moved into
  `/usr/local/lib/focusos/stashed-sessions/{wayland,xsessions}`.
- Only `/usr/share/wayland-sessions/focusos.desktop` remains visible to SDDM.
- `/etc/sddm.conf.d/10-focusos.conf` pins autologin/session selection to
  `focusos.desktop` where SDDM uses autologin.
- `packaging/linux/focusos-restore-sessions.sh` restores the stashed entries
  after a TOTP-gated SYSTEM recovery action.

## TTY And VT Escape Hatches

- `packaging/linux/90-focusos-logind.conf` now sets:
  - `NAutoVTs=0`
  - `ReserveVT=0`
  - `KillUserProcesses=yes`
- `install.sh` installs that file into
  `/etc/systemd/logind.conf.d/90-focusos-logind.conf`.
- `install.sh` masks `getty@tty1` through `getty@tty6` and `autovt@tty1`
  through `autovt@tty6`.
- `focusos-restore-sessions.sh` un-masks those units during recovery.
- The shipped KDE shortcut config also unbinds known VT/TTY switch action names
  for VT/TTY 1 through 6. This is defense-in-depth; logind/getty lockdown is the
  primary backstop.

## Runtime Shell And Compositor Posture

- `focusos-watchdog.sh --kiosk` now respawns FocusOS for the entire login
  session. It no longer treats an idle FocusOS exit as a reason to release the
  session back to SDDM or another desktop.
- The Linux fatal-signal cleanup in `src/main.cpp` no longer launches
  `plasmashell` on crash. The watchdog is the recovery path; spawning a desktop
  shell would be an escape surface.
- The KWin session wrapper continues to run FocusOS as KWin's
  `--exit-with-session` payload, keeping the watchdog headless and out of the
  window manager.

## Running Routine Escape Surface

- `LinuxBackend` records routine-allowed process names at session preparation
  time.
- The lockdown sweep now kills common terminal emulators in addition to
  launchers, docks, file managers, settings panels, app stores, and monitor
  tools.
- A terminal listed explicitly as a routine app is exempted by process name;
  otherwise terminal surfaces are treated as escape hatches.
- Always-allowed apps remain exempt from the lockdown sweep.

## Recovery Boundary

- `install.sh` installs a scoped sudoers rule:
  `TARGET_USER ALL=(root) NOPASSWD: /usr/local/lib/focusos/focusos-restore-sessions.sh`
- The app calls that script only from the SYSTEM settings tab after TOTP unlock.
- Recovery restores other SDDM sessions, removes the FocusOS SDDM pin, removes
  the logind lockdown, and unmasks TTY gettys.

## Remaining Limits

- A user with physical boot-media access or root credentials can still recover
  the machine. That is intentional operational recovery, not a FocusOS bypass.
- KWin itself is still the compositor. FocusOS hardens the session shell and
  login surface, but it is not a compositor-level security boundary.
