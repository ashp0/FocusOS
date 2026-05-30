# Agent Guide

Orientation for AI agents and contributors working in FocusOS. Read this before
editing so you can move with the shape of the repo instead of rediscovering it.

## Project Shape

FocusOS is a Qt 6 / QML productivity shell for deep work. The C++ backend owns
routine state, platform control, TOTP, notes, stats, media, updates, and browser
policy. QML owns the single-window presentation.

The strict product target is a Linux/KDE Wayland login session for a dedicated
non-admin user. macOS is supported as a softer shell and fast UI development
loop, but it is not the same security boundary.

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Linux foreground run:

```bash
./build/focusos
```

macOS foreground run:

```bash
open build/focusos.app
```

Linux-only code is behind the UNIX platform branch in CMake and `Q_OS_LINUX`
guards. A macOS build will not catch Linux compile errors, and a Linux build
will not catch macOS backend mistakes.

## Directory Map

```text
CMakeLists.txt
README.md
INSTALL.md
AGENT.md
docs/
  architecture-decisions.md
  browser-blocker.md
assets/
  fonts/                  Bundled UI fonts.
  github/                 README screenshots.
  music/                  Bundled fallback ambient audio.
  qml/theme.js            Shared QML theme constants.
src/
  main.cpp                Backend selection, signal wiring, single-instance guard.
  blocker/                Browser policy serialization and native-host mode.
  core/                   QObject services exposed to QML.
  platform/
    PlatformBackend.h     The OS boundary. Add virtuals here before core calls.
    linux/                KWin, nftables, app control, watchdog, recovery.
    macos/                macOS implementation for the softer shell.
  shell/                  ShellWindow and QML UI files.
resources/
  Cold Turkey/            Pristine upstream extension snapshot.
  focusos-blocker/        FocusOS extension fork and native host dev files.
scripts/                  Blocker packaging, update, policy, diagnostics.
packaging/linux/          SDDM session, watchdog, updater, restore scripts, KDE config.
tests/                    Qt Test coverage.
build/                    Generated CMake tree; not source of truth.
```

## Core Responsibilities

- `RoutineManager` is the center of routine editing, engagement, timer state,
  app launch coordination, lockdown, stats handoff, and active-session recovery.
- `PlatformBackend` is the only interface core code should use for OS-specific
  behavior. Keep platform `#ifdef`s out of core logic when a backend virtual is
  the cleaner boundary.
- `ShellWindow` hosts one `QQuickView`. FocusOS should stay a single-window app.
- `TOTPEngine` owns enrollment URI, QR code generation, and six-digit code
  verification. `tests/totp_tests.cpp` covers this path.
- `Updater` orchestrates the Linux no-sudo update/revert scripts.
- `src/blocker` and `resources/focusos-blocker` work together: the app writes
  signed policy, and the extension enforces browser navigation allowlists.

## Runtime State

FocusOS writes user state under `~/.focusos/`:

- `routines.json` - routine definitions.
- `config.json` - settings shared across panels.
- `stats.json` - completed, interrupted, unlocked, and active session records.
- `active.json` - armed-routine checkpoint watched by the Linux watchdog.
- `totp.enrolled` and TOTP secret files - unlock enrollment state.
- `inspiration/` and `music/` - user-provided ambient media.
- `blocker/` - browser blocker policy, logs, CRX dist files, and heartbeat.

Treat writes to these files as crash-sensitive. The Linux watchdog may relaunch
the shell while a routine is armed.

## Editing Guidelines

- Keep code changes scoped to the subsystem you are touching.
- Do not move platform behavior into QML.
- Add `Q_PROPERTY` or `Q_INVOKABLE` on a core object when QML needs new data or
  actions.
- Preserve unknown JSON keys in user config files unless a migration explicitly
  owns them.
- Any method called through `PlatformBackend*` must be declared on
  `PlatformBackend.h` even if only one platform implements real behavior.
- After touching browser blocking, read [docs/browser-blocker.md](docs/browser-blocker.md)
  and test native-host delivery, not just C++ compilation.
- After touching permanent install or recovery, read [INSTALL.md](INSTALL.md)
  and the scripts in `packaging/linux/`.

## Useful Commands

```bash
rg --files
rg "Q_PROPERTY|Q_INVOKABLE" src
cmake --build build
ctest --test-dir build --output-on-failure
scripts/blocker-doctor.sh
```
