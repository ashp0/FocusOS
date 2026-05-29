#pragma once

#include "platform/PlatformBackend.h"

#include <QProcess>

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
    void setDisplaySleepInhibited(bool inhibited) override;

private:
    // Holds `caffeinate` while a routine wants the display kept on; terminated
    // (or never started) otherwise. Destroyed -> child is reaped automatically.
    QProcess m_caffeinate;
};
