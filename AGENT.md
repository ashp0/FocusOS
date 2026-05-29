# AGENT.md

Orientation for AI agents working in FocusOS. Read this first to avoid
re-deriving the layout. Keep it accurate when structure changes.

## What this is

FocusOS is a full-screen **productivity shell for deep work** — a Qt6/QML app
that takes over the screen during a "routine," launches only the apps you've
allowed, and locks down everything else (network, other apps, sleep) until the
session ends or you unlock with a TOTP code. C++/Qt backend, QML frontend.

- **Build system:** CMake ≥ 3.24, C++20, Qt6 ≥ 6.7 (Core, Gui, Multimedia,
  Network, Qml, Quick, QuickControls2, Test, Widgets).
- **Platforms:** macOS (Apple Silicon by default) and Linux/KDE Plasma 6. There
  is no third platform — `main.cpp` `#error`s otherwise.
- **User testing:** primarily KDE Plasma 6 with Brave. The Linux lockdown story
  (network gate, watchdog, session swapping, in-place updater) is the deep end.

## Build & test

```sh
cmake -B build            # configure (first time)
cmake --build build       # builds the `focusos` app + `totp_tests`
ctest --test-dir build    # runs the TOTP unit tests
```

The macOS build produces `build/focusos.app`. **Note:** Linux-only code lives
behind `#if defined(Q_OS_LINUX)` / `elseif(UNIX)` in CMake, so a macOS build
will NOT catch Linux compile errors (and vice versa). Cross-platform code
(`PlatformBackend.h`, `RoutineManager`, `main.cpp`'s crash handler) is the
common gotcha — a method called through `PlatformBackend*` must be declared on
the base class even if only one platform overrides it.

## Directory map

```
CMakeLists.txt          Single top-level build file; lists every source + QML
src/
  main.cpp              Entry point. Picks the platform backend, installs the
                        crash-cleanup signal handler (Linux), constructs the
                        core C++ objects, wires their signals, and hands them
                        to ShellWindow. Single-instance via QLockFile in ~/.focusos.
  core/                 Backend logic — QObjects exposed to QML as properties.
    RoutineManager.*    The heart. QAbstractListModel of routines; starts/stops
                        sessions, drives the platform lockdown, owns timer +
                        always-allowed-apps state. Largest file.
    Timer.*             FocusTimer — countdown for a routine session.
    NotesStore.*        Per-routine scratch notes, archived per day.
    MusicEngine.*       Ambient music playback (Qt Multimedia).
    StatsStore.*        Focus-minute history / daily stats.
    SystemStatus.*      Battery, clock, etc. for the info panel.
    InspirationStore.*  Quotes/wallpaper assets shown in the shell.
    TOTPEngine.*        TOTP secret + enrollment URI + QR code. Gates unlock
                        and session recovery. Covered by tests/totp_tests.cpp.
    Updater.*           In-app git-pull-and-rebuild updater with a 30-min crash
                        probation + auto-revert. Orchestrates packaging/linux
                        shell scripts. Linux-only model. (See header comment.)
  platform/
    PlatformBackend.h   Abstract interface every backend implements. App control,
                        network policy, sleep inhibition, watchdog, session
                        recovery. ADD A VIRTUAL HERE before calling it through
                        the base pointer.
    macos/MacBackend.*  macOS implementation (lighter lockdown).
    linux/LinuxBackend.* Linux/KDE implementation (full lockdown).
    linux/NetGate.*     Builds/applies/drops a firewall ruleset for the
                        allowed-hosts network policy.
  shell/
    ShellWindow.*       QQuickView subclass; the single top-level window that
                        hosts all QML and receives the core objects as context.
    *.qml               UI. Main.qml is the root; UnlockModal, MissionView,
                        ActivitiesPanel, InfoPanel, NotesDrawer, AmbientLayer,
                        ProgressOverlay are the panels/overlays.
assets/                 Bundled fonts, ambient music, QML theme.js. Compiled
                        into the binary via qt_add_qml_module RESOURCES.
resources/              (currently empty)
tests/totp_tests.cpp    Qt Test unit tests for TOTPEngine.
packaging/linux/        Install/update/revert/watchdog/session shell scripts +
                        KDE config (kwinrc, kglobalshortcutsrc) + .desktop file.
                        This is the privileged-free, no-sudo lockdown machinery.
build/                  CMake build tree (generated; not source of truth).
```

## Architecture notes

- **C++ owns logic, QML owns presentation.** Core objects are instantiated in
  `main.cpp`, connected via signals there, and exposed to QML through
  `ShellWindow`. To surface new data to the UI, add a `Q_PROPERTY`/`Q_INVOKABLE`
  on the relevant core class, not logic in QML.
- **Platform abstraction:** all OS-specific behavior goes through
  `PlatformBackend`. `RoutineManager` and `main.cpp` only ever hold a
  `PlatformBackend*`. Keep platform `#ifdef`s out of core/ — put them behind the
  backend interface.
- **State on disk:** `~/.focusos/` holds the instance lock, the active-session
  checkpoint (`active.json`), and updater state. On Linux a respawn watchdog
  relaunches FocusOS while a checkpoint is armed, so cleanup must be
  crash-safe and idempotent (see the crash handler in `main.cpp` and
  `LinuxBackend::releaseDisplaySleepInhibitors`).
- **Single window:** FocusOS deliberately runs as one process / one window.
  Avoid spawning auxiliary QQuickViews or extra processes.
