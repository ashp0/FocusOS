#pragma once

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
