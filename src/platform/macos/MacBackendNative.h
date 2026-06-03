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

bool startExecBlocker(const QStringList &blockedNames,
                      const QStringList &blockedBundleIdentifiers,
                      const QStringList &allowedNames,
                      const QStringList &allowedBundleIdentifiers,
                      const QStringList &allowedExecutablePaths,
                      QString *errorMessage = nullptr);
void stopExecBlocker();

bool applyNetworkFilter(const QStringList &allowedHosts, QString *errorMessage = nullptr);
void dropNetworkFilter();

} // namespace MacBackendNative
