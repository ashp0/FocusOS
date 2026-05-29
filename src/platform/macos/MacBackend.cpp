#include "platform/macos/MacBackend.h"

#include "blocker/BlockerPolicy.h"

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

bool MacBackend::openUrls(const QStringList &urls, QString *errorMessage)
{
    for (const QString &url : urls) {
        const QString trimmed = url.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"), {trimmed})) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to open %1").arg(trimmed);
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
    // No NetGate on macOS — the blocker extension is the only enforcement path.
    // Writing the active policy here is what makes a routine's network lock real.
    if (errorMessage) {
        errorMessage->clear();
    }
    BlockerPolicy::write(true, allowedHosts);
    return true;
}

void MacBackend::dropNetworkPolicy()
{
    BlockerPolicy::write(false, {});
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

void MacBackend::setDisplaySleepInhibited(bool inhibited)
{
    if (inhibited) {
        if (m_caffeinate.state() != QProcess::NotRunning) {
            return;
        }
        // caffeinate -d holds an assertion that prevents the display from
        // sleeping; it lives until we terminate it (or FocusOS exits).
        m_caffeinate.start(QStringLiteral("/usr/bin/caffeinate"), {QStringLiteral("-d")});
        return;
    }

    if (m_caffeinate.state() != QProcess::NotRunning) {
        m_caffeinate.terminate();
        if (!m_caffeinate.waitForFinished(1000)) {
            m_caffeinate.kill();
            m_caffeinate.waitForFinished(200);
        }
    }
}
