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

// Routine-launched apps inherit this env. KWALLET_DISABLED keeps KDE apps from
// asking for the wallet, NO_AT_BRIDGE silences the assistive-tech bridge, and
// the chromium overrides keep Brave/Chrome from triggering kwalletd.
QProcessEnvironment routineLaunchEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("KWALLET_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("NO_AT_BRIDGE"), QStringLiteral("1"));
    const QString existing = env.value(QStringLiteral("CHROMIUM_FLAGS"));
    env.insert(QStringLiteral("CHROMIUM_FLAGS"),
               (existing + QStringLiteral(" --password-store=basic --use-mock-keychain")).trimmed());
    return env;
}

bool isChromiumFamily(const QString &program)
{
    static const QStringList browsers {
        QStringLiteral("brave"), QStringLiteral("brave-browser"),
        QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable"),
        QStringLiteral("chromium"), QStringLiteral("chromium-browser"),
        QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable"),
        QStringLiteral("vivaldi"), QStringLiteral("vivaldi-stable"),
        QStringLiteral("opera")
    };
    const QString name = QFileInfo(program).fileName().toLower();
    for (const QString &browser : browsers) {
        if (name == browser || name.startsWith(browser + QLatin1Char('-'))) {
            return true;
        }
    }
    return false;
}

// Chromium-family browsers honor --password-store=basic to skip kwalletd. Inject
// it when we know the binary qualifies; otherwise leave the args alone.
QStringList chromiumWalletGuard(const QString &program, const QStringList &arguments)
{
    if (!isChromiumFamily(program)) {
        return arguments;
    }
    if (arguments.contains(QStringLiteral("--password-store=basic"))) {
        return arguments;
    }
    QStringList augmented = arguments;
    augmented.prepend(QStringLiteral("--password-store=basic"));
    return augmented;
}

bool launchCommand(const QStringList &parts, qint64 *pidOut = nullptr)
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
    arguments = chromiumWalletGuard(program, arguments);

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(routineLaunchEnvironment());
    qint64 pid = 0;
    const bool ok = process.startDetached(&pid);
    if (ok && pidOut) {
        *pidOut = pid;
    }
    return ok;
}

