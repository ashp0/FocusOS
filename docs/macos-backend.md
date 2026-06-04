# macOS Backend — Build, Frameworks, and Entitlements

The macOS backend enforces a routine lockdown using native Apple APIs. Apple
framework calls are isolated in an Objective-C++ shim (`MacBackendNative.mm`,
exposed through `MacBackendNative.h`) so that `MacBackend.cpp` stays plain
Qt/C++ orchestration. This document records what the shim needs to actually run
on a hardened, signed build.

> macOS is the deprioritized target (Linux/KDE is the product). This file exists
> so the macOS path stays buildable and so that anyone who *does* sign and ship
> it knows which capabilities are mandatory and which gaps remain.

## Architecture

| Capability | API used | Shim entry point |
| --- | --- | --- |
| Home-screen kiosk / Dock / menu-bar hiding | `NSApplicationPresentationOptions` | `enterKioskPresentation` / `leaveKioskPresentation` |
| Active-routine window (own Space) | `NSWindow toggleFullScreen:` + `FullScreenPrimary` | `enterNativeFullScreen` / `exitNativeFullScreen` / `windowIsNativeFullScreen` |
| Countdown overlay across Spaces | `NSWindowCollectionBehaviorCanJoinAllSpaces` | `setWindowJoinsAllSpaces` |
| Routine Dock neuter (tiny + empty) | `com.apple.dock` defaults snapshot/restore | `neuterDock` / `setSystemDockHidden` |
| Shortcut blocking (home = full, routine = navigation-aware) | `CGEventTap` | `startInputBlocker(allowNavigation)` / `stopInputBlocker` |
| Process blocking | Endpoint Security `ES_EVENT_TYPE_AUTH_EXEC` | `startExecBlocker` / `stopExecBlocker` |
| Launch enforcement (userland) | `NSWorkspaceDidLaunchApplicationNotification` | `startLaunchWatcher` / `stopLaunchWatcher` |
| Display-sleep inhibition | IOKit `IOPMAssertionCreateWithName` | `createDisplaySleepAssertion` / `releaseDisplaySleepAssertion` |
| Network policy | `pf` (`pfctl`) outbound allowlist | `applyNetworkFilter` / `dropNetworkFilter` |
| Aqua shell lockdown (legacy / cleanup only) | SIP-off launchd overrides for Dock/Finder/Spotlight/etc. | `applyAquaUiLockdown` / `restoreAquaUiLockdown` |
| App launch / terminate / metadata | AppKit `NSWorkspace`, `NSRunningApplication`, `NSBundle` | `launchApplicationBundle`, `terminateApplications`, etc. |

## Linked frameworks

`CMakeLists.txt` enables the `OBJCXX` language on Apple and links:

- **AppKit** — presentation options, `NSWorkspace`, `NSRunningApplication`
- **Foundation** — `NSString`/`NSArray` bridging, `NSRunLoop` pumping
- **IOKit** — `IOPMAssertionCreateWithName` display-sleep assertions
- **EndpointSecurity** — `AUTH_EXEC` subscription (linked only when the SDK ships
  the framework; gated by the `FOCUSOS_HAS_ENDPOINT_SECURITY` compile define).
  When absent, `startExecBlocker` returns a clear error and the rest of the
  lockdown still functions.

A plain `cmake --build` produces a working binary. The Endpoint Security and
Network Extension code paths compile and link, but only *enforce* once the build
is signed with the entitlements below — unsigned, `es_new_client` fails with a
permission error and `NEFilterManager` save is rejected.

## Required entitlements

Sign `focusos.app` with a provisioning profile carrying:

| Entitlement | Why |
| --- | --- |
| `com.apple.developer.endpoint-security.client` | Create the `es_client_t` for `AUTH_EXEC` process blocking. Requires the restricted Endpoint Security entitlement from Apple. |
| `com.apple.developer.networking.networkextension` = `content-filter-provider` | Install and enable the `NEFilterManager` socket content filter. |
| Hardened Runtime (`com.apple.security.cs.*` as needed) | Required for notarization and for the Endpoint Security client to load. |

Notes:

- The Endpoint Security client must run with **root** privileges in addition to
  the entitlement. `es_new_client` returns `ES_NEW_CLIENT_RESULT_ERR_NOT_PERMITTED`
  otherwise. The watchdog `LaunchAgent` runs in the user GUI domain, so a
  production lockdown needs a privileged helper (or a `LaunchDaemon`) for the
  exec blocker — the user-session binary alone cannot subscribe.
