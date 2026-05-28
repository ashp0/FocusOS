#pragma once

#include "platform/PlatformBackend.h"
#include "platform/linux/NetGate.h"

#include <QList>
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

private:
    void startLockdownWatchdog();
    void stopLockdownWatchdog();
    void tickLockdownWatchdog() const;
    QStringList alwaysAllowedProcessNames() const;
    void loadWindowPinScript();
    void unloadWindowPinScript();

    NetGate m_netGate;
    QList<qint64> m_sessionPids;
    QTimer m_lockdownTimer;
    bool m_lockdownActive = false;
    int m_windowPinScriptHandle = -1;
    QString m_windowPinScriptPlugin;
    QStringList m_alwaysAllowedCommandLines;
};
