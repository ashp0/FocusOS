# FocusOS — Permanent Install (Linux / SDDM + Wayland)

This makes a machine boot into **FocusOS as the only selectable login session**.
Tested target: KDE Plasma 6 on Wayland (KWin compositor).

## What the install does

`packaging/linux/install.sh` (run once, with `sudo`):

1. **Builds** FocusOS from this home checkout into `build/focusos`, owned by your
   user. The in-app updater rebuilds *here* — that's why updates need no `sudo`.
2. Installs the helper scripts and the locked-down KDE config into
   `/usr/local/lib/focusos/`.
3. Installs the session wrapper at `/usr/local/bin/focusos-session`, baking in
   the path to your `build/focusos` binary.
4. **Stashes every other login session** (`*.desktop` in
   `/usr/share/wayland-sessions` and `/usr/share/xsessions`) into
   `/usr/local/lib/focusos/stashed-sessions/`, then installs the FocusOS session
   entry. After this, SDDM offers only FocusOS.
5. Installs the logind lockdown (`/etc/systemd/logind.conf.d/90-focusos-logind.conf`):
   no spare VTs, kill leftover user processes on logout.
6. Installs a **scoped sudoers rule** (`/etc/sudoers.d/focusos`) letting your
   user run *only* the recovery script without a password.
7. `setcap cap_net_admin,cap_net_raw+ep` on `nft` so the unprivileged FocusOS
   session can apply the network allowlist.

```bash
sudo ./packaging/linux/install.sh
sudo reboot
```

At the SDDM screen FocusOS should be the only session.

## How the strictness holds

- **Respawn watchdog** (`focusos-watchdog.sh`): a headless, windowless supervisor.
  In the install it runs as KWin's `--exit-with-session` command in `--kiosk`
  mode, so FocusOS is relaunched for the life of the login session. During a
  routine it's also armed in routine mode via the `~/.focusos/active.json`
  checkpoint — kill or crash FocusOS and it comes back and resumes the locked
  routine with the correct remaining time. The min-time floor still applies.
- **Desktop lockdown**: the shipped `kglobalshortcutsrc` keeps Alt+Tab (window
  switching among the allowed apps — the only way to move between routine
  windows) but strips launcher / overview / activity / "run command" shortcuts.
  `kwinrc` disables hot-corner and overview effects. The lockdown watchdog inside
  FocusOS additionally kills any launcher / spotlight / dock / file-manager /
  drop-down-terminal process every ~1.5s while a routine is engaged.

## Updating (no sudo)

Settings → **SYSTEM** → **Update** (behind the TOTP unlock). This runs
`git pull --ff-only` + a rebuild of `build/focusos`, snapshots the previous
binary, then relaunches under a **30-minute probation**:

- If the new build crash-loops (the watchdog keeps relaunching and the launch
  counter passes the threshold), FocusOS **auto-reverts** to the snapshot.
- If 30 minutes pass cleanly, the update is **auto-committed** and the snapshot
  is reclaimed.
- You can also **Revert** manually during the window, or **Keep this build** to
  end probation early.

## Recovery — leaving the FocusOS-only session

Recovery is gated behind your **six-digit TOTP code**. In FocusOS:

1. Open **Settings** and unlock with your authenticator code.
2. Go to the **SYSTEM** tab → **Restore other sessions**.

This runs `focusos-restore-sessions.sh` (via the scoped sudoers rule), which
moves the stashed session entries back into `/usr/share/wayland-sessions` /
`xsessions` and removes the logind lockdown. Log out and the other sessions
(Plasma, etc.) are selectable at SDDM again.

> If you ever lose the TOTP device, recovery is still possible from a root shell
> (another TTY/USB boot): run `/usr/local/lib/focusos/focusos-restore-sessions.sh`
> as root, or move the `.desktop` files back from
> `/usr/local/lib/focusos/stashed-sessions/` by hand.

## Uninstall / reverse the install

```bash
sudo /usr/local/lib/focusos/focusos-restore-sessions.sh   # restore sessions + logind
sudo rm -f /usr/share/wayland-sessions/focusos.desktop \
           /usr/local/bin/focusos-session \
           /etc/sudoers.d/focusos
sudo rm -rf /usr/local/lib/focusos
```
