#include "platform/linux/LinuxBackend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString expandedPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.startsWith(QStringLiteral("~/"))) {
        return QDir::homePath() + trimmed.mid(1);
    }
    return trimmed;
}

QString firstExecutable(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

QStringList desktopExecParts(const QString &desktopFilePath)
{
    QSettings desktopFile(desktopFilePath, QSettings::IniFormat);
    desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
    const QString execLine = desktopFile.value(QStringLiteral("Exec")).toString().trimmed();
    desktopFile.endGroup();

    QStringList parts = QProcess::splitCommand(execLine);
    QStringList cleaned;
    cleaned.reserve(parts.size());
    const QRegularExpression fieldCodePattern(QStringLiteral("%[fFuUdDnNickvm]"));
    for (QString part : parts) {
        if (part.startsWith(QLatin1Char('%'))) {
            continue;
        }
        part.replace(fieldCodePattern, QString());
        part = part.trimmed();
        if (!part.isEmpty()) {
            cleaned.append(part);
        }
    }
    return cleaned;
}

bool launchCommand(const QStringList &parts)
{
    if (parts.isEmpty()) {
        return false;
    }

    QString program = parts.first();
    QStringList arguments = parts.mid(1);
    if (!program.contains(QLatin1Char('/'))) {
        const QString resolved = QStandardPaths::findExecutable(program);
        if (!resolved.isEmpty()) {
            program = resolved;
        }
    }
    return QProcess::startDetached(program, arguments);
}

bool launchDesktopFile(const QString &desktopFilePath)
{
    const QStringList parts = desktopExecParts(desktopFilePath);
    if (launchCommand(parts)) {
        return true;
    }

    const QString gtkLaunch = QStandardPaths::findExecutable(QStringLiteral("gtk-launch"));
    if (!gtkLaunch.isEmpty()) {
        const QString desktopId = QFileInfo(desktopFilePath).fileName();
        return QProcess::startDetached(gtkLaunch, {desktopId});
    }
    return false;
}

bool startDetachedWithKdeEnvironment(const QString &program, const QStringList &arguments = {})
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("KDE"));
    environment.insert(QStringLiteral("XDG_SESSION_DESKTOP"), QStringLiteral("KDE"));
    environment.insert(QStringLiteral("KDE_FULL_SESSION"), QStringLiteral("true"));
    environment.insert(QStringLiteral("KDE_SESSION_VERSION"), QStringLiteral("6"));
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments(arguments);
    return process.startDetached();
}

void pkillExact(const QString &processName)
{
    QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), processName});
}

bool processRunning(const QString &processName)
{
    const QString pgrep = QStandardPaths::findExecutable(QStringLiteral("pgrep"));
    if (pgrep.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(pgrep, {QStringLiteral("-x"), processName});
    if (!process.waitForFinished(300)) {
        process.kill();
        process.waitForFinished(50);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

QString LinuxBackend::name() const
{
    return QStringLiteral("Linux/Wayland");
}

void LinuxBackend::prepareRoutineSession(const QStringList &appPaths)
{
    Q_UNUSED(appPaths)
    QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QStringLiteral("x-terminal-emulator")});
    terminateDesktopShell();
}

bool LinuxBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
    for (const QString &appPath : appPaths) {
        const QString path = expandedPath(appPath);
        if (path.isEmpty()) {
            continue;
        }

        const QFileInfo info(path);
        bool launched = false;
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            launched = launchDesktopFile(path);
        } else {
            launched = QProcess::startDetached(path, {});
        }

        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to launch %1").arg(path);
            }
            return false;
        }
    }
    return true;
}

bool LinuxBackend::openUrls(const QStringList &urls, QString *errorMessage)
{
    const QString opener = firstExecutable({
        QStringLiteral("xdg-open"),
        QStringLiteral("gio")
    });
    if (opener.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to find xdg-open or gio");
        }
        return false;
    }

    for (const QString &url : urls) {
        const QString trimmed = url.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QString normalized = QUrl::fromUserInput(trimmed).toString();
        const bool launched = QFileInfo(opener).fileName() == QStringLiteral("gio")
            ? QProcess::startDetached(opener, {QStringLiteral("open"), normalized})
            : QProcess::startDetached(opener, {normalized});
        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to open %1").arg(normalized);
            }
            return false;
        }
    }
    return true;
}

void LinuxBackend::terminateApps(const QStringList &appPaths)
{
    for (const QString &appPath : appPaths) {
        const QString trimmed = expandedPath(appPath);
        if (trimmed.isEmpty()) {
            continue;
        }

        const QFileInfo info(trimmed);
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            const QStringList parts = desktopExecParts(trimmed);
            if (!parts.isEmpty()) {
                QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QFileInfo(parts.first()).fileName()});
            }
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
    const QString terminal = firstExecutable({
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("konsole"),
        QStringLiteral("kgx"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("foot"),
        QStringLiteral("alacritty"),
        QStringLiteral("xterm")
    });
    const bool launched = !terminal.isEmpty() && QProcess::startDetached(terminal, {});
    if (!launched && errorMessage) {
        *errorMessage = QStringLiteral("Unable to open a system terminal");
    }
    return launched;
}

void LinuxBackend::terminateUnrestrictedApps()
{
    const QStringList terminalProcesses {
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("konsole"),
        QStringLiteral("kgx"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("foot"),
        QStringLiteral("alacritty"),
        QStringLiteral("xterm")
    };
    for (const QString &process : terminalProcesses) {
        pkillExact(process);
    }
    terminateDesktopShell();
}

bool LinuxBackend::launchDesktopShell(QString *errorMessage)
{
    if (processRunning(QStringLiteral("plasmashell"))) {
        return true;
    }

    const QStringList sidecars {
        QStringLiteral("kded6"),
        QStringLiteral("kded5"),
        QStringLiteral("kglobalaccel6"),
        QStringLiteral("kglobalaccel5"),
        QStringLiteral("polkit-kde-authentication-agent-1")
    };
    for (const QString &sidecar : sidecars) {
        const QString path = QStandardPaths::findExecutable(sidecar);
        if (!path.isEmpty()) {
            startDetachedWithKdeEnvironment(path);
        }
    }

    const QString plasmaShell = QStandardPaths::findExecutable(QStringLiteral("plasmashell"));
    const bool launched = !plasmaShell.isEmpty() && startDetachedWithKdeEnvironment(plasmaShell);
    const QString krunner = QStandardPaths::findExecutable(QStringLiteral("krunner"));
    if (launched && !krunner.isEmpty()) {
        startDetachedWithKdeEnvironment(krunner);
    }

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
        pkillExact(process);
    }
}