bool launchDesktopFile(const QString &desktopFilePath, qint64 *pidOut = nullptr)
{
    const QStringList parts = desktopExecParts(desktopFilePath);
    if (launchCommand(parts, pidOut)) {
        return true;
    }

    const QString gtkLaunch = QStandardPaths::findExecutable(QStringLiteral("gtk-launch"));
    if (!gtkLaunch.isEmpty()) {
        const QString desktopId = QFileInfo(desktopFilePath).fileName();
        QProcess process;
        process.setProgram(gtkLaunch);
        process.setArguments({desktopId});
        process.setProcessEnvironment(routineLaunchEnvironment());
        qint64 pid = 0;
        const bool ok = process.startDetached(&pid);
        if (ok && pidOut) {
            *pidOut = pid;
        }
        return ok;
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
    // Even when bringing up plasmashell we suppress kwalletd — the wallet is
    // what spawns the popup the user keeps hitting.
    environment.insert(QStringLiteral("KWALLET_DISABLED"), QStringLiteral("1"));
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

// Tries wmctrl first (X11), then qdbus (KWin). Returns -1 if neither succeeds.
int captureCurrentVirtualDesktop()
{
    const QString wmctrl = QStandardPaths::findExecutable(QStringLiteral("wmctrl"));
    if (!wmctrl.isEmpty()) {
        QProcess process;
        process.start(wmctrl, {QStringLiteral("-d")});
        if (process.waitForFinished(400) && process.exitCode() == 0) {
            const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
            for (const QString &line : output.split(QLatin1Char('\n'))) {
                // wmctrl -d marks the active desktop with '*'.
                if (line.contains(QStringLiteral(" * "))) {
                    return line.section(QLatin1Char(' '), 0, 0).toInt();
                }
            }
        }
    }

    const QString qdbus = firstExecutable({QStringLiteral("qdbus6"), QStringLiteral("qdbus")});
    if (!qdbus.isEmpty()) {
        QProcess process;
        process.start(qdbus, {QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("currentDesktop")});
        if (process.waitForFinished(400) && process.exitCode() == 0) {
            bool ok = false;
            const int idx = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed().toInt(&ok);
            if (ok) {
                return idx - 1; // qdbus uses 1-based, normalize to 0-based like wmctrl
            }
        }
    }

    return -1;
}

bool switchToVirtualDesktop(int zeroBasedIndex)
{
    if (zeroBasedIndex < 0) {
        return false;
    }
    const QString wmctrl = QStandardPaths::findExecutable(QStringLiteral("wmctrl"));
    if (!wmctrl.isEmpty()) {
        if (QProcess::execute(wmctrl, {QStringLiteral("-s"), QString::number(zeroBasedIndex)}) == 0) {
            return true;
        }
    }
    const QString qdbus = firstExecutable({QStringLiteral("qdbus6"), QStringLiteral("qdbus")});
    if (!qdbus.isEmpty()) {
        QProcess::execute(qdbus, {
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/KWin"),
            QStringLiteral("setCurrentDesktop"),
            QString::number(zeroBasedIndex + 1)
        });
        return true;
    }
    return false;
}

// Make sure at least `count` virtual desktops exist on KWin. Quietly no-ops if
// KWin/qdbus aren't available.
void ensureVirtualDesktopCount(int count)
{
    const QString qdbus = firstExecutable({QStringLiteral("qdbus6"), QStringLiteral("qdbus")});
    if (qdbus.isEmpty()) {
        return;
    }
    QProcess process;
    process.start(qdbus, {
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.freedesktop.DBus.Properties.Get"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        QStringLiteral("count")
    });
    int current = 0;
    if (process.waitForFinished(400) && process.exitCode() == 0) {
        current = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed().toInt();
    }
    while (current > 0 && current < count) {
        QProcess::execute(qdbus, {
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/VirtualDesktopManager"),
            QStringLiteral("org.kde.KWin.VirtualDesktopManager.createDesktop"),
            QString::number(current),
            QStringLiteral("Focus %1").arg(current + 1)
        });
        ++current;
    }
}

void killTrackedPids(QList<qint64> &pids)
{
    for (qint64 pid : pids) {
        if (pid <= 0) continue;
        // Soft signal first; the process may already be gone, which is fine.
        QProcess::execute(QStringLiteral("kill"), {QString::number(pid)});
    }
    pids.clear();
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

    // Park routine apps on a dedicated KDE virtual desktop so the user's
    // regular desktop stays uncluttered. Best-effort: works on KWin + wmctrl.
    if (m_savedDesktopIndex < 0) {
        m_savedDesktopIndex = captureCurrentVirtualDesktop();
    }
    ensureVirtualDesktopCount(2);
    switchToVirtualDesktop(1);
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
        qint64 pid = 0;
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            launched = launchDesktopFile(path, &pid);
        } else {
            launched = launchCommand({path}, &pid);
        }

        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to launch %1").arg(path);
            }
            return false;
        }
        if (pid > 0) {
            m_sessionPids.append(pid);
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
        const QStringList args = QFileInfo(opener).fileName() == QStringLiteral("gio")
            ? QStringList{QStringLiteral("open"), normalized}
            : QStringList{normalized};
        QProcess process;
        process.setProgram(opener);
        process.setArguments(args);
        process.setProcessEnvironment(routineLaunchEnvironment());
        qint64 pid = 0;
        const bool launched = process.startDetached(&pid);
        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to open %1").arg(normalized);
            }
            return false;
        }
        if (pid > 0) {
            m_sessionPids.append(pid);
        }
    }
    return true;
}

void LinuxBackend::terminateApps(const QStringList &appPaths)
{
    // Kill anything we tracked from launchApps/openUrls first — this catches
    // browser windows that opened on free PIDs we wouldn't otherwise reach.
    killTrackedPids(m_sessionPids);

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

    // Drop back to the user's original virtual desktop so they don't land on
    // the empty "Focus" desktop after their routine ends.
    if (m_savedDesktopIndex >= 0) {
        switchToVirtualDesktop(m_savedDesktopIndex);
        m_savedDesktopIndex = -1;
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
    // The 30-min Other Access timer fires this. We want to take the user back
    // to a clean FocusOS state: kill terminals, the desktop shell, and any
    // routine-spawned apps we still track.
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
    killTrackedPids(m_sessionPids);
    terminateDesktopShell();

    if (m_savedDesktopIndex >= 0) {
        switchToVirtualDesktop(m_savedDesktopIndex);
        m_savedDesktopIndex = -1;
    }
}

bool LinuxBackend::launchDesktopShell(QString *errorMessage)
{
    if (processRunning(QStringLiteral("plasmashell"))) {
        return true;
    }

    // The desktop shell goes on its own virtual desktop too so the user can
    // swipe between FocusOS and the real desktop instead of layering them.
    if (m_savedDesktopIndex < 0) {
        m_savedDesktopIndex = captureCurrentVirtualDesktop();
    }
    ensureVirtualDesktopCount(2);
    switchToVirtualDesktop(1);

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
    // -x matches exact names so we don't nuke unrelated processes. kwalletd is
    // included so the wallet popup the user keeps hitting goes away with the shell.
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
