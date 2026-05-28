# FocusOS Linux Daily-Driver Setup

This guide is written for testing FocusOS on a 2017 Intel iMac and then moving toward a dedicated daily-driver install.

Last verified: 2026-05-28.

## Recommended distro

Use **Fedora KDE Plasma Desktop 44** first.

Why:
- FocusOS is built around **KWin Wayland**, Qt 6, KDE virtual desktops, and a login-session replacement model.
- Fedora KDE has Plasma 6 on Wayland as the primary path, which matches FocusOS best.
- Fedora KDE Plasma Desktop 44 is the current Fedora KDE release and ships Plasma 6.6, so it is close to the Fedora machine you are already testing on and bugs reproduce cleanly.

Good alternatives:
- **Fedora Kinoite 44** if you want an immutable base later. Do not start here; debugging capabilities, `nft`, and local builds are easier on regular Fedora KDE.
- **KDE neon User Edition** if you only want the newest KDE userspace. It is less ideal for FocusOS production testing because this project is already tuned around Fedora/KWin/nftables.

Avoid for the first production pass:
- GNOME Workstation. FocusOS needs KWin behavior for desktop creation/window pinning.
- Ubuntu/Kubuntu LTS as the first target. It can work later, but KDE/Qt versions and package names vary more.
- Arch as the production machine until the app is stable. It is great for development, but not the first daily-driver base.

## 2017 iMac install notes

1. Back up macOS fully before resizing or replacing the disk.
2. Create the Fedora KDE USB from macOS with Fedora Media Writer. Balena Etcher or `dd` are fine fallbacks if Fedora Media Writer fails.
3. Boot the iMac while holding `Option`, then choose the USB installer.
4. Prefer Ethernet during install. Some Intel iMac Wi-Fi chips need extra firmware or RPM Fusion packages after installation.
5. Install Fedora KDE normally. For first testing, either use the whole disk or keep macOS on a separate APFS partition with plenty of free space.
6. After first boot, update everything:

```bash
sudo dnf upgrade --refresh
sudo reboot
```

If Wi-Fi does not work after install, use Ethernet or USB phone tethering, then enable RPM Fusion and install the common Broadcom driver stack:

```bash
sudo dnf install \
  https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
  https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install broadcom-wl akmods
sudo akmods --force
sudo reboot
```

If the iMac uses a different Wi-Fi chipset, check the hardware id before installing random firmware packages:

```bash
lspci -nnk | grep -A3 -Ei 'network|wireless|wifi'
```

## Install build dependencies

On Fedora KDE:

```bash
sudo dnf install \
  git cmake ninja-build ccache gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel qt6-qtwayland qt6-qttools \
  kwin-wayland plasma-workspace-wayland xdg-utils nftables wireplumber pulseaudio-utils
```

Check the two critical runtime tools:

```bash
command -v qdbus6 || command -v qdbus-qt6 || command -v qdbus
command -v nft
```

`qdbus6` is required for the KWin script path that creates/switches the Focus desktop. FocusOS intentionally avoids Qt DBus in-process because it caused Fedora/KWin aborts during engage.

## Build FocusOS

