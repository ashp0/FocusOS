# FocusOS on macOS — Capability Audit & Port Notes

Experimental research notes, not a shipping plan. The goal: figure out how much
of the Linux/KDE FocusOS lockdown model (see [lockdown.md](lockdown.md)) can be
reproduced on macOS, what each level of hardening costs, and where the platform
draws a hard line.

A partial macOS backend already exists and is more complete than its
"deprioritized" status suggests — [`MacBackend.cpp`](src/platform/macos/MacBackend.cpp)
and [`MacBackendNative.mm`](src/platform/macos/MacBackendNative.mm) already wire up
Endpoint Security exec-blocking, a Network Extension content filter, kiosk
presentation options, `IOPMAssertion` display-sleep inhibition, and a LaunchAgent
watchdog. This doc explains what those mechanisms can and can't enforce.

---

## TL;DR verdict

- **You cannot replace the macOS shell.** There is exactly one `WindowServer` and
  it's Apple's. Unlike Linux — where FocusOS *is* the session shell running on top
  of KWin and a swappable login manager — macOS has no swappable session, no
  alternate compositor, and no "your binary instead of the desktop" path. The Linux
  caveat "FocusOS is not a compositor-level security boundary" is *more* true on
  macOS, not less.
- **Out of the box (signed app, SIP on)** you get a *cooperative* kiosk: a
  fullscreen app that hides the Dock/menu bar and disables Cmd-Tab / Force Quit
  *while it is frontmost*. Plus app-quitting and a firewall. This is genuinely
  useful and is roughly the "medium" Linux posture.
- **The real enforcement teeth — block app launches, block network — need
  Apple-granted entitlements** (Endpoint Security, Network Extension), *not* SIP off.
  Those entitlements + user TCC approval are the sanctioned path.
- **Disabling SIP** mainly buys you two things FocusOS would actually use: (1) the
  ability to run Endpoint Security / unsigned privileged code *without* Apple's
  managed entitlement (via an AMFI boot-arg), and (2) the ability to permanently
  disable the Aqua shell agents (Dock/Finder/Spotlight) so they stop respawning.
  It does **not** give you the desktop as a replaceable surface.
- **Some things are impossible from any software tier:** the power button, Recovery
  mode, the boot picker, NVRAM reset, and target-disk/DFU. Those are firmware, and
  only MDM supervision (recovery lock / activation lock) gates them — not an app.

---

## 1. Why macOS is structurally different from Linux

The Linux FocusOS model rests on three things that **do not exist on macOS**:

| Linux assumption | macOS reality |
| --- | --- |
| The session is a process you swap out (SDDM picks `focusos.desktop`, KWin runs FocusOS as its `--exit-with-session` payload). | The GUI session is `loginwindow` → `WindowServer` + a fixed set of launchd-managed "Aqua" agents. You cannot substitute your binary for it. |
| The compositor (KWin) is separable from the shell and you can harden around it. | `WindowServer` *is* the compositor, owned by `_windowserver`, unkillable and unreplaceable. All window/key policy ultimately lives here. |
| You can stash/restore other login sessions and mask gettys/VTs to remove escape hatches. | There are no VTs, no getty, no alternate session entries. The "escape hatches" are different: Spotlight, Mission Control, Cmd-Tab, the menu bar, Recovery boot. |

The macOS "shell" you're fighting is this set of launchd agents (all under
`/System/Library/LaunchAgents`, all SIP-protected):

- **`Dock`** — the Dock *and* Mission Control / Spaces / Launchpad host.
- **`Finder`** — the desktop (wallpaper + icons) and file browser.
- **`ControlCenter`** / **`SystemUIServer`** — menu-bar status items, clock, the
  Control Center pulldown.
- **`NotificationCenter`** (`usernoted`) — notifications + the pulldown.
- **`Spotlight`** — the Cmd-Space launcher (backed by `mds`/`mdworker`).
- **`TextInputMenuAgent`, `talagent` (Stage Manager / window mgmt), etc.**

None of these can be *replaced*. They can be *hidden* (presentation options),
*killed* (they respawn), or — only with SIP off — *disabled at the launchd level*
so they stop respawning.

---

## 2. SIP and the Apple Silicon boot model — what "disable SIP" actually buys

**System Integrity Protection** protects: `/System`, `/usr` (except `/usr/local`),
`/bin`, `/sbin`; files flagged `restricted`; Apple-process memory
(`task_for_pid`/debugging is blocked for protected processes); unsigned kext
loading; protected NVRAM variables; and `dtrace` on system processes.

