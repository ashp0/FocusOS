# FocusOS Linux Desktop Replacement Guide

FocusOS is a full-screen intentional-work shell. On Linux, the goal is not to theme KDE Plasma. The goal is to stop launching Plasma, start FocusOS as the login session, and make the normal desktop/dock/application-launcher path disappear.

This guide uses "Linux" for the platform name. The product can still use the Lennox name if that is the brand language.

## Security Model

FocusOS can strongly restrict a normal non-admin user. It cannot make a computer impossible to use against someone with root access, full disk access, firmware access, or physical boot media. The serious deployment model is:

- A dedicated `focusos` daily-use account with no sudo/admin rights.
- A separate administrator account used only for maintenance.
- SDDM starts the FocusOS Wayland session automatically instead of KDE Plasma.
- No Plasma shell, dock, launcher, or desktop menu is started.
- Routine mode applies an nftables outbound network allowlist.
- Other Access requires the six-digit TOTP code and opens a temporary unrestricted window.
- For production-grade "only allowed routine apps can exist", FocusOS should become the Wayland compositor or launch apps through a narrow supervisor that owns the app allowlist.

## Modes

Use `kwin` mode first. It supports external routine apps because KWin is the compositor, but Plasma is not started.

Use `cage` mode when you want the strictest single-app kiosk. Cage intentionally runs one maximized application, so it is best for routines that happen inside FocusOS itself rather than routines that need separate apps.

The installed session wrapper supports both:

```bash
FOCUSOS_MODE=kwin   # FocusOS shell, KWin compositor, routine apps can appear.
FOCUSOS_MODE=cage   # Single-app kiosk, strongest immediate lock, no external app workflow.
```

## Install Dependencies

FocusOS requires Qt 6.7 or newer, CMake, Ninja, a C++20 compiler, Wayland support, nftables, xdg-open, `pactl`, writable backlight permissions, and either KWin Wayland or Cage. For inspiration videos, make sure the Qt Multimedia runtime backend and normal system codecs are installed; many distros provide this through FFmpeg or GStreamer packages alongside Qt Multimedia.

Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja ccache qt6-base qt6-declarative qt6-multimedia qt6-wayland qt6-tools kwin cage nftables sddm xdg-utils wireplumber libpulse
```

Fedora:

```bash
sudo dnf install cmake ninja-build ccache gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel qt6-qtwayland kwin-wayland cage nftables sddm xdg-utils wireplumber pulseaudio-utils
```

Ubuntu package names vary by release. Install the Qt 6.7+ development packages from your distro, KDE/Qt PPA, or the Qt online installer, plus:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build ccache nftables sddm cage xdg-utils wireplumber pulseaudio-utils
```

## Build And Install FocusOS

From the repository root:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
sudo mkdir -p /opt/focusos/bin
sudo install -m 0755 build-linux/focusos /opt/focusos/bin/focusos
```

For faster incremental rebuilds on Linux, configure with ccache:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build-linux -j"$(nproc)"
```

If your Linux build output path differs, locate the binary with:

```bash
find build-linux -type f -perm -111 -name focusos
```

## Ambient Inspiration Media

On first launch, FocusOS creates `~/.focusos/inspiration/`. Drop supported images or videos there and the shell will fold them into the background. The media layer loops continuously: images rotate as a slow slideshow, videos advance when playback ends, and single-item folders repeat the same item. At desktop launch the media starts visible enough to notice, then fades over a 30-minute countdown until it is barely noticeable.

If images work but video is blank or never advances on Linux, install the missing Qt Multimedia backend/codecs for your distro before debugging FocusOS itself.

## Install The Login Session

Install the wrapper and Wayland session file:

```bash
sudo install -m 0755 packaging/linux/focusos-session.sh /usr/local/bin/focusos-session
sudo install -m 0644 packaging/linux/focusos.desktop /usr/share/wayland-sessions/focusos.desktop
```

The wrapper defaults to `kwin` mode. To force the single-app Cage mode, edit `/usr/local/bin/focusos-session` and set:

```bash
FOCUSOS_MODE="${FOCUSOS_MODE:-cage}"
```

Enable SDDM if it is not already the display manager:

```bash
sudo systemctl enable sddm
```

## Create The Restricted User

Create a daily-use user and remove admin privileges:

```bash
sudo useradd -m -s /bin/bash focusos
sudo passwd focusos
sudo gpasswd -d focusos sudo 2>/dev/null || true
sudo gpasswd -d focusos wheel 2>/dev/null || true
```

Keep your existing admin user. Do not test hardening without a known admin recovery path.

## Auto-Login Directly Into FocusOS