From the repo:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
```

Quick foreground smoke test:

```bash
./build-linux/focusos
```

Open Settings, verify the Audio tab, and test one short routine before installing the login session.

## Configure nftables for strict network lock

FocusOS calls `nft` directly. The binary needs network-admin capabilities:

```bash
sudo systemctl enable --now nftables
sudo setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)"
getcap "$(command -v nft)"
```

Expected output:

```text
/usr/sbin/nft cap_net_admin,cap_net_raw=ep
```

Validate that missing old tables do not print scary errors:

```bash
nft list table inet focusos >/dev/null 2>&1 || true
```

FocusOS now checks before deleting its `inet focusos` table, so an absent table should not spam the terminal during engage.

Security note: setting capabilities on `nft` lets any local user who can execute that binary alter firewall state. That is acceptable for a dedicated single-user FocusOS test machine, but the cleaner production path is a tiny privileged helper or `pkexec` wrapper limited to FocusOS' own table.

## Install as a login session

Install the binary and session launcher:

```bash
sudo install -Dm755 build-linux/focusos /opt/focusos/bin/focusos
sudo install -Dm755 packaging/linux/focusos-session.sh /usr/local/bin/focusos-session
sudo install -Dm644 packaging/linux/focusos.desktop /usr/share/wayland-sessions/focusos.desktop
```

Optional but recommended for a dedicated FocusOS machine:

```bash
sudo install -Dm644 packaging/linux/90-focusos-logind.conf /etc/systemd/logind.conf.d/90-focusos-logind.conf
sudo systemctl restart systemd-logind
```

Warning: restarting `systemd-logind` can terminate graphical sessions. Do it from a TTY or after saving work.

At the login screen, choose the **FocusOS** session.

## First-run checklist

1. Start FocusOS from a normal Plasma terminal first.
2. Add one allowed app to a routine, such as `/usr/bin/kate` or `/usr/bin/konsole`.
3. Set the routine to 2-5 minutes.
4. Press Engage.
5. Expected behavior:
   - No `QDBusArgument` abort.
   - No `delete table inet focusos` error for an absent table.
   - Plasma shell/launchers disappear.
   - A second KWin desktop is created if needed.
   - The routine app opens on the Focus desktop.
   - The network lock blocks non-allowlisted outbound traffic.
6. End the routine and confirm FocusOS returns to its home desktop.

If Engage fails, run:

```bash
./build-linux/focusos 2>&1 | tee /tmp/focusos.log
journalctl --user -b --no-pager | tail -200
journalctl -b --no-pager | grep -Ei 'focusos|kwin|nft|pipewire|wireplumber' | tail -200
```

## Daily-driver hardening checklist

Before trusting this for long sessions:

- Confirm `getcap "$(command -v nft)"` still shows `cap_net_admin,cap_net_raw=ep` after system updates.
- Keep one known-good TOTP authenticator enrolled.
- Keep Ethernet available for recovery while testing Wi-Fi.
- Test routine launch with every app you will use daily.
- Put editors, calendar, and contacts in Settings -> Allowed Apps only if you genuinely want them available in every routine.
- Keep `qdbus6` installed. KWin space creation/window pinning depends on it.
- Keep a normal Plasma session available from the login screen as the recovery path.
- Keep a second admin user or live USB available until FocusOS has survived several days of testing.

## Battery and thermals

The 2017 iMac is desktop power, so battery optimization does not matter there. For laptop testing:

```bash
sudo dnf install power-profiles-daemon
sudo systemctl enable --now power-profiles-daemon
powerprofilesctl set power-saver
```

Also reduce ambient media volume/animation during long sessions if fans ramp. FocusOS is mostly Qt Quick and KWin, so the biggest power cost is the compositor plus animated background.

## macOS-only feasibility

A macOS FocusOS shell is possible as a softer product:

- A fullscreen always-on-top Qt shell can mimic the old Dashboard-style overlay.
- A launch agent/helper can relaunch FocusOS if it quits.
- Screen Time, Focus modes, and a packet-filter helper can provide partial app/network blocking.

A truly strict macOS version is not realistic without unacceptable tradeoffs:

- macOS Spaces/Mission Control does not expose a stable public API for creating and pinning spaces like KWin scripting does.
- Blocking arbitrary apps/processes reliably would require MDM supervision, private APIs, SIP reduction, endpoint-security entitlements, or brittle hacks.
- Lowering SIP/root protections for a productivity shell makes the machine less trustworthy than a dedicated Linux session.

So the practical plan is:

1. Make Linux/KWin the strict production version.
2. Keep macOS as a design/testing shell and maybe a soft-focus edition.
3. Revisit macOS only if you are willing to accept an MDM-style managed-device model rather than a true OS-level sandbox.

## References checked

- Fedora KDE Plasma Desktop 44 download: https://www.fedoraproject.org/kde/download/
- Fedora Linux 44 release announcement: https://fedoramagazine.org/announcing-fedora-linux-44/
- Fedora KDE Plasma Desktop 44 notes: https://fedoramagazine.org/whats-new-fedora-kde-plasma-desktop-44/
- KDE KWin scripting tutorial: https://develop.kde.org/docs/plasma/kwin/
- Apple System Integrity Protection support note: https://support.apple.com/en-us/102149
- Apple Platform Security, System Integrity Protection: https://support.apple.com/en-euro/guide/security/secb7ea06b49/web