**What disabling SIP gives FocusOS that it would actually use:**

1. **Run privileged/unsigned security code without Apple's managed entitlement.**
   Endpoint Security normally requires `com.apple.developer.endpoint-security.client`
   (a *managed* entitlement Apple grants to enrolled orgs). For research, booting
   with `amfi_get_out_of_my_way=1` (set in `recoveryOS` via `nvram boot-args`,
   which itself needs SIP off) lets a self-signed binary carry restricted
   entitlements. This is the only way to test the ES exec-blocker on a personal
   machine without going through Apple's entitlement-request process.
2. **Permanently disable the Aqua shell agents.** `launchctl disable
   gui/$(id -u)/com.apple.Dock.agent` (and `…Finder`, `…Spotlight`,
   `…controlcenter`, `…notificationcenterui`) writes to launchd's override state in
   `/var/db/com.apple.xpc.launchd/`, which is **SIP-protected**. With SIP on you get
   `Operation not permitted`. With SIP off it sticks, and `killall Dock` no longer
   respawns. This is the closest macOS gets to "the desktop shell is simply not
   running."

**What disabling SIP does *not* buy you (the important caveats):**

- **Apple Silicon has a second lock: the Signed System Volume (SSV) + `authenticated-root`.**
  `/System` lives on a sealed, cryptographically verified snapshot. Even with SIP
  off you cannot modify `/System` until you *also* `csrutil authenticated-root
  disable`, remount the system volume read-write, edit, and re-bless a new
  snapshot. This is invasive and makes the system fragile. FocusOS should never
  need to touch `/System` — note this only to dispel the idea that "SIP off = own
  the OS."
- **Kexts are a dead end.** Apple deprecated KPIs; on Apple Silicon a kext needs
  Reduced Security + user-approved kernel extensions and still can't do what you'd
  want. The supported kernel-adjacent surface is **System Extensions** (Endpoint
  Security, Network Extension, DriverKit) — userspace, entitlement-gated, **no SIP
  off required**. FocusOS already targets exactly these. *There is no reason for
  FocusOS to ship a kext.*
- **You still don't get the compositor.** SIP off does not let you inject into or
  replace `WindowServer`.

---

## 3. System-UI suppression, component by component

"OOTB" = signed app, SIP on, normal user (plus TCC approvals like Accessibility /
Input Monitoring where noted). "SIP off" = additionally what the launchd-disable
path unlocks. "MDM" = what supervised-device management adds.

