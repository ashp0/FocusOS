# AGENT.md

> **Read this first.** It is the orientation + rulebook for any AI agent (or human)
> editing FocusOS. It exists to keep changes aligned with the architecture *and* to
> keep your token usage low. For the deep per-file code map and platform gotchas see
> [CLAUDE.md](CLAUDE.md); for subsystem detail see [docs/](docs/). Do not duplicate
> those here — link to them.

---

## 1. Architecture Blueprint

**What it is.** FocusOS is a fullscreen Qt 6 / QML "mission control" shell for deep
work. It is **not** a compositor — it runs *on top of* KWin as the session shell. A
*routine* launches a set of apps + URLs, optionally applies an nftables outbound
allowlist, and runs a countdown the user cannot escape until it ends or an admin
unlocks Settings with a TOTP code.

**Primary target:** Linux / KDE Plasma 6 (Wayland), single dedicated non-admin user.
**macOS** is kept compiling as a softer shell + fast UI loop — it is *not* the same
security boundary. When the two conflict, **Linux wins.**

**Core technologies:** C++17, Qt 6 (Quick, QML, DBus on Linux), CMake (QML is
AOT-compiled into the binary, so a successful build also validates QML), nftables
(Linux egress), pf (macOS egress), a Chromium native-messaging extension (browser
allowlist), TOTP for admin gating.

**Design pattern — modular monolith, MVVM-ish, single process, single window:**

```
                 ┌──────────────────────────────────────────────┐
   QML view  ───▶│  context properties (routineManager, …)       │  src/shell/*.qml
 (declarative)   └──────────────────────────────────────────────┘
                                  ▲  bound by
                          src/shell/ShellWindow.cpp  (one QQuickView)
                                  ▲  constructed + wired in
   composition  ─────────  src/main.cpp  ──── connects services by SIGNAL/SLOT,
   root                                       not by direct calls
                                  │ owns
        ┌─────────────────────────┴───────────────────────────┐
   QObject services (src/core/*)                     PlatformBackend (Strategy)
   RoutineManager, StatsStore, NotesStore,           src/platform/PlatformBackend.h
   TOTPEngine, MusicEngine, SystemStatus,            ├─ linux/LinuxBackend  (real target)
   InspirationStore, Updater, IdleMonitor,           └─ macos/MacBackend    (softer)
   MediaKeys, Timer
```

- **`main.cpp` is the only wiring hub.** Services are decoupled from each other and
  connected via Qt `signal/slot` there (e.g. `RoutineManager::routineSessionFinished`
  → `StatsStore::recordRoutineSession`). Prefer adding a connection here over making
  one service call another directly.
- **`PlatformBackend` is the single OS boundary** (compile-time Strategy: `Q_OS_LINUX`
  / `Q_OS_MACOS`). Core code must talk to the OS only through this interface.
- **QML is the view, the QObject services are the view-models.** QML reaches C++ only
  through `Q_PROPERTY` / `Q_INVOKABLE` on the exposed context properties.
- **One window, one process.** Extra `QQuickView`s are treated as unwanted "second
  processes"; a WM flag is unacceptable. (The progress overlay is the one tolerated
  exception.)

**Runtime state** lives under `~/.focusos/` (`routines.json`, `config.json`,
`stats.json`, `active.json` watchdog checkpoint, `totp.*`, `inspiration/`, `music/`,
`blocker/`). Treat writes as crash-sensitive — the Linux watchdog may relaunch the
shell mid-routine.

---

## 2. File Structure Map

