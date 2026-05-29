#pragma once

#include "platform/PlatformBackend.h"
#include "platform/linux/NetGate.h"

#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

class LinuxBackend final : public PlatformBackend
{
public:
    LinuxBackend();
    ~LinuxBackend() override;

    QString name() const override;
    void prepareRoutineSession(const QStringList &appPaths) override;
    bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) override;
    bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) override;
    void terminateApps(const QStringList &appPaths) override;
    bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) override;
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

private:
    void startLockdownWatchdog();
    void stopLockdownWatchdog();
    void tickLockdownWatchdog();
    QStringList alwaysAllowedProcessNames() const;
    QString watchdogScriptPath() const;

    // While a network lock is live: if a browser is running but the blocker
    // extension stopped talking to its native host (i.e. it was disabled or
    // removed), clamp the network to a full deny and nag the user to re-enable
    // it; restore the routine allowlist once the extension is back.
    void ensureWatchdogTimer();
    void maybeStopWatchdogTimer();
    void enforceBlockerExtension();
    bool blockerExtensionAlive() const;
    bool chromiumBrowserRunning() const;
    void showExtensionDisabledAlert() const;

    NetGate m_netGate;
    QList<qint64> m_sessionPids;
    QTimer m_lockdownTimer;
    bool m_lockdownActive = false;
    // Network-lock state, tracked so the watchdog can restore the routine
    // allowlist after an extension-disabled full-deny clamp.
    bool m_networkLockActive = false;
    bool m_extensionBanActive = false;
    QStringList m_activeAllowedHosts;
    // Wall-clock (ms) the "browser up but extension beacon stale" condition has
    // held continuously; 0 when clear. The ban only fires once it has persisted
    // past a debounce, so browser/extension startup lag doesn't false-trigger.
    qint64 m_extensionMissingSinceMs = 0;
    qint64 m_lastExtensionAlertMs = 0;
    QStringList m_alwaysAllowedCommandLines;
    // Holds a systemd-inhibit lock (--what=idle) while a routine wants the
    // screen kept on; terminated to release. Reaped on destruction.
    QProcess m_displayInhibitor;
};