| Component | OOTB (cooperative) | SIP off (persistent) | MDM | Hard limit |
| --- | --- | --- | --- | --- |
| **Dock** | `NSApplicationPresentationHideDock` hides it *while FocusOS is frontmost*; `defaults write com.apple.dock autohide` globally. Killing it → respawns. | `launchctl disable …Dock.agent` → gone for the session, no respawn. | Restrictions payload can lock Dock contents/position. | — |
| **Menu bar** | `NSApplicationPresentationHideMenuBar` (frontmost only); `_HIHideMenuBar` = global auto-hide (reveals on hover). | No clean persistent removal — the bar is drawn by WindowServer + frontmost app. | Some items hideable via restrictions. | The system menu bar cannot be *removed*; only hidden/auto-hidden. |
| **Spotlight (Cmd-Space)** | Disable the hotkey via `com.apple.symbolichotkeys` IDs **64/65**; `mdutil -i off` stops indexing. | Disable `…Spotlight` agent so the UI process won't launch at all. | Disable Spotlight suggestions; can't fully remove the launcher. | — |
| **Notification Center** | A **Focus / DND** mode suppresses banners; no clean public API to force-enable a Focus (the old `com.apple.notificationcenterui doNotDisturb` default broke after Monterey — it's now a `~/Library/DoNotDisturb` Focus DB). | Disable `…notificationcenterui` agent → no NC pulldown / banners. | Focus + notification restrictions are first-class. | — |
| **App switcher (Cmd-Tab)** | `NSApplicationPresentationDisableProcessSwitching` blocks it *while frontmost*. A `CGEventTap` can also swallow ⌘⇥. | — | — | A tap needs Accessibility/Input-Monitoring TCC; some combos reach WindowServer first. |
| **Mission Control / Spaces / Exposé** | Disable hotkeys (`symbolichotkeys` IDs 32/33/36, etc.); disable trackpad swipe gestures. A fullscreen app lives in its own Space — multi-touch can still navigate away. | — | Restrictions can disable Mission Control / hot corners. | Trackpad space-swipe is partly handled below the app. |
| **Force Quit (⌘⌥⎋)** | `NSApplicationPresentationDisableForceQuit` (frontmost only). | — | — | Only holds while your app owns presentation options. |
| **Fast user switch / log out (⌘⇧Q)** | Disable via symbolichotkeys / restrictions. | — | Login-window + switching restrictions. | — |
| **Power button / shutdown dialog** | **Not blockable** from userspace — handled by `loginwindow`/WindowServer. | Not blockable. | Not blockable (MDM can't stop the hardware button). | Firmware/OS owned. |
| **Recovery, boot picker, NVRAM reset, DFU, target disk** | **Impossible.** | **Impossible.** | Recovery lock (Apple Silicon) / firmware password (Intel) via MDM gates these. | Firmware. A user with the machine can always recover it — same "operational recovery, not a bypass" stance as Linux. |

Key takeaway: **every OOTB suppression is "while frontmost" or "until a system
service respawns."** It is cooperative, not enforced. SIP-off persistence + a
watchdog turns cooperative into sticky, but never into WindowServer-level.

---

## 4. Enforcement primitives (the actual FocusOS mechanisms)

How each Linux mechanism maps to a macOS API, and what it costs.

### App blocking — Endpoint Security `AUTH_EXEC`
*Linux equivalent: the ~1.5 s pkill watchdog sweep.* macOS does this **better**: the
ES client (already in [`MacBackendNative.mm`](src/platform/macos/MacBackendNative.mm))
subscribes to `ES_EVENT_TYPE_AUTH_EXEC` and **denies the exec before the process
starts** — a real allow/deny gate, not a kill-after-launch race. Cost: runs as
root, needs `com.apple.developer.endpoint-security.client` (Apple-granted) **or**
SIP-off + `amfi_get_out_of_my_way=1` for research, plus Full Disk Access (TCC).
This is the single most important macOS primitive and it does **not** require SIP
off in production.

### App quitting — `NSWorkspace` terminate
*Linux equivalent: `quitBackgroundApps` SIGTERM sweep.* `[NSRunningApplication
terminate]` → `forceTerminate` (already implemented). Works for user GUI apps;
cannot kill SIP-protected system processes (nor should it). The dry-run
(`previewBackgroundAppQuit`) and always-allowed exemptions port cleanly.

### Network — `pf` (implemented) **or** Network Extension content filter
*Linux equivalent: `NetGate.cpp` nftables allowlist.* Two options:
- **`pf` (`pfctl`) — now the implemented macOS path** (`MacBackendNative.mm`
  `applyNetworkFilter`/`dropNetworkFilter`): root-only, no entitlement, no SIP off —
  the closest analog to nftables. FocusOS resolves the allowlist to IPs (reusing
  NetGate's companion-domain expansion for YouTube/Google), writes a ruleset to
  `/etc/pf.anchors/focusos` that blocks all egress except loopback + DNS + the
  resolved addresses, and loads it with `pfctl -E -f`. `dropNetworkPolicy` restores
  `/etc/pf.conf` and disables pf. Best path for a research build (no $99 account).
- **`NEFilterManager` content filter** (the sanctioned alternative, not currently
  wired up): per-socket allow/deny, survives as a System Extension, user approves it
  once — but needs the Network Extension entitlement + a bundled `…NetworkFilter`
  provider, which is impractical without a Developer account. Swap back to this for
  a distributable build.

### Fullscreen / kiosk shell — `NSApplicationPresentationOptions`
*Linux equivalent: being the session shell + the lock overlay.* Already implemented
in `enterKioskPresentation`: Hide Dock + Hide Menu Bar + Disable Apple Menu +
Disable Process Switching + Disable Force Quit + Disable Session Termination +
Disable Hide. This is the kiosk backbone — but, again, **frontmost-only**. The ES
blocker is what keeps anything else from *becoming* frontmost.

### Process freeze — SIGSTOP / SIGCONT
*Linux equivalent: `freezeBackgroundProcesses` for deep-idle.* Same POSIX signals by
PID. macOS already does opportunistic App Nap, but explicit SIGSTOP works for the
deep-idle "soft sleep." Maps 1:1.

### Watchdog / respawn — LaunchAgent `KeepAlive`
*Linux equivalent: `focusos-watchdog.sh --kiosk`.* macOS is **cleaner** here: the
implemented `startWatchdog` writes a `com.focusos.watchdog` LaunchAgent with
`KeepAlive → PathState` watching `~/.focusos/active.json`. launchd respawns FocusOS
whenever the checkpoint exists — no shell loop needed. The Linux crash-loop
self-heal (degrade to Plasma) has no macOS analog because there's nothing to fall
back *to*; the equivalent safety valve is simply removing the checkpoint file.

### Display / power — `IOPMAssertion`, `pmset`, `caffeinate`
*Linux equivalent: DPMS + KWin idle + s2idle pin.* Already implemented:
`IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep)` with a
`caffeinate -d -w <pid>` fallback. `pmset displaysleepnow` = sleep display,
`pmset sleepnow` = suspend. macOS sleep/resume is robust, so the Linux "S3
black-screens the iMac, pin to s2idle" workaround is unnecessary — `suspendSystem`
can just call `pmset sleepnow`.

### Media keys
*Linux equivalent: KGlobalAccel + the sleep/resume re-grab dance.* On macOS, volume
/ brightness keys are handled by the system; you don't re-implement them. If FocusOS
wants to *intercept* them it'd use a `CGEventTap` (Input Monitoring TCC) or the
`MPRemoteCommandCenter`. The whole KGlobalAccel re-grab gotcha simply doesn't apply.

---

## 5. The four lockdown tiers

### Tier A — Signed app, SIP on (out of the box)
Developer-ID signed + notarized; user grants Accessibility / Input Monitoring / Full
Disk Access via TCC. You get: fullscreen kiosk presentation, hide Dock/menu bar,
disable Cmd-Tab/Force-Quit *while frontmost*, quit other user apps, a `pf` firewall,
display-sleep control, and a LaunchAgent watchdog. Spotlight / Mission Control /
Launchpad / screenshot / switcher hotkeys are swallowed by a `CGEventTap`
(`startInputBlocker`, needs Accessibility) rather than rewriting the user's
`symbolichotkeys` plist — reversible, nothing left behind. **This is a genuinely
usable focus kiosk and the realistic target for a personal/research build.** It is
cooperative: a determined user can still navigate via a trackpad space-swipe (gestures
ride below the event tap) and the shell agents are alive underneath.

### Tier B — Tier A + Apple-granted entitlements (still SIP on)
Add `com.apple.developer.endpoint-security.client` and the Network Extension
content-filter entitlement. Now: **app launches are denied at exec time** (not
killed after) and network is filtered per-socket as a System Extension. This is the
*sanctioned* strong posture and the one Apple intends for this use case. Cost is
bureaucratic (entitlement requests, notarization, system-extension approval), not
technical.

### Tier C — Tier A + SIP disabled (research only)
Adds: run the ES blocker without the managed entitlement (`amfi_get_out_of_my_way=1`),
and **persistently disable the Aqua shell agents** (`launchctl disable` Dock/Finder/
Spotlight/ControlCenter/NotificationCenter) so the desktop genuinely isn't running
underneath your kiosk. This is the closest macOS comes to the Linux "replace the
shell" feel. Downsides: SIP off weakens the whole machine, breaks some DRM/Apple
services, is obvious to any IT/MDM, and on Apple Silicon you've had to fiddle with
`nvram boot-args`. **Good for a personal experiment, unacceptable for distribution.**

### Tier D — MDM supervision (the enterprise kiosk path)
Enroll the Mac in MDM and supervise it (Apple Configurator / ABM). Now you have
*declarative*, SIP-on restrictions: lock the Dock, disable Spotlight suggestions,
restrict apps via `com.apple.applicationaccess`, configure the login window, hide
fast-user-switching, and — critically — set a **recovery lock** so Recovery/boot
picker is gated. This is how real macOS kiosks (retail, exam machines) are built.
It's policy, not a custom shell: macOS still has **no equivalent to iOS Guided
Access / Autonomous Single App Mode** — there is no WindowServer-enforced "one app
and nothing else" lock on the Mac. The nearest OS-native limiter for a single user
is **Screen Time** app/category limits (admin-password gated, bypassable, but real).

### Impossible at every tier
- Replace or inject into `WindowServer`; become the compositor.
- Block the physical power button / forced shutdown.
- Prevent Recovery, the boot picker, NVRAM reset, DFU, or booting external media —
  except by firmware password (Intel) / recovery lock (Apple Silicon via MDM).
- A truly inescapable lock with no admin/physical recovery. (Same honest boundary as
  Linux: "a user with physical/boot-media access can still recover the machine —
  that's operational recovery, not a bypass.")

---

## 6. Linux ↔ macOS feature parity (by `PlatformBackend.h` method)

✅ clean port · 🟡 works with caveats / weaker · 🔴 conceptual mismatch or blocked.

| `PlatformBackend` method | Linux | macOS | Notes |
| --- | --- | --- | --- |
| `launchApps` / `openUrls` | ✅ | ✅ | `NSWorkspace` + kiosk `--app` browser. Implemented. |
| `terminateApps` / `quitBackgroundApps` / `previewBackgroundAppQuit` | ✅ | ✅ | **Implemented:** `sweepOtherApplications` quits every regular (Dock-visible) app outside the keep-set (routine + always-allowed + Finder + FocusOS), with the same dry-run preview as Linux. Can't touch protected system procs. |
| `prepareRoutineSession` (lockdown sweep) | ✅ pkill loop | ✅ ES `AUTH_EXEC` + key tap | macOS *pre-empts* launches (ES, needs entitlement/SIP-off) **and** swallows the launcher hotkeys via a `CGEventTap` (`startInputBlocker`) so the user can't reach Spotlight/Mission Control in the first place. |
| `endRoutineLockdown` | ✅ | ✅ | **Implemented:** stop the ES client + the key tap and leave kiosk presentation, without killing the routine's apps. |
| `applyNetworkPolicy(Async)` / `dropNetworkPolicy` | ✅ nftables | ✅ | **Implemented via `pf`** (root, no entitlement); `applyNetworkPolicyAsync` now resolves DNS off the GUI thread on a worker (mirrors Linux). NEFilter is the distributable alternative. |
| `lockScreen` / `unlockScreen` | ✅ | ✅ | **Implemented:** `pmset displaysleepnow` (locks if "require password after sleep" is set) + the QML overlay; unlock wakes via `caffeinate -u`. |
| `sleepDisplay` / `wakeDisplay` | ✅ DPMS | ✅ | `pmset displaysleepnow`; wakes on input / `caffeinate -u`. Implemented. |
| `suspendSystem` | 🟡 s2idle pin | ✅ | **Implemented:** `pmset sleepnow` — macOS sleep is reliable; the s2idle workaround is unneeded. |
| `freezeBackgroundProcesses` / `thaw` | ✅ | ✅ | **Implemented:** SIGSTOP/SIGCONT regular apps outside the keep-set by PID. |
| `setDisplaySleepInhibited` / `releaseDisplaySleepInhibitors` | ✅ | ✅ | `IOPMAssertion` + `caffeinate -w <pid>`. Implemented and clean. |
| `startWatchdog` | ✅ shell watchdog | ✅ | LaunchAgent `KeepAlive`/`PathState`. Implemented; arguably cleaner than Linux. |
| `setAlwaysAllowedApps` | ✅ | ✅ | Exemption set feeds ES allowlist. Implemented. |
| `launchDesktopShell` / `terminateDesktopShell` / `desktopShellSupported` | ✅ | 🔴 | No swappable shell. Implemented as "leave kiosk + open Terminal" — a different idea. |
| `restoreShellPlacement` | ✅ (no-op) | ✅ | Re-raise the always-on baseline (kiosk presentation + key tap) so the home screen stays locked between routines. |
| `restoreLoginSessions` | ✅ (stashed sessions) | 🔴 | macOS has no stashed sessions; returns unsupported. |
| `ensureGlobalShortcutsDaemon` | ✅ KGlobalAccel | 🔴 | KDE-only concept; N/A. |
| `runSessionStartupItems` | ✅ startup.sh | ✅ (repurposed) | No autostart replay needed (launchd handles login items); used as the post-window hook to raise the always-on kiosk + key-tap posture so the home screen is locked "in general", not only mid-routine. |
| `signOut` / `signOutSupported` | ✅ loginctl | ✅ | **Implemented:** drop pf + lockdown, bootout the watchdog LaunchAgent, then log out via the `aevtrlgo` Apple event (no confirm dialog). |
| Media keys (`MediaKeys.*`) | ✅ KGlobalAccel + resume re-grab | 🟡 | System handles them; intercept via `CGEventTap`/`MPRemoteCommandCenter`. No re-grab gotcha. |
| **Replace the session shell / login surface** | ✅ SDDM + KWin payload | 🔴 | **The defining gap.** No equivalent — see §1, §5. |

Net: ~70% of the backend ports cleanly and a few pieces (exec-gating, LaunchAgent
watchdog, IOPM assertions, robust sleep) are actually *nicer* on macOS. The
irreducible loss is the shell-replacement / login-surface story, which Linux owns
via the display manager and which macOS structurally denies.

---

## 7. Recommended approach

For a **personal / research locked-down macOS** that mirrors FocusOS as closely as
the platform allows, build on the existing backend with this posture:

1. **Base = Tier A**, always. Fullscreen `NSApplicationPresentationOptions` kiosk +
   a `CGEventTap` (`startInputBlocker`) swallowing Spotlight / Mission Control /
   Launchpad / screenshot / Force-Quit hotkeys + the LaunchAgent watchdog +
   `IOPMAssertion`. The tap needs Accessibility (prompted on first engage) but no
   other privilege; this alone is a solid focus kiosk. The kiosk + tap are raised at
   startup (`runSessionStartupItems`), so the home screen is locked too, not only
   mid-routine.
2. **App enforcement = Endpoint Security**, the keystone. For distribution, get the
   ES entitlement (Tier B). For a personal machine, SIP-off + `amfi_get_out_of_my_way=1`
   (Tier C) to run the existing ES blocker self-signed. *Don't* fall back to a
   pkill-loop on macOS — the AUTH_EXEC gate is the whole point.
3. **Network = `pf`** (now the implemented macOS path) — needs only root, matches the
   nftables model, no entitlement. Switch to a `NEFilter` System Extension only if you
   later want a distributable build. `applyNetworkPolicyAsync` resolves DNS + loads pf
   on a detached worker thread and hops back to the GUI thread for the callback, so
   engage no longer freezes the shell (matches the Linux contract).
4. **Persistence = the LaunchAgent watchdog** keyed on `~/.focusos/active.json`, plus
   (Tier C only, optional) `launchctl disable` of the Dock/Finder/Spotlight agents so
   the desktop isn't lurking behind the kiosk. Keep the checkpoint file as the kill
   switch — deleting it is the macOS analog of `touch ~/.focusos/boot-to-plasma`.
5. **Accept the recovery boundary explicitly.** Document that power-button,
   Recovery, and external boot are out of scope unless the Mac is MDM-supervised with
   a recovery lock. This is the same honest stance as the Linux `lockdown.md`
   "Remaining Limits" section.
6. **If this ever needs to be genuinely tamper-resistant for someone other than the
   owner, go Tier D (MDM supervision).** That is the only path that gates firmware
   recovery and gives policy-level (not cooperative) restrictions — at the price of
   enrolling the machine in management. There is no software-only way to get there.

**One-line summary:** macOS lets FocusOS build a strong *cooperative* kiosk out of
the box and a strong *enforced* one with Apple's Endpoint-Security / Network-Extension
entitlements — but it will never let an app *be* the shell. Disabling SIP only
loosens entitlement checking and lets you switch off Apple's shell agents; it does
not hand over the desktop. For anything beyond a personal experiment, the platform's
intended answer is MDM supervision, not a custom session.

---

## 8. Build / wiring notes

- The macOS backend builds today; CMake links AppKit/Foundation/IOKit and gates
  Endpoint Security behind `FOCUSOS_HAS_ENDPOINT_SECURITY` (see the
  `FOCUSOS_ENDPOINT_SECURITY_AVAILABLE` guard in
  [`MacBackendNative.mm`](src/platform/macos/MacBackendNative.mm)). NetworkExtension
  is no longer linked — the network lock is `pf`.
- **Implemented Tier-C research path** (no Developer account): build + ad-hoc-sign
  with [`packaging/macos/build-and-sign.sh`](packaging/macos/build-and-sign.sh),
  which embeds [`packaging/macos/focusos.entitlements`](packaging/macos/focusos.entitlements)
  (`com.apple.developer.endpoint-security.client`). Disable SIP (and, if needed,
  `amfi_get_out_of_my_way=1`) per [`packaging/macos/README.md`](packaging/macos/README.md),
  then run as root. For a distributable Tier-B build you'd instead use Developer-ID +
  notarization with the Apple-granted ES (and, if you re-add NEFilter, Network-Extension)
  entitlements.
- TCC approvals the app will prompt for: **Accessibility** and/or **Input Monitoring**
  (event taps / hotkey suppression), **Full Disk Access** (Endpoint Security),
  **Screen Recording** is *not* needed. These can't be granted programmatically —
  the user approves them in System Settings → Privacy & Security.
- The `pf` route needs a small root helper (or running FocusOS elevated) to
  `pfctl -f` an anchor; keep the rules in an anchor file you load/flush, mirroring
  `NetGate`'s apply/drop lifecycle.