```text
CMakeLists.txt          Build; links Qt6::DBus on Linux; AOT-compiles QML; totp_tests target.
AGENT.md                This file — orientation + AI rules.
CLAUDE.md               Deep code map + platform gotchas. Read before exploring.
README.md / INSTALL.md  User-facing overview / install levels.
docs/                   Subsystem detail: architecture-decisions, browser-blocker,
                        macos-backend, stability-audit. Put long-form design notes HERE,
                        not in new root-level *.md files.
assets/                 fonts/, music/ (fallback ambient), qml/theme.js (shared constants).
resources/
  Cold Turkey/          Pristine upstream extension snapshot (vendored — do not hand-edit).
  focusos-blocker/      FocusOS extension fork + native-host dev files (ctBackground.js etc).
scripts/                Blocker packaging, update/revert, policy, diagnostics (blocker-doctor.sh).
packaging/linux/        SDDM session, watchdog, sleep/logind .conf, updater, restore scripts.
tests/                  Qt Test (totp_tests is the only target today).
build/                  GENERATED CMake tree. Never read or edit. Not source of truth.

src/
  main.cpp              Composition root: backend select, service construction, signal
                        wiring, single-instance lock, crash-cleanup handlers, --native-host
                        and --write-policy CLI branches.
  core/                 QObject services exposed to QML (one context property each, lowercased
                        name). Domain + app logic ONLY. No OS #ifdefs that belong behind a
                        backend virtual; no QML/ShellWindow includes (never depend upward).
  platform/
    PlatformBackend.h   The OS boundary interface. Add a virtual HERE (with a safe no-op
                        default) before any core code calls new platform behavior.
    linux/              LinuxBackend (KWin, watchdog, freeze/thaw, media daemon, recovery)
                        + NetGate (nftables allowlist). THE shipping target.
    macos/              MacBackend (+ MacBackendNative.mm Obj-C shim). Softer shell.
  shell/
    ShellWindow.{h,cpp} Hosts the single QQuickView; sets the QML context properties.
    *.qml               UI. Main.qml = root; MissionView = active routine; InfoPanel =
                        stats/notes; UnlockModal = admin Settings (incl. ROUTINES editor);
                        ActivitiesPanel = launcher; others (StarField, AmbientLayer,
                        ProgressOverlay, IdleScreen, FileBrowser, NotesDrawer).
  blocker/              Browser native-messaging host + signed-binary policy (BlockerPolicy,
                        BlockerHost, BlockerSecret).
```

**Where new logic goes:** domain/state → a `core/` service (add `Q_PROPERTY`/`Q_INVOKABLE`
for QML). OS-specific behavior → a `PlatformBackend` virtual + per-backend impl. Pure
presentation → QML. A new long design note → `docs/`, never a new root `*.md`.

---

## 3. Vibe-Coding Remediation (issues to fix incrementally)

These are real structural debts found in the current tree. **Do not fix them as a big
bang** — chip away when you're already touching the area, and keep each fix scoped.

