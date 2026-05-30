# FocusOS Install And Recovery

This is the canonical setup guide for building FocusOS, trying it locally, and
installing it as a strict Linux login session.

## Requirements

- CMake 3.24 or newer.
- Qt 6.7 or newer with Core, Gui, Multimedia, Network, Qml, Quick,
  QuickControls2, Test, and Widgets.
- A C++20 compiler.
- Ninja or Make.
- Linux strict mode: SDDM, KWin Wayland, nftables, xdg-utils, PulseAudio
  command-line tools or PipeWire compatibility, and a Chromium-family browser if
  the browser blocker is used.

## Linux Dependencies

Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja ccache qt6-base qt6-declarative qt6-multimedia qt6-wayland qt6-tools kwin nftables sddm xdg-utils wireplumber libpulse
```

Fedora KDE:

```bash
sudo dnf install cmake ninja-build ccache gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel qt6-qtwayland qt6-qttools kwin-wayland plasma-workspace-wayland nftables sddm xdg-utils wireplumber pulseaudio-utils
```

Ubuntu package names vary by release. Use Qt 6.7+ from the distro, KDE/Qt PPA,
or the Qt online installer, then install the base tools:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build ccache nftables sddm xdg-utils wireplumber pulseaudio-utils
```

## Build And Test

From the repository root:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

On Linux, run the foreground development shell:

```bash
./build/focusos
```

On macOS:

```bash
open build/focusos.app
```

For faster Linux rebuilds:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux
```

## Before Permanent Install

Use a dedicated Linux test machine or a VM until the workflow is boring. Confirm
these basics before installing FocusOS as the only session:

- FocusOS launches from a normal terminal.
- Settings enrollment works with a TOTP authenticator.
- At least one short routine can engage and end cleanly.
- KWin Wayland is available.
- `nft` is installed and usable.
- You have a separate admin account, SSH access, live USB, or another recovery
  path.

## Permanent Linux Session Install

The installer builds the app from this checkout, installs the session wrapper
and helper scripts, stashes other login sessions, applies KDE/logind lockdown
config, grants a narrow recovery sudoers rule, and gives `nft` the capabilities
needed for network policy.

Run it from your normal user account:

```bash
sudo ./packaging/linux/install.sh
sudo reboot
```

After reboot, SDDM should offer FocusOS as the only selectable session. The app
binary remains in this checkout at `build/focusos`, which lets the in-app
updater rebuild without sudo.

Installed paths:

| Path | Purpose |
|------|---------|
| `/usr/local/bin/focusos-session` | SDDM session entry point. |
| `/usr/local/lib/focusos/` | Watchdog, update, revert, recovery scripts, KDE config. |
| `/usr/local/lib/focusos/stashed-sessions/` | Other desktop sessions moved out of SDDM's list. |
| `/usr/share/wayland-sessions/focusos.desktop` | FocusOS Wayland session. |
| `/etc/systemd/logind.conf.d/90-focusos-logind.conf` | VT/session lockdown. |
| `/etc/sudoers.d/focusos` | Allows only the recovery script to run without a password. |

## Manual Non-Permanent Session Install

For a softer trial that does not stash other sessions:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
sudo install -Dm755 build-linux/focusos /opt/focusos/bin/focusos
sudo install -Dm755 packaging/linux/focusos-session.sh /usr/local/bin/focusos-session
sudo install -Dm644 packaging/linux/focusos.desktop /usr/share/wayland-sessions/focusos.desktop
```

Then choose FocusOS at the SDDM login screen.

## Network Lock

Routine network policy is implemented through nftables. During a locked routine,
FocusOS allows loopback, DNS, and resolved addresses for the routine allowlist,
then drops other outbound traffic. Routine engagement fails closed if policy
application fails.

The permanent installer runs this capability grant when `nft` exists:

```bash
sudo setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)"
getcap "$(command -v nft)"
```

For a production deployment, a tiny privileged helper would be cleaner than
granting capabilities to the general `nft` binary.

## Browser Blocker

Network allowlists alone are too coarse for modern web work. FocusOS also ships
a Chromium-family extension that blocks top-frame and iframe navigations outside
the active routine's domain allowlist while allowing subresources for permitted
sites.

Use [docs/browser-blocker.md](docs/browser-blocker.md) for setup,
self-hosted CRX delivery, native-host diagnostics, and failure modes.

## Updating

In the installed Linux session, unlock Settings with TOTP and use
`SYSTEM -> Update`. The updater:

- Runs `git pull --ff-only`.
- Rebuilds the user-owned `build/focusos` binary.
- Snapshots the previous binary.
- Relaunches into a probation window.
- Auto-reverts if the watchdog sees a crash loop.
- Auto-keeps the new build after a clean probation.

The update scripts live in `packaging/linux/`.

## Recovery

Preferred in-app path:

1. Open Settings.
2. Unlock with the six-digit TOTP code.
3. Use `SYSTEM -> Restore other sessions`.
4. Log out and choose the restored desktop session.

Shell path from an admin account, TTY, SSH session, or recovery boot:

```bash
sudo /usr/local/lib/focusos/focusos-restore-sessions.sh
sudo systemctl restart sddm
```

Last-resort manual restoration:

```bash
sudo mv /usr/local/lib/focusos/stashed-sessions/wayland/*.desktop /usr/share/wayland-sessions/ 2>/dev/null || true
sudo mv /usr/local/lib/focusos/stashed-sessions/xsessions/*.desktop /usr/share/xsessions/ 2>/dev/null || true
sudo rm -f /etc/systemd/logind.conf.d/90-focusos-logind.conf
sudo systemctl restart systemd-logind
sudo systemctl restart sddm
```

## Uninstall

```bash
sudo /usr/local/lib/focusos/focusos-restore-sessions.sh
sudo rm -f /usr/share/wayland-sessions/focusos.desktop
sudo rm -f /usr/local/bin/focusos-session
sudo rm -f /etc/sudoers.d/focusos
sudo rm -rf /usr/local/lib/focusos
```

The user data directory `~/.focusos/` is not removed automatically.

## Smoke Test Checklist

- Reboot and enter the FocusOS session.
- Confirm no Plasma panel, dock, launcher, desktop icons, or app menu appears.
- Press common launcher shortcuts such as `Alt+Space`, `Meta`, and
  `Ctrl+Alt+T`; they should not expose a launcher.
- Start a short routine and confirm the timer begins.
- Open an allowed routine URL and confirm it works.
- Open a blocked URL and confirm it fails or shows the blocker page.
- Put one image or video in `~/.focusos/inspiration/`, relaunch, and confirm the
  ambient layer picks it up.
- Use TOTP-gated Settings access and confirm the unrestricted window expires.
- Force a reboot during a test routine, relaunch, and confirm interrupted work
  is reflected in stats.