- The Endpoint Security entitlement is **restricted**: it requires a dedicated
  provisioning profile granted by Apple and cannot be used with an ad-hoc
  signature. For local development, AUTH_EXEC blocking is disabled
  (`SIP`-protected) unless you disable SIP — not recommended.

## Network and Aqua lockdown

The macOS network lock is `pf`, not a Network Extension target. FocusOS resolves
the routine allowlist, writes `/etc/pf.anchors/focusos`, loads it with `pfctl`,
and clears it on routine end/crash cleanup. This needs root but not an Apple
Network Extension entitlement.

## Active-routine window layout

During a routine FocusOS becomes a **native-fullscreen window in its own Space**
(`enterNativeFullScreen`), so the routine's app windows stay on the desktop Space
and remain reachable — the user swipes / Mission Controls between FocusOS and
their allowed apps, the way the old left-most fullscreen Space worked. Three
pieces make this a focus surface rather than an escape hatch:

- **Neutered Dock** (`neuterDock`): the Dock is shrunk to its minimum tile size
  and stripped of every pinned app/recent, so a Mission Control swipe-up never
  reveals a useful launch surface. The user's real Dock is snapshotted to
  `~/.focusos/dock-pre-focusos.plist` and fully restored on routine end / admin
  unlock (`setSystemDockHidden(false)`).
- **Navigation-aware key blocker** (`startInputBlocker(allowNavigation=true)`):
  Spotlight, Launchpad, screenshots and force-quit stay blocked, but Mission
  Control / Spaces / ⌘-Tab are *allowed* so the user can move between FocusOS and
  the routine apps. The home screen uses `allowNavigation=false` (everything
  blocked — no route off the locked shell).
- **Launch watcher** (`startLaunchWatcher`): any disallowed Dock-visible app the
  user still manages to launch is terminated the instant it appears. This is the
  real enforcement; it does not yank focus or re-hide surfaces, so it never fights
  the user's Space navigation.

The countdown progress overlay is pinned onto every Space
(`setWindowJoinsAllSpaces`) so its border keeps painting over the routine apps
even while FocusOS sits in its own fullscreen Space.

When the routine ends (or admin "Access Desktop" is requested) FocusOS exits the
fullscreen Space and reclaims the frameless home cover / steps down into an
ordinary windowed window.

## Aqua shell lockdown (legacy / cleanup)

A stronger, SIP-off/research-only lockdown that disables the user's
launchd-managed Aqua agents (`com.apple.Dock.agent`, Finder, Spotlight,
SystemUIServer, Control Center, Notification Center, Siri, and `talagent`) still
exists in the shim. It is **no longer applied during routines** — disabling the
Dock/Mission Control agents is incompatible with the native-fullscreen routine
layout above (the user could not navigate back to FocusOS), and `disable` + `bootout`
turned out not to be cleanly reversible mid-session.

The critical detail behind the old "Access Desktop doesn't bring the Dock back"
failure: a service that was `launchctl bootout`ed is **dead**, and `enable` +
`kickstart` cannot revive it — `kickstart` errors with "Could not find service".
Only a fresh `launchctl bootstrap <gui-domain> <plist>` reloads it into the domain.
A dead Dock also takes down ⌘-Tab (the app switcher) and Mission Control, which is
why all three vanished together.

`restoreAquaUiLockdown` now heals this properly. It is called on routine end /
admin unlock / startup, and for every known UI agent it: clears any `disable`
flag; checks whether the agent is actually **loaded** (`launchctl print`); and only
when it is not, `bootstrap`s its `/System/Library/LaunchAgents/*.plist` back and
`kickstart`s it. Healthy agents are left untouched (no Dock/Finder restart churn on
a normal launch), and a machine left broken by an older build self-heals on the
next FocusOS launch. The standalone recovery script
`scripts/focusos-restore-macos-ui.sh` does the same and can be run by hand.

## TCC / development caveat

A binary launched from under `~/Desktop` (or other TCC-protected locations) can
be denied automation/accessibility access. Build and run from a non-protected
path, or grant Full Disk / Automation access in System Settings → Privacy when
testing the lockdown locally.
