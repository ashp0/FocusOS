#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <QtGlobal>

namespace MacBackendNative {

struct NativeLaunchResult
{
    bool launched = false;
    qint64 pid = 0;
    QString executablePath;
    QString bundleIdentifier;
    QString displayName;
    QString errorMessage;
};

QString applicationExecutablePath(const QString &bundlePath);
QString bundleIdentifierForApplication(const QString &bundlePath);
QString bundleIdentifierForExecutable(const QString &executablePath);
QString displayNameForApplication(const QString &bundlePath);

NativeLaunchResult launchApplicationBundle(const QString &bundlePath,
                                           const QStringList &arguments = {});
void terminateApplications(const QStringList &bundleIdentifiers,
                           const QStringList &displayNames,
                           const QStringList &executablePaths);

bool enterKioskPresentation(QString *errorMessage = nullptr);
void leaveKioskPresentation();

// Hide the Dock system-wide for the whole session. enterKioskPresentation's
// NSApplicationPresentationHideDock only applies while FocusOS *itself* is
// frontmost — during a routine the routine's own app is frontmost, so the Dock
// reappears on hover. This sets the Dock's own autohide preference with an
// effectively infinite reveal delay (and restarts the Dock to apply), so it
// stays hidden no matter which app is focused. `hidden=false` removes the
// override and restores a normal Dock. Targets the console user's preference
// domain even when FocusOS runs as root.
void setSystemDockHidden(bool hidden);

// Make the shell window cover the WHOLE display — including the menu-bar / camera
// "notch" strip at the top — so the starfield reaches the top edge seamlessly
// around the notch instead of leaving the black safe-area bar native fullscreen
// produces. Lifts the window above the menu-bar level and sets its frame to the
// full screen frame; does NOT touch the window's style mask (Qt owns that).
// `nsView` is the QWindow's winId() (an NSView* on macOS). Runs on the main
// thread; safe to call repeatedly.
void coverScreenIncludingNotch(void *nsView);

// Undo coverScreenIncludingNotch(): drop the window back to the normal window
// level (below the menu bar) so it behaves like an ordinary, movable macOS
// window instead of a fullscreen kiosk cover. Used when "Access Desktop" lifts
// the lock and FocusOS should step aside into a regular window so the rest of
// the system is visible. Does NOT touch the window's style mask or frame (Qt
// owns those — the caller sets normal flags + a windowed geometry). Runs on the
// main thread; safe to call repeatedly. `nsView` is the QWindow's winId().
void restoreStandardWindowLevel(void *nsView);

// Whether FocusOS currently has Accessibility (AXIsProcessTrusted) — required
// for the CGEventTap key blocker to *suppress* events. When false, startInput
// Blocker still installs a tap but it can only observe, so the system shortcuts
// keep firing; the caller surfaces a "grant Accessibility" hint.
bool isAccessibilityTrusted();

// Swallow the macOS system shortcuts the kiosk presentation can't reach:
// Spotlight (⌘-Space), Mission Control / Spaces (⌃-arrows, F3/F4), Launchpad,
// the Dock toggle (⌥⌘D), screenshots (⌘⇧3/4/5), force-quit (⌥⌘Esc) and the
// app/window switchers. This is the macOS analog of the Linux launcher-killing
// watchdog — it stops the user reaching a launcher in the first place. Installs
// a CGEventTap on the main run loop; idempotent. stopInputBlocker() removes it.
bool startInputBlocker(QString *errorMessage = nullptr);
void stopInputBlocker();

// Strict engage-time sweep parity with the Linux backend: terminate (or, in
// dry-run, just list) every regular, Dock-visible GUI application the user is
// running EXCEPT FocusOS itself, Finder, and the keep-set (routine apps +
// always-allowed list, by bundle id / localized name / executable path).
// Returns the localized names acted on. Mirrors LinuxBackend::sweepBackgroundApps.
QStringList sweepOtherApplications(const QStringList &keepBundleIdentifiers,
                                   const QStringList &keepDisplayNames,
                                   const QStringList &keepExecutablePaths,
                                   bool dryRun);

// Deep-idle "app sleep": SIGSTOP every regular GUI app outside the keep-set so
// the CPU parks at idle, returning the frozen pids. resumeProcesses() SIGCONTs
// exactly those. Mirrors LinuxBackend::freeze/thawBackgroundProcesses.
QList<qint64> freezeOtherApplications(const QStringList &keepBundleIdentifiers,
                                      const QStringList &keepDisplayNames,
                                      const QStringList &keepExecutablePaths);
void resumeProcesses(const QList<qint64> &pids);

bool createDisplaySleepAssertion(quint32 *assertionId, QString *errorMessage = nullptr);
void releaseDisplaySleepAssertion(quint32 assertionId);

// Entitlement-free launch enforcement — the userland fallback for the AUTH_EXEC
// exec-blocker below, which needs a signed build carrying
// com.apple.developer.endpoint-security.client (an adhoc / SIP-on build can't run
// it, so the media key launching Apple Music, Dock launches of Spotify/Discord/a
// terminal, etc. otherwise slip past the lock). This subscribes to NSWorkspace's
// didLaunchApplication notification and, the instant a *blocked* app launches,
// terminates it and pulls FocusOS back to the front — so Apple Music never gets
// to take focus. It deliberately mirrors the exec-blocker's blocklist semantics
// (deny only the blocked set, never the routine's own apps / browser / system
// dialogs) so it can't kill an allowed launch. Same argument shape as
// startExecBlocker(); idempotent; the policy can be updated by calling it again.
void startLaunchWatcher(const QStringList &blockedNames,
                        const QStringList &blockedBundleIdentifiers,
                        const QStringList &allowedNames,
                        const QStringList &allowedBundleIdentifiers,
                        const QStringList &allowedExecutablePaths);
void stopLaunchWatcher();

bool startExecBlocker(const QStringList &blockedNames,
                      const QStringList &blockedBundleIdentifiers,
                      const QStringList &allowedNames,
                      const QStringList &allowedBundleIdentifiers,
                      const QStringList &allowedExecutablePaths,
                      QString *errorMessage = nullptr);
void stopExecBlocker();

// Split apply: buildNetworkFilterRuleset() does the slow, GUI-thread-unsafe-free
// DNS resolution and renders the pf ruleset string (safe to run on a worker
// thread). commitNetworkFilter() does the privileged part — write the ruleset
// file and drive pfctl — and MUST run on the GUI thread (QProcess driven by
// waitFor* off a raw worker thread is the crash the REFLECTIONS routine hit).
// applyNetworkFilter() chains both for the synchronous (non-engage) path.
QString buildNetworkFilterRuleset(const QStringList &allowedHosts);
bool commitNetworkFilter(const QString &ruleset, QString *errorMessage = nullptr);
bool applyNetworkFilter(const QStringList &allowedHosts, QString *errorMessage = nullptr);
void dropNetworkFilter();

} // namespace MacBackendNative
