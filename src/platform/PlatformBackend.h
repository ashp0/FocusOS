#pragma once

#include <QString>
#include <QStringList>

class PlatformBackend
{
public:
    virtual ~PlatformBackend() = default;

    virtual QString name() const = 0;
    virtual void prepareRoutineSession(const QStringList &appPaths) { Q_UNUSED(appPaths); }
    virtual bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) = 0;
    virtual bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) = 0;
    virtual void terminateApps(const QStringList &appPaths) = 0;
    virtual bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) = 0;
    virtual void dropNetworkPolicy() = 0;
    virtual bool openSystemTerminal(QString *errorMessage = nullptr) = 0;
    virtual void terminateUnrestrictedApps() = 0;
    virtual bool launchDesktopShell(QString *errorMessage = nullptr) { Q_UNUSED(errorMessage); return false; }
    virtual void terminateDesktopShell() {}
    virtual bool desktopShellSupported() const { return false; }
};
