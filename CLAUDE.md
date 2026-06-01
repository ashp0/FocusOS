# CLAUDE.md

Agent orientation for FocusOS. Read this first; it saves you re-deriving the map.
Deep user-facing detail lives in [README.md](README.md). This file is the code map.

## What it is

A fullscreen Qt6/QML "mission control" shell for deep work. It runs **on top of
KWin** as the session shell (NOT a compositor — don't scaffold wlroots/Wayland).
A *routine* launches a set of apps + URLs, optionally applies an nftables
outbound allowlist, and runs a countdown the user can't escape until it ends or
an admin unlocks Settings with a TOTP code.

**Linux/KDE Plasma 6 (Wayland) is the only target that matters.** macOS is
deprioritized — keep it compiling, don't invest. The user tests on a KDE laptop
with Brave.

## Build

```bash
cmake --build build          # reconfigure-free incremental build (build/ already exists)
```

The build links `Qt6::DBus` on Linux. QML is AOT-compiled into the binary, so a
successful build also validates QML syntax. `totp_tests` is the only test target.

## Layout

- `src/main.cpp` — wires the C++ services together and hands them to `ShellWindow`.
  Also handles the `--native-host` arg (browser blocker stdio host).
- `src/core/` — backend services, each exposed to QML as a context property under
  its lowercased name (`routineManager`, `statsStore`, `notesStore`, `totpEngine`,
  `musicEngine`, `systemStatus`, `inspirationStore`, `updater`, `idleMonitor`):
  - `RoutineManager` — routine model, engage/end/pause, the picker dialogs
    (`pickApplication`, `pickFile`), Other-Access mode, min-time floor.
  - `StatsStore` — per-session focus log, streak, daily target (`dailyTargetMinutes`
    is read/write from QML), today's progress.
  - `NotesStore` — live draft + archived per-session notes & timeline.
  - `TOTPEngine` — gates all admin actions; first-launch QR enrollment.
  - others: `MusicEngine`, `SystemStatus`, `InspirationStore`, `Updater`,
    `IdleMonitor`, `MediaKeys`, `Timer`.
- `src/shell/` — QML UI + `ShellWindow` (sets the QML context properties).
  `Main.qml` is the root; `MissionView.qml` is the active-routine screen;
  `InfoPanel.qml` is the stats/notes panel; `UnlockModal.qml` holds the admin
  Settings incl. the ROUTINES editor tab; `ActivitiesPanel.qml` is the launcher.
- `src/platform/` — `PlatformBackend.h` interface; `linux/LinuxBackend.cpp` and
  `macos/MacBackend.cpp` (+ `MacBackendNative.mm` shim) implement it.
  `linux/NetGate.cpp` is the nftables allowlist.
- `src/blocker/` — browser-extension native-host + signed-binary policy.

## Routine app entries (parsed by each backend's `launchApps`)

Each entry is a shell-quoted command string (`QProcess::splitCommand`). Dispatch:
- `kiosk:<url>` → chromium-family browser in single-page `--app` fullscreen mode.
- `*.desktop` → launched as a desktop entry.
- an existing **non-executable file** → opened in its **default application**
  ("Open File" workflow; Linux prefers a concrete PDF/ebook reader via
  `launchFile`, else `xdg-open`; macOS uses `/usr/bin/open`).
- anything else (abs path executable, or a bare command like `flatpak run …`) →
  exec'd directly with its args.

## Gotchas

- Routine apps + shell all run on the user's **single current desktop** — FocusOS
  no longer spins up a separate "Focus" virtual desktop or pins windows to it (that
  was just churn + an extra KWin-scripting surface). `restoreShellPlacement()` is a
  no-op on Linux now; ShellWindow raises the shell back to the foreground when a
  routine ends.
- During a routine, LinuxBackend runs a ~1.5s lockdown watchdog that pkills
  launchers (krunner/plasmashell/kickoff/rofi/dmenu/wofi/etc.).
- nftables blocking needs `CAP_NET_ADMIN`.
- Bare kwin_wayland session has no Plasma daemons: media keys need KGlobalAccel,
  audio needs a Qt multimedia backend plugin, brightness needs `brightnessctl`.
- The kiosk watchdog respawns FocusOS on quit; real sign-out must terminate the
  logind session (`loginctl`).
- The user treats extra `QQuickView`s as unwanted "second processes" — keep to one
  window; a flag in the WM is unacceptable.

For anything deeper (network blocker pipeline, bare-session setup, session exit),
see the user's persistent memory index referenced from the project notes.
