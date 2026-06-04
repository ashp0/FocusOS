#pragma once

#include "platform/PlatformBackend.h"

#include <QList>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <memory>

class MacBackend final : public PlatformBackend
{
public:
    MacBackend();
    ~MacBackend() override;

    QString name() const override;
    void prepareRoutineSession(const QStringList &appPaths) override;
    bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) override;
    bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) override;
    void terminateApps(const QStringList &appPaths) override;
    void endRoutineLockdown() override;
    void quitBackgroundApps(const QStringList &allowedCommandLines) override;
    QStringList previewBackgroundAppQuit(const QStringList &allowedCommandLines) override;
    void lockScreen() override;
    void unlockScreen() override;
    void sleepDisplay() override;
    void wakeDisplay() override;
    bool suspendSystem() override;
    void freezeBackgroundProcesses() override;
    void thawBackgroundProcesses() override;
    bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) override;
    void applyNetworkPolicyAsync(const QStringList &allowedHosts,
                                 std::function<void(bool, const QString &)> onComplete) override;
    void applyBrowserBlockerPolicy(const QStringList &allowedHosts) override;
    void dropNetworkPolicy() override;
    bool openSystemTerminal(QString *errorMessage = nullptr) override;
    void terminateUnrestrictedApps() override;
    bool launchDesktopShell(QString *errorMessage = nullptr) override;
    void terminateDesktopShell() override;
    bool desktopShellSupported() const override { return true; }
    void restoreShellPlacement() override;
    void setAlwaysAllowedApps(const QStringList &commandLines) override;
    void startWatchdog(const QString &binaryPath) override;
    bool restoreLoginSessions(QString *errorMessage = nullptr) override;
    void setDisplaySleepInhibited(bool inhibited) override;
    void releaseDisplaySleepInhibitors() override;
    void runSessionStartupItems() override;
    bool signOutSupported() const override { return true; }
    bool signOut(QString *errorMessage = nullptr) override;

    // Public so the free helpers in MacBackend.cpp's anonymous namespace can
    // match tracked processes against routine entries.
    struct LaunchedProcess
    {
        qint64 pid = 0;
        QString sourceEntry;
        QString executablePath;
        QString bundleIdentifier;
        QString displayName;
    };

private:
    void startLockdown();
    void stopLockdown();
    void applyAquaUiLockdown();
    void restoreAquaUiLockdown();
    void killTrackedProcesses(const QStringList &entries = {});
    QStringList allowedCommandLines() const;
    // The always-on home-screen posture: hide the Dock/menu bar (kiosk
    // presentation) and start the system-shortcut blocker. Applied at startup and
    // re-applied whenever a routine or admin-desktop access ends, so FocusOS stays
    // a locked shell "in general", not only mid-routine. startInputBlocker() needs
    // Accessibility access; m_inputBlockerWarned dedupes the one-time warning.
    void applyBaselineKioskPosture();

    // Idempotent wrapper around MacBackendNative::setMissionControlDisabled — the
    // locked home screen disables Mission Control / Spaces (so a routine end stays
    // genuinely locked until the 6-digit code), a routine or Access-Desktop unlock
    // re-enables it. The guard flag avoids re-running `killall Dock` on every
    // posture re-apply (engage/end/End-Access all funnel through these paths).
    void setMissionControlDisabled(bool disabled);
    bool m_missionControlDisabled = false;

    // --- Blocker extension presence clamp (parity with LinuxBackend) ---
    // While a pf network lock is active, the native host rewrites
    // ~/.focusos/blocker/host-alive every ~1.5s. If a *user* Brave/Chromium is
    // running but that beacon goes stale (the user disabled/removed the manually
    // installed extension), clamp pf to full-deny until it reconnects. The
    // extension on macOS is user-installed and therefore user-removable, so this
    // is the tamper backstop that makes a network-locked routine hold.
    void armBlockerPresenceWatch(const QStringList &allowedHosts);
    void disarmBlockerPresenceWatch();
    void enforceBlockerExtension();
    bool blockerExtensionAlive() const;
    bool chromiumBrowserRunning() const;
    void showExtensionDisabledAlert() const;

    QTimer m_presenceTimer;
    bool m_presenceTimerWired = false;
    bool m_networkLockActive = false;
    bool m_extensionSeenAlive = false;
    bool m_extensionBanActive = false;
    qint64 m_extensionMissingSinceMs = 0;
    qint64 m_lastExtensionAlertMs = 0;
    QStringList m_activeAllowedHosts;

    QList<LaunchedProcess> m_launchedProcesses;
    QStringList m_alwaysAllowedCommandLines;
    QStringList m_sessionAppEntries;
    QList<qint64> m_frozenPids;
    bool m_lockdownActive = false;
    bool m_desktopAccessOpen = false;
    bool m_inputBlockerWarned = false;
    quint32 m_displayAssertionId = 0;
    // Fallback only. The primary path is an IOPM assertion, but older or
    // restricted systems can still use caffeinate while this instance lives.
    QProcess m_caffeinate;
};
