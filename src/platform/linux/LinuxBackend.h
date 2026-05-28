#pragma once

#include "platform/PlatformBackend.h"
#include "platform/linux/NetGate.h"

class LinuxBackend final : public PlatformBackend
{
public:
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

private:
    NetGate m_netGate;
};
