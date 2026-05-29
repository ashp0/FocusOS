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
    void tickLockdownWatchdog() const;
    QStringList alwaysAllowedProcessNames() const;
    QString watchdogScriptPath() const;

    NetGate m_netGate;
    QList<qint64> m_sessionPids;
    QTimer m_lockdownTimer;
    bool m_lockdownActive = false;
    QStringList m_alwaysAllowedCommandLines;
    // Holds a systemd-inhibit lock (--what=idle) while a routine wants the
    // screen kept on; terminated to release. Reaped on destruction.
    QProcess m_displayInhibitor;
};
