#pragma once

#include "platform/PlatformBackend.h"

class MacBackend final : public PlatformBackend
{
public:
    QString name() const override;
    bool launchApps(const QStringList &appPaths, QString *errorMessage = nullptr) override;
    bool openUrls(const QStringList &urls, QString *errorMessage = nullptr) override;
    void terminateApps(const QStringList &appPaths) override;
    bool applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage = nullptr) override;
    void dropNetworkPolicy() override;
    bool openSystemTerminal(QString *errorMessage = nullptr) override;
    void terminateUnrestrictedApps() override;
};
