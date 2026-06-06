# Build & Performance Notes

Living reference for FocusOS's build ergonomics and the runtime
performance/power decisions. Pairs with `architecture-decisions.md` (product
boundary) and `lockdown.md` (enforcement surface).

## Guiding principle: do less, don't compute faster

FocusOS is a timer/event-driven Qt6/QML shell that launches apps, flips a
firewall, and polices a deny-list. It has **no high-throughput I/O path and no
data-parallel numeric kernel.** Its real costs are:

1. **Needless wakeups while the machine is idle** — periodic timers and stall
   watchdogs that keep the CPU from parking.
2. **`fork`/`exec` inside a polling loop** — the lockdown watchdog spawning
   `pkill` several times a second for the whole routine.
3. **Decorative animation that runs when nothing can see it.**

So the optimization work is about *not doing work*, plus making the build
pleasant. This is deliberately **not** a place for io_uring, AVX2, or compute
shaders — see "Roads not taken" below for why each would be cargo-cult here.

## Runtime power optimizations (2026-06)

### Deep-idle quiescence

When `IdleMonitor` reaches `deepIdle` (the macOS-style soft sleep: music off,
GUI apps `SIGSTOP`'d, DPMS off — `main.cpp`), the shell should also stop
*itself* from waking the CPU. Two pollers used to keep ticking behind the black
idle screen:

- **`SystemStatus`** ran a 30s refresh that read battery/brightness sysfs **and
  spawned `pactl`** for volume. `SystemStatus::setLowPowerMode(true)` now stops
  that timer on `deepIdle` and resumes (with an immediate reconcile) on wake.
- **`MusicEngine`** ran a 4s playback-stall watchdog unconditionally.
  `setSleeping(true)` now parks it (playback can't stall while intentionally
  paused) and `setSleeping(false)` re-arms it.

Net: a "sleeping" FocusOS no longer fires a timer every 4s, nor spawns a process
every 30s, on a machine that is supposed to be sipping power. This matters
specifically on the 2017 iMac whose boot drive is an external T7 — keeping I/O
genuinely quiet during sleep keeps that drive's LED dark.

### Lockdown watchdog: `/proc` pre-check instead of a `pkill` storm

`LinuxBackend::tickLockdownWatchdog()` runs every 1.5s for the entire routine.
It used to *always* `QProcess::startDetached("sh", "-c", "pkill -x …; pkill -f
…")` — three process creations per tick, each `pkill` walking all of `/proc`
itself. Over a multi-hour routine that is tens of thousands of `fork`/`exec`s
for a deny-list that, in the steady state, matches nothing.

Now the tick first does a cheap **in-process `/proc` scan**
(`anyOutlawedProcessPresent`, modelled on `freezeBackgroundProcesses`): read
each `/proc/<pid>/comm` and test membership in a `QSet<QByteArray>` of the
deny-list names truncated to 15 bytes (which is exactly what `pkill -x`
compares against). Only if something matches do we pay for the `pkill` spawn. In
the common case the per-tick cost drops from *3 process creations + 2 full
`/proc` walks* to *one `/proc` walk of tiny reads, zero spawns.*

**Safety:** the scan only decides *whether to spawn the proven `pkill`*; it never
changes *what* gets killed. A forced full sweep every ~20 ticks (~30s) is a
backstop, so even a process that renamed its `comm` via `prctl()` to dodge the
pre-check cannot survive longer than ~30s — it can never become a permanent
lockdown hole. The `comm` matching is faithful to `pkill -x` for every realistic
launcher (none of the outlawed apps rename their `comm`).

### Misc

- **Executable-path memoisation** (`SystemStatus::cachedExecutable`):
  `QStandardPaths::findExecutable` walks `$PATH` stat()-ing candidates on every
  call. The status refresh and every volume/brightness write hit the same few
  tools (`pactl`, `brightnessctl`); their location is stable for the process
  lifetime, so it's resolved once and cached.
- **Decorative-animation gating** (`InfoPanel.qml`): the telemetry-LED pulse, the
  1s "stardate" clock `Timer`, and the sweeping footer marker now run only while
  the panel is visible **and** the idle screensaver isn't up
  (`running: root.visible && !idleMonitor.idle`). A clock nobody can see needn't
  tick; it snaps back to real time on re-show.

The big visual cost — the full-screen starfield fragment shader — was **already**
bounded by a previous pass (rendered at `renderScale 0.5`, clock capped to 30fps
via accumulation, and `FrameAnimation` paused whenever the layer is hidden /
unfocused / DPMS-off). It was reviewed and left as-is.

## Roads not taken (and why)

The brief floated io_uring, epoll, mmap, AVX2/SIMD, NUMA, zero-copy, and GPU
compute. Applying them here would add dependencies and risk to a working product
for **no measurable gain**, because the workload doesn't have the shape any of
them accelerate:

- **io_uring / epoll** — there is no I/O hot path. Config/state reads are tiny
  and one-shot; the only repeated reads are the `/proc/<pid>/comm` scan, which is
  `opendir`+`read` bound on ~hundreds of 16-byte files. A submission/completion
  ring (plus a `liburing` dependency) wouldn't beat plain `read()` there and
  would complicate code that already can't be unit-tested off-target. Qt's event
  loop already uses the platform poller for socket/timer readiness.
- **mmap / zero-copy** — the files touched are sysfs/procfs pseudo-files (not
  mmap-able in a useful way) and small assets Qt already maps via its resource
  system. The watchdog scan was made allocation-light (reused small
  `QByteArray`, in-place newline strip) — that *is* the relevant win.
- **AVX2 / SIMD** — the only arithmetic-heavy code is TOTP HMAC-SHA1, which runs
  a handful of times per unlock (not hot), and the starfield, which is already a
  GPU fragment shader. There is no CPU pixel/audio/DSP loop to vectorize.
- **GPU compute shaders** — the ambient visual is already a fragment shader,
  render-scaled and frame-capped. A compute pass wouldn't lower its cost and
  would raise the minimum GL/Vulkan feature requirement on old iMac hardware.
- **NUMA** — the target is a single-socket all-in-one. There is no NUMA topology
  to place memory against.

The genuinely "Linux-specific power" levers that *did* apply: replacing
`fork`/`exec` with a direct `/proc` scan, aligning timer quiescence with
logind/DPMS sleep, and keeping the hot loop allocation-light.

## Build system

Everything below is opt-in or auto-degrading, so the default
`cmake --build build` is unchanged unless you ask for a flag.

| Lever | Default | Notes |
|------|---------|-------|
| `ccache` | auto | Used as the compiler launcher if found. No-op otherwise. Biggest rebuild-loop win on the Fedora box. |
| Precompiled headers | on | Core Qt headers (`QObject`, `QString`, `QTimer`, …). The ObjC++ shim is excluded (`SKIP_PRECOMPILE_HEADERS`) because it's ARC and the PCH isn't. |
| `-Wall -Wextra` | on (`FOCUSOS_WARNINGS`) | The tree is warning-clean as of this pass; keep it that way. No `-Werror` (wouldn't want a stray Qt-version warning to brick a build). |
| ASan + UBSan | off (`FOCUSOS_SANITIZE`) | `cmake -B build -DFOCUSOS_SANITIZE=ON`. UBSan is `-fno-sanitize-recover` so it aborts on the first violation. |
| LTO | Release only | Probed via `check_ipo_supported`; skipped cleanly if the toolchain lacks it. |
| Dead-strip / `--gc-sections` | Release only | `-Wl,-dead_strip` (macOS) / `-ffunction-sections -fdata-sections` + `--gc-sections` (Linux) for a leaner binary. |
| Frame pointers | Debug only | `-fno-omit-frame-pointer` so `perf`/Instruments stacks stay readable for free. |

Common invocations:

```bash
cmake --build build                              # day-to-day Debug build
cmake -B build -DFOCUSOS_SANITIZE=ON && cmake --build build   # ASan/UBSan session
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel  # lean release
ctest --test-dir build --output-on-failure       # run the TOTP suite
```

## Verification status

Built and run on macOS (the dev box for this pass); the Fedora iMac is the
deployment target.

- **Compile-verified on macOS:** all `src/core` changes, the `InfoPanel.qml`
  bindings (QML is AOT-compiled into the binary, so the build validates them),
  the CMake changes, and the expanded TOTP suite (**17 tests, all green**, up
  from 7).
- **Off-target code:** `LinuxBackend.cpp` is not compiled on macOS. The new
  `anyOutlawedProcessPresent` helper + watchdog gate were extracted and
  **compiled standalone against Qt Core** to validate the API usage, but the
  integration and behavior still need a build/run on the Fedora iMac.

Suggested manual checks on Linux after pulling this:

1. Engage a routine; open KRunner (or `plasmashell`) — it should still be killed
   within ~1.5s, same as before.
2. Watch fork churn drop: `vmstat 2` (the `system → in/cs` columns) or
   `perf stat -a -e task-clock,context-switches sleep 10` during an idle routine
   should show far less activity than before.
3. Let the home screen go deep-idle and confirm no `pactl` spawns
   (`pidof pactl`, or `perf trace -e execve`) and the T7 LED stays dark.
