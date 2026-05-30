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
| Kiosk / Dock / menu-bar hiding | `NSApplicationPresentationOptions` | `enterKioskPresentation` / `leaveKioskPresentation` |
| Process blocking | Endpoint Security `ES_EVENT_TYPE_AUTH_EXEC` | `startExecBlocker` / `stopExecBlocker` |
| Display-sleep inhibition | IOKit `IOPMAssertionCreateWithName` | `createDisplaySleepAssertion` / `releaseDisplaySleepAssertion` |
| Network policy | NetworkExtension `NEFilterManager` content filter | `applyNetworkFilter` / `dropNetworkFilter` |
| App launch / terminate / metadata | AppKit `NSWorkspace`, `NSRunningApplication`, `NSBundle` | `launchApplicationBundle`, `terminateApplications`, etc. |

## Linked frameworks

`CMakeLists.txt` enables the `OBJCXX` language on Apple and links:

- **AppKit** — presentation options, `NSWorkspace`, `NSRunningApplication`
- **Foundation** — `NSString`/`NSArray` bridging, `NSRunLoop` pumping
- **IOKit** — `IOPMAssertionCreateWithName` display-sleep assertions
- **NetworkExtension** — `NEFilterManager` content-filter configuration
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

## Network filter system extension (open gap)

`applyNetworkFilter` configures `NEFilterManager` to point at a content-filter
provider with bundle identifier `<main-bundle-id>.NetworkFilter`
(i.e. `com.focusos.shell.NetworkFilter`). **That provider target does not exist
in this repository yet.** To make the network lock actually filter traffic you
must add a Network Extension app/system extension target that:

1. Subclasses `NEFilterDataProvider`.
2. Reads the signed policy at `~/.focusos/blocker/policy.dat`
   (`BlockerPolicy::policyFilePath()`, passed through `vendorConfiguration`
   as `PolicyPath`) and the `AllowedHosts` / `DefaultAction` keys.
3. Is embedded in `focusos.app/Contents/Library/SystemExtensions` (system
   extension) or `PlugIns` (app extension) and signed with the matching
   Network Extension entitlement.

Until that target ships, `applyNetworkFilter` will load/save preferences but the
OS has no provider to run, so enforcement on macOS rests entirely on the
**browser blocker extension** (see [browser-blocker.md](browser-blocker.md)),
which is the documented macOS enforcement path.

## TCC / development caveat

A binary launched from under `~/Desktop` (or other TCC-protected locations) can
be denied automation/accessibility access. Build and run from a non-protected
path, or grant Full Disk / Automation access in System Settings → Privacy when
testing the lockdown locally.