- **`RoutineManager` is a god object** (`RoutineManager.cpp` ~2330 LOC, `.h` ~405 LOC).
  It owns the routine model *and* engage/end/pause *and* the timer *and* lockdown
  orchestration *and* stats/notes handoff *and* crash recovery *and* music behavior
  *and* deep-sleep *and* screen lock *and* idle handling. **Progress:** the
  file-dialog UI concern is now extracted to `core/FilePicker` (the
  `pickApplication`/`pickFile`/`pickFolder` invokables are thin forwarders that just
  relay the picker's validation hint to the status line). **Still to do:** split the
  domain model (the `QAbstractListModel` of routines + JSON persistence) from the
  session-orchestration (engage/end/pause/timer/lockdown/recovery).

- ~~**`dataDirectory()` couples every store to the god header.**~~ *Done.* The
  `~/.focusos` path now lives in `core/AppPaths` (`AppPaths::dataDirectory()` /
  `filePath()`). `StatsStore`, `MusicEngine`, and `InspirationStore` no longer pull in
  `RoutineManager.h`; `Updater` and `NotesStore` dropped their own divergent copies of
  the path logic (those two had silently skipped the macOS sudo/console-home resolution).
  Use `AppPaths` for any new `~/.focusos` path — do not re-add a `dataDirectory()` to a
  service.

- **Shared enums live on the god object.** Types like the music-behavior and
  session-outcome enums sit in `RoutineManager.h`, dragging it across translation units.
  Move shared value types into their own small headers.

- **Oversized QML files mixing layout with logic.** `UnlockModal.qml` (~2935 LOC) packs
  every admin Settings tab — including routine-editing business logic — into one file;
  `InfoPanel.qml` (~1925) is stats + notes together; `ActivitiesPanel.qml` (~1233) and
  `Main.qml` (~1150) are large. Split per tab/panel into components, and push imperative
  logic down into C++ `Q_INVOKABLE`s — QML should stay declarative.

- **Monolithic platform backends.** `LinuxBackend.cpp` (~2008) and the macOS pair
  (`MacBackend.cpp` ~1726, `MacBackendNative.mm` ~2040) each do KWin scripting, the
  watchdog, process freeze/thaw, the media-keys daemon, session startup, and recovery in
  one class. Extract focused helpers (e.g. `Watchdog`, `ProcessFreezer`, `KwinControl`)
  *behind* the backend without widening the interface.

- **`PlatformBackend.h` is a fat interface** (40+ virtuals, many single-platform with
  no-op defaults). It is the correct boundary but is ballooning. Over time, group by
  capability (power/idle, kiosk/login, network) toward Interface Segregation — but never
  reintroduce OS `#ifdef`s in core to avoid it.

- ~~**Doc / root sprawl.**~~ *Done.* The long-form root notes (`lockdown.md`,
  `macos.md`, `questions.md`, `workflow_extensibility.md`) now live under `docs/`; the
  root keeps only README / INSTALL / AGENT / CLAUDE. Put any new long design note in
  `docs/`, never a new root `*.md`. *(An earlier pass also removed the stale `AGENTS.md`,
  a verbatim duplicate of `CLAUDE.md`.)* `docs/macos.md` (port/capability audit) and
  `docs/macos-backend.md` (backend code doc) still overlap in places — fold one into the
  other next time you're editing the macOS docs.

---

## 4. Token-Efficiency Protocols (rules for AI agents)

Follow these on every task in this repo. They cut wasted context and keep edits small.

**Reading**
- **Read [CLAUDE.md](CLAUDE.md) first** — it is the pre-built code map. Don't re-derive
  the layout by scanning files.
- **Never read `build/`** — it is generated and the QML is AOT-compiled; it tells you
  nothing the source doesn't.
- **For the big files, never do a full read.** `RoutineManager.cpp`, `UnlockModal.qml`,
  `LinuxBackend.cpp`, `MacBackendNative.mm`, `InfoPanel.qml`, `MacBackend.cpp`,
  `ActivitiesPanel.qml`, `Main.qml`, and `resources/.../ctBackground.js` are 1,100–2,900
  lines each. Use `grep`/ripgrep to find the symbol, then read with a tight
  `offset`/`limit`. Read the `.h` before the `.cpp`.

**Editing**
- **Scope each change to one subsystem.** Don't refactor unrelated code in the same pass;
  if you spot debt, note it against §3 instead of expanding the diff.

**Dependencies & boundaries**
- **OS behavior goes through `PlatformBackend`.** Add the virtual (with a no-op default)
  to `PlatformBackend.h` *before* core calls it — never sprinkle `#ifdef Q_OS_*` into
  `core/`.
- **QML ↔ C++ only via `Q_PROPERTY` / `Q_INVOKABLE`.** Don't push platform or business
  logic into QML.
- **One window, one process.** Don't introduce extra `QQuickView`s or a WM flag.

**Verifying**
- **Build incrementally:** `cmake --build build` (the tree is already configured; this
  also validates QML). Run `ctest --test-dir build` for `totp_tests`.
- **After touching the browser blocker**, read [docs/browser-blocker.md](docs/browser-blocker.md)
  and test native-host delivery — a clean C++ compile is *not* proof it works.
- **State your assumptions about Linux vs macOS.** A macOS build won't catch Linux
  backend errors and vice-versa; the shipping target is Linux.