Create `/etc/sddm.conf.d/10-focusos.conf`:

```ini
[Autologin]
User=focusos
Session=focusos.desktop
Relogin=true
```

Then restart:

```bash
sudo systemctl restart sddm
```

The login should now enter FocusOS instead of KDE Plasma.

## Hide Plasma As A Daily Option

After FocusOS is confirmed working, move Plasma session files out of the display manager list. Keep a backup so you can restore.

```bash
sudo mkdir -p /usr/share/wayland-sessions.disabled
sudo mv /usr/share/wayland-sessions/plasma*.desktop /usr/share/wayland-sessions.disabled/ 2>/dev/null || true
sudo mkdir -p /usr/share/xsessions.disabled
sudo mv /usr/share/xsessions/plasma*.desktop /usr/share/xsessions.disabled/ 2>/dev/null || true
```

This does not uninstall KDE. It removes the easy login choice.

## Reduce Escape Routes

Disable automatic extra gettys and make user processes die on logout:

```bash
sudo mkdir -p /etc/systemd/logind.conf.d
sudo install -m 0644 packaging/linux/90-focusos-logind.conf /etc/systemd/logind.conf.d/90-focusos.conf
sudo systemctl restart systemd-logind
```

Mask extra text consoles:

```bash
for n in 2 3 4 5 6; do
  sudo systemctl mask "getty@tty${n}.service" "autovt@tty${n}.service"
done
```

Optional kiosk machines can also disable SysRq:

```bash
echo 'kernel.sysrq = 0' | sudo tee /etc/sysctl.d/90-focusos.conf
sudo sysctl --system
```

## Enable Volume, Brightness, And Battery Status

FocusOS exposes system volume and brightness controls in the bottom toolbar so routine sessions do not require Plasma. On Linux it checks, in order:

- Volume: `pactl get-sink-volume @DEFAULT_SINK@` and `pactl set-sink-volume @DEFAULT_SINK@ <value>%`.
- Brightness: `/sys/class/backlight/*/max_brightness` and `brightness`; if no backlight device exists, the slider is hidden.
- Battery: `/sys/class/power_supply/BAT*` or any supply reporting `type=Battery`, including Asahi-style names such as `macsmc-battery`; if no battery exists, the widget is hidden.

If brightness changes do not apply, add a narrow udev rule or group permission that allows the FocusOS user to write the chosen `brightness` file, then log out and back in.

## Enable Network Locking

FocusOS uses nftables for routine network policy. During a routine, outbound traffic is dropped except loopback, DNS, and resolved IPv4/IPv6 addresses for the routine allowlist. Routine engagement now fails closed: if FocusOS cannot install the nftables table, the routine does not start.

Enable nftables:

```bash
sudo systemctl enable --now nftables
```

The prototype app invokes `nft` directly. For a dedicated FocusOS machine, the simplest prototype path is:

```bash
sudo setcap cap_net_admin,cap_net_raw+ep "$(command -v nft)"
getcap "$(command -v nft)"
```

Production should replace this with a root-owned, narrow D-Bus or setuid helper that only allows these two operations:

```bash
nft -f -
nft delete table inet focusos
```

That keeps the daily user from getting a general firewall administration tool.

## Development / Fast Iteration

Use three loops instead of trying to make one environment do everything:

- macOS loop: fastest for QML layout, animations, typography, and basic C++ compile checks.
- Linux VM loop: best for KWin, nftables, xdg-open, `.desktop` launch files, `pactl` volume, backlight permissions, and session behavior.
- Bare-metal Asahi loop: final pass for battery, backlight, real input devices, suspend/resume, and the actual daily-driver login session.

From macOS, create an arm64 Fedora or Arch Linux VM in UTM with hardware virtualization enabled. On Linux hosts, use `virt-manager` with QEMU/KVM and a Fedora or Arch image. Give the VM enough CPU cores for Ninja builds, enable OpenSSH in the guest, and install the FocusOS dependencies from the section above.

Mount this repository into the guest so editing stays fast on the host and compilation happens in Linux:

- UTM: add the repo as a shared directory and mount it in the guest with virtiofs, for example at `/mnt/focusos`.
- QEMU/KVM or virt-manager: add a virtiofs filesystem device, or use `sshfs ashwin@host:/Users/ashwinpaudel/Desktop/FocusOS ~/FocusOS` from the guest if virtiofs is inconvenient.

Configure the VM build with ccache:

```bash
cd /mnt/focusos
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build-linux -j"$(nproc)"
```

One host-side rebuild and relaunch loop:

