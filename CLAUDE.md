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
successful build also validates QML syntax. Test targets: `totp_tests`,
`notes_tests` (NotesStore search, day-grouping + draft rollover) and `stats_tests`
(StatsStore counts/streaks/best-day/averages, focus-rating + distraction-tally
round-trip through stats.json). Each test points `$HOME` at a throwaway dir. Run
all with `ctest --test-dir build`. Adding a test target needs a one-time
`cmake -S . -B build` reconfigure.

## Layout

- `src/main.cpp` — wires the C++ services together and hands them to `ShellWindow`.
  Also handles the `--native-host` arg (browser blocker stdio host).
- `src/core/` — backend services, each exposed to QML as a context property under
  its lowercased name (`routineManager`, `statsStore`, `notesStore`, `totpEngine`,
  `musicEngine`, `systemStatus`, `inspirationStore`, `updater`, `idleMonitor`,
  `diagnostics`):
  - `RoutineManager` — routine model, engage/end/pause, Other-Access mode,
    min-time floor. The `pickApplication`/`pickFile`/`pickFolder` invokables now
    forward to `core/FilePicker` (the QFileDialog plumbing lives there). Exposes a
    live `sessionDistractionsBlocked` (reset each engage) fed by the backend's
    lockdown-watchdog `setDistractionAttemptCallback` (edge-triggered: one count
    per launcher reach); re-broadcasts each hit via `distractionAttemptBlocked`.
  - `StatsStore` — per-session focus log, streak, daily target (`dailyTargetMinutes`
    is read/write from QML), today's progress. Also derives progress analytics
    (`weekFocusMinutes`, `bestDayMinutes`/`bestDayLabel`, `longestStreakDays`,
    `averageSessionMinutes`, `totalSessions`) surfaced in InfoPanel's MISSION
    INSIGHTS card, a 1–5 `focusRating` per session (`recordLastSessionFocusRating`,
    `averageFocusRating`) captured by the MISSION COMPLETE prompt, and a persisted
    lifetime `totalDistractionsBlocked` tally (`noteDistractionBlocked` slot).
  - `NotesStore` — live draft + archived per-session notes & timeline. Full-text
    recall via `searchNotes(query)` (terms ANDed over name/result/body, returns
    rows with highlighted `snippetHtml`); surfaced as the MISSION LOG search box.
    History/search rows also carry a `dateGroup` (TODAY/YESTERDAY/weekday+date)
    and `dayKey` so the log renders as dated sections with outcome filter chips.
  - `TOTPEngine` — gates all admin actions; first-launch QR enrollment.
  - `Logger` — process-wide singleton (`Logger::install()` in `main()`) that tees
    every Qt message to a rotating `~/.focusos/logs/focusos.log` while keeping
    stderr. Exposed to QML as `diagnostics` (tail/clear/reveal) for the SYSTEM tab
    log viewer.
  - others: `MusicEngine`, `SystemStatus`, `InspirationStore`, `Updater`,
    `IdleMonitor`, `MediaKeys`, `Timer`.
  - non-QObject helpers: `AppPaths` (single source for `~/.focusos` paths —
    `dataDirectory()`/`filePath()`; use instead of re-deriving the dir) and
    `FilePicker` (native file/folder dialogs; pure UI, no domain state).
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
  launchers (krunner/plasmashell/kickoff/rofi/dmenu/wofi/etc.). To avoid a
  `fork`/`exec` storm it first does an in-process `/proc` `comm` scan
  (`anyOutlawedProcessPresent`) and only spawns `pkill` when something matches,
  with a forced full sweep every ~20 ticks as a backstop. See
  [docs/build-and-perf.md](docs/build-and-perf.md).
- nftables blocking needs `CAP_NET_ADMIN`.
- **Black screen / software-render fallback.** If the scene graph can't get a
  usable GL/EGL context (a bare kwin_wayland session with a broken/missing GPU
  stack), the window maps but every frame is just the `#050508` clear colour — an
  all-black screen. `main()` arms a `~/.focusos/gpu-render-probe` marker before
  show and clears it on the first painted frame (`frameSwapped`); a launch that
  never renders leaves the marker behind, so the *next* launch auto-falls-back to
  the Qt Quick software renderer (`QT_QUICK_BACKEND=software`). `FOCUSOS_SAFE_GRAPHICS=1`
  forces it manually. In software mode the two procedural `ShaderEffect`s (starfield
  + scanlines, gated on the `safeGraphics` context property) are skipped — the rest
  of the UI renders on the CPU. `ShellWindow` logs `sceneGraphError`, QML load
  errors, and the chosen `graphicsApi` to `~/.focusos/logs/focusos.log`, so a black
  screen is diagnosable rather than silent.
- Bare kwin_wayland session has no Plasma daemons: media keys need KGlobalAccel
  (KGlobalAccel is inert unless `kglobalacceld` runs, so `main()` starts it via
  `backend.ensureGlobalShortcutsDaemon()` before MediaKeys registers), audio needs
  a Qt multimedia backend plugin, brightness needs `brightnessctl`.
  - KWin can drop the global-key grabs across a sleep/resume, so `MediaKeys`
    subscribes to logind `PrepareForSleep` and re-grabs every volume/brightness
    shortcut (remove + re-`setShortcut`) on the resume edge — otherwise the keys
    are dead after the first sleep until re-login.
- Deep-idle sleep is macOS-style and S3-free by default: pause music + `backend.
  freezeBackgroundProcesses()` (SIGSTOP every user GUI app, same conservative
  keep-set as the engage sweep) + DPMS off. Any input / logind resume thaws
  (`thawBackgroundProcesses()` SIGCONTs exactly the tracked PIDs). Whole-machine
  suspend stays opt-in (`deep_sleep_suspend`) and is pinned to s2idle/freeze via
  `packaging/linux/90-focusos-sleep.conf` so it's safe on hardware that
  black-screens out of S3 (e.g. the 2017 iMac). Freeze only runs on the home
  screen (idle is suppressed during a routine). On `deepIdle` the shell also
  quiesces its own pollers so it stops waking the CPU behind the black screen:
  `systemStatus.setLowPowerMode(true)` stops the 30s status refresh (no more
  `pactl` spawn) and `MusicEngine::setSleeping` parks the 4s playback watchdog;
  both resume on wake.
- No Plasma session = nothing runs the user's autostart items. `main()` calls
  `backend.runSessionStartupItems()` once per login (XDG_RUNTIME_DIR marker guards
  against re-running on watchdog respawn): it runs ONLY the user-editable
  `~/.focusos/startup.sh` (edited from the SYSTEM tab of the Settings modal via
  `SystemStatus::{read,write}StartupScript`). This is the Toshy / tray-agent hook.
  We deliberately do NOT replay `~/.config/autostart/*.desktop` anymore — a stray
  entry there pulled in the whole Plasma desktop on top of FocusOS, so the user
  lists exactly what they want (e.g. `toshy-services-restart`) in startup.sh.
- Inspiration media (AmbientLayer) is home-screen wallpaper only; it's suppressed
  during a routine (`showMedia: !routineManager.active`). `showMedia:false` now also
  tears down the MediaPlayer so a hidden layer never decodes video.
- The kiosk watchdog respawns FocusOS on quit; real sign-out must terminate the
  logind session (`loginctl`).
- The user treats extra `QQuickView`s as unwanted "second processes" — keep to one
  window; a flag in the WM is unacceptable.

For anything deeper (network blocker pipeline, bare-session setup, session exit),
see the user's persistent memory index referenced from the project notes.
