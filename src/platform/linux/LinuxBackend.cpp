#include "platform/linux/LinuxBackend.h"

#include <QProcess>

QString LinuxBackend::name() const
{
    return QStringLiteral("Linux/Wayland");
}

bool LinuxBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
    for (const QString &appPath : appPaths) {
        if (appPath.trimmed().isEmpty()) {
            continue;
        }
        if (!QProcess::startDetached(appPath, {})) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to launch %1").arg(appPath);
            }
            return false;
        }
    }
    return true;
}

void LinuxBackend::terminateApps(const QStringList &appPaths)
{
    for (const QString &appPath : appPaths) {
        const QString trimmed = appPath.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), trimmed});
    }
}

bool LinuxBackend::applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage)
{
    return m_netGate.apply(allowedHosts, errorMessage);
}

void LinuxBackend::dropNetworkPolicy()
{
    m_netGate.drop();
}

bool LinuxBackend::openSystemTerminal(QString *errorMessage)
{
    const bool launched = QProcess::startDetached(QStringLiteral("x-terminal-emulator"), {});
    if (!launched && errorMessage) {
        *errorMessage = QStringLiteral("Unable to open x-terminal-emulator");
    }
    return launched;
}

void LinuxBackend::terminateUnrestrictedApps()
{
    QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QStringLiteral("x-terminal-emulator")});
    terminateDesktopShell();
}

bool LinuxBackend::launchDesktopShell(QString *errorMessage)
{
    QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("plasmashell")});
    const bool launched = QProcess::startDetached(QStringLiteral("plasmashell"), {QStringLiteral("--no-respawn")});
    if (!launched && errorMessage) {
        *errorMessage = QStringLiteral("Unable to launch plasmashell");
    }
    return launched;
}

void LinuxBackend::terminateDesktopShell()
{
    // Kill plasmashell and the typical KDE sidecars it spawns / depends on.
    // -x matches exact names so we don't nuke unrelated processes.
    const QStringList shellProcesses {
        QStringLiteral("plasmashell"),
        QStringLiteral("krunner"),
        QStringLiteral("kded5"),
        QStringLiteral("kded6"),
        QStringLiteral("kglobalaccel5"),
        QStringLiteral("kglobalaccel6"),
        QStringLiteral("plasma-session"),
        QStringLiteral("plasma_waitforname"),
        QStringLiteral("kactivitymanagerd"),
        QStringLiteral("kwalletd5"),
        QStringLiteral("kwalletd6"),
        QStringLiteral("polkit-kde-authentication-agent-1"),
        QStringLiteral("ksmserver")
    };
    for (const QString &process : shellProcesses) {
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), process});
    }
}