```bash
ssh focusos-vm 'cd /mnt/focusos && cmake --build build-linux -j"$(nproc)" && { pkill -x focusos || true; } && FOCUSOS_BIN="$PWD/build-linux/focusos" FOCUSOS_MODE=kwin packaging/linux/focusos-session.sh >/tmp/focusos.log 2>&1 &'
```

For destructive session tests, keep a clean base qcow2 and boot disposable overlays:

```bash
qemu-img create -f qcow2 -F qcow2 -b focusos-base.qcow2 focusos-test.qcow2
```

Delete `focusos-test.qcow2` after each broken experiment and recreate it from the base image. This is much faster than reinstalling a desktop stack after testing SDDM, logind, nftables, or KDE shell teardown.

## TOTP Setup On iPhone

You do not need a different app if Apple Passwords works for you.

First launch:

1. Open Other Access.
2. Scan the QR code with the iPhone Camera app or Passwords app.
3. If scan fails, open Passwords, go to Codes, tap `+`, choose Setup Key, and enter the grouped setup key shown under the QR code.
4. Enter the current six-digit code into FocusOS.

If codes are rejected, sync time on both devices:

```bash
timedatectl set-ntp true
timedatectl status
```

FocusOS now uses a shorter TOTP label in the QR payload and shows the setup key grouped in four-character chunks so manual entry is less painful.

## Test Checklist

- Reboot and confirm the `focusos` user lands directly in FocusOS.
- Confirm no Plasma panel, dock, launcher, desktop icons, or app menu appears.
- Press common launcher shortcuts such as `Alt+Space`, `Meta`, and `Ctrl+Alt+T`; they should not expose a launcher.
- Start a routine and confirm the routine timer begins.
- Put one image and one short video in `~/.focusos/inspiration/`, relaunch, and confirm the background loops and fades over time.
- Open a blocked site from an allowed app and confirm it fails.
- Open an allowed routine URL and confirm it works after DNS resolution.
- Use Other Access with the six-digit code and confirm the unrestricted window expires.
- Pull power or force a reboot during a test routine, then relaunch FocusOS and confirm the partial work appears as an interrupted session in stats.

## Recovery

From an admin TTY, SSH session, or recovery boot:

```bash
sudo mv /usr/share/wayland-sessions.disabled/plasma*.desktop /usr/share/wayland-sessions/ 2>/dev/null || true
sudo mv /usr/share/xsessions.disabled/plasma*.desktop /usr/share/xsessions/ 2>/dev/null || true
sudo rm -f /etc/sddm.conf.d/10-focusos.conf
for n in 2 3 4 5 6; do
  sudo systemctl unmask "getty@tty${n}.service" "autovt@tty${n}.service"
done
sudo rm -f /etc/systemd/logind.conf.d/90-focusos.conf
sudo systemctl restart systemd-logind
sudo systemctl restart sddm
```

## Production Hardening Roadmap

The current Linux session replacement is strong enough for a non-admin productivity account. The production direction should be stronger:

- Make FocusOS a Wayland compositor with QtWaylandCompositor or a wlroots-based compositor. Then FocusOS can decide which surfaces exist.
- Launch routine apps under a FocusOS supervisor with cgroups, process lifetime ownership, and an allowlist.
- Replace direct `nft` execution with a tiny privileged helper.
- Add AppArmor or Landlock profiles per routine so allowed apps cannot spawn browsers, terminals, or package managers.
- Add a watchdog session service that immediately restarts FocusOS if it crashes.

## References

- Freedesktop Desktop Entry Specification: https://specifications.freedesktop.org/desktop-entry/latest-single/
- Cage Wayland kiosk compositor: https://github.com/cage-kiosk/cage
- UTM shared directories: https://docs.getutm.app/settings-qemu/sharing/
- Apple Virtualization Linux guests: https://developer.apple.com/documentation/virtualization/virtualize-linux-on-a-mac
- QEMU qcow2 images: https://www.qemu.org/docs/master/system/images
- QEMU image utility: https://www.qemu.org/docs/master/tools/qemu-img.html
- CMake compiler launcher / ccache support: https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html
- systemd service restart behavior: https://www.freedesktop.org/software/systemd/man/249/systemd.service.html
- systemd process killing behavior: https://www.freedesktop.org/software/systemd/man/latest/systemd.kill.html
- systemd-logind virtual terminal options: https://www.freedesktop.org/software/systemd/man/latest/logind.conf.html
- Apple iPhone verification code setup: https://support.apple.com/guide/iphone/automatically-fill-in-verification-codes-ipha6173c19f/ios
- Apple iCloud Keychain verification-code URL support: https://developer.apple.com/documentation/AuthenticationServices/securing-logins-with-icloud-keychain-verification-codes
