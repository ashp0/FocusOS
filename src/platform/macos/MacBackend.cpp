#include "platform/macos/MacBackend.h"

#include <QFileInfo>
#include <QProcess>

QString MacBackend::name() const
{
    return QStringLiteral("macOS");
}

bool MacBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
    for (const QString &appPath : appPaths) {
        if (appPath.trimmed().isEmpty()) {
            continue;
        }

        const QFileInfo info(appPath);
        bool launched = false;
        if (info.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0 || appPath.endsWith(QStringLiteral(".app"))) {
            launched = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {appPath});
        } else {
            launched = QProcess::startDetached(appPath, {});
        }

        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to launch %1").arg(appPath);
            }
            return false;
        }
    }

    return true;
}

void MacBackend::terminateApps(const QStringList &appPaths)
{
    for (const QString &appPath : appPaths) {
        const QString trimmed = appPath.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QFileInfo info(trimmed);
        const bool looksLikeBundle = info.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0 ||
                                     trimmed.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive);
        if (looksLikeBundle) {
            QString appName = info.completeBaseName();
            if (appName.isEmpty()) {
                appName = info.baseName();
            }
            if (!appName.isEmpty()) {
                QProcess::execute(QStringLiteral("/usr/bin/osascript"), {
                    QStringLiteral("-e"),
                    QStringLiteral("tell application \"%1\" to quit").arg(appName)
                });
                continue;
            }
        }

        QProcess::execute(QStringLiteral("/usr/bin/pkill"), {QStringLiteral("-f"), trimmed});
    }
}

bool MacBackend::applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage)
{
    Q_UNUSED(allowedHosts)
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void MacBackend::dropNetworkPolicy()
{
}

bool MacBackend::openSystemTerminal(QString *errorMessage)
{
    const bool launched = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), QStringLiteral("Terminal")});
    if (!launched && errorMessage) {
        *errorMessage = QStringLiteral("Unable to open Terminal");
    }
    return launched;
}

void MacBackend::terminateUnrestrictedApps()
{
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), {
        QStringLiteral("-e"),
        QStringLiteral("tell application \"Terminal\" to quit")
    });
}
