#include "platform/linux/LinuxBackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

namespace {

constexpr const char *kKWinService = "org.kde.KWin";
constexpr const char *kVdmPath = "/VirtualDesktopManager";
constexpr const char *kVdmIface = "org.kde.KWin.VirtualDesktopManager";

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

bool launchDesktopFile(const QString &desktopFilePath, const QStringList &extraArgs, qint64 *pidOut = nullptr)
{
    QStringList parts = desktopExecParts(desktopFilePath);
    parts.append(extraArgs);
    if (launchCommand(parts, pidOut)) {
        return true;
    }

    // gtk-launch doesn't let us pass arguments cleanly, so fall back only when
    // the routine didn't ask for extra args.
    if (!extraArgs.isEmpty()) {
        return false;
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

bool launchKioskBrowser(const QString &url, qint64 *pidOut = nullptr, QString *errorMessage = nullptr)
{
    const QString browser = firstExecutable({
        QStringLiteral("brave-browser"),
        QStringLiteral("brave"),
        QStringLiteral("google-chrome-stable"),
        QStringLiteral("google-chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("chromium-browser"),
        QStringLiteral("microsoft-edge-stable"),
        QStringLiteral("microsoft-edge")
    });
    if (browser.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No chromium-family browser installed for kiosk mode");
        }
        return false;
    }

    // Use a throwaway profile per kiosk so the routine doesn't bleed into the
    // user's main browser session (no logged-in accounts, no extensions, no
    // history). The dir is created under XDG_RUNTIME_DIR so it cleans up at
    // session end.
    const QString runtimeDir = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("XDG_RUNTIME_DIR"),
        QStringLiteral("/tmp"));
    const QString kioskProfile = QStringLiteral("%1/focusos-kiosk-%2")
                                     .arg(runtimeDir)
                                     .arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(kioskProfile);

    const QStringList args {
        QStringLiteral("--app=%1").arg(url),
        QStringLiteral("--user-data-dir=%1").arg(kioskProfile),
        QStringLiteral("--start-fullscreen"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--password-store=basic"),
        QStringLiteral("--use-mock-keychain"),
        QStringLiteral("--disable-features=Translate")
    };

    QProcess process;
    process.setProgram(browser);
    process.setArguments(args);
    process.setProcessEnvironment(routineLaunchEnvironment());
    qint64 pid = 0;
    const bool ok = process.startDetached(&pid);
    if (ok && pidOut) {
        *pidOut = pid;
    }
    if (!ok && errorMessage) {
        *errorMessage = QStringLiteral("Unable to launch kiosk browser %1").arg(browser);
    }
    return ok;
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

// ─── KWin DBus helpers ───────────────────────────────────────────────────────
//
// We talk to KWin's VirtualDesktopManager directly over the user session bus
// instead of shelling out to `qdbus`. Subprocess qdbus made the switch racy:
// the property set returned 0 even when KWin had not yet processed the new
// "current" value, so apps launched before the desktop actually flipped.

QStringList kwinListDesktopIds()
{
    QDBusInterface iface(QString::fromLatin1(kKWinService),
                         QString::fromLatin1(kVdmPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return {};
    }

    const QDBusMessage reply = iface.call(QStringLiteral("Get"),
                                          QString::fromLatin1(kVdmIface),
                                          QStringLiteral("desktops"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    // The reply variant wraps an a(uss). We don't care about the position
    // ordering KWin uses internally — KWin already returns them in the order
    // the user sees, and we only need the ids.
    QDBusArgument arg = reply.arguments().first().value<QDBusVariant>().variant().value<QDBusArgument>();
    QStringList ids;
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        uint position = 0;
        QString id;
        QString name;
        arg >> position >> id >> name;
        arg.endStructure();
        Q_UNUSED(position)
        Q_UNUSED(name)
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    arg.endArray();
    return ids;
}

QString kwinCurrentDesktopId()
{
    QDBusInterface iface(QString::fromLatin1(kKWinService),
                         QString::fromLatin1(kVdmPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return {};
    }

    const QDBusMessage reply = iface.call(QStringLiteral("Get"),
                                          QString::fromLatin1(kVdmIface),
                                          QStringLiteral("current"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toString();
}

int kwinCurrentDesktopIndex()
{
    const QString currentId = kwinCurrentDesktopId();
    if (currentId.isEmpty()) {
        return -1;
    }
    return kwinListDesktopIds().indexOf(currentId);
}

uint kwinDesktopCount()
{
    QDBusInterface iface(QString::fromLatin1(kKWinService),
                         QString::fromLatin1(kVdmPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return 0;
    }
    const QDBusMessage reply = iface.call(QStringLiteral("Get"),
                                          QString::fromLatin1(kVdmIface),
                                          QStringLiteral("count"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return 0;
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toUInt();
}

bool kwinSetCurrentDesktop(const QString &desktopId)
{
    if (desktopId.isEmpty()) {
        return false;
    }

    QDBusInterface iface(QString::fromLatin1(kKWinService),
                         QString::fromLatin1(kVdmPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    // Properties.Set expects a variant; QDBusVariant ensures KWin sees a
    // string variant rather than us being silently coerced.
    const QDBusMessage reply = iface.call(QStringLiteral("Set"),
                                          QString::fromLatin1(kVdmIface),
                                          QStringLiteral("current"),
                                          QVariant::fromValue(QDBusVariant(desktopId)));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return false;
    }

    // Poll until KWin reports the new current desktop — the property change is
    // applied asynchronously, and we don't want to launch routine apps before
    // KWin has actually flipped to the focus desktop.
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (waitTimer.elapsed() < 1500) {
        if (kwinCurrentDesktopId() == desktopId) {
            return true;
        }
        QThread::msleep(40);
    }
    return kwinCurrentDesktopId() == desktopId;
}

QString kwinEnsureDesktop(int desiredCount, const QString &nameForNew)
{
    const QStringList existing = kwinListDesktopIds();
    if (existing.size() >= desiredCount) {
        return existing.at(desiredCount - 1);
    }

    QDBusInterface vdm(QString::fromLatin1(kKWinService),
                       QString::fromLatin1(kVdmPath),
                       QString::fromLatin1(kVdmIface),
                       QDBusConnection::sessionBus());
    if (!vdm.isValid()) {
        return {};
    }

    int current = existing.size();
    while (current < desiredCount) {
        vdm.call(QStringLiteral("createDesktop"),
                 static_cast<uint>(current),
                 nameForNew);

        // KWin emits desktopCreated after the new id is allocated, but our
        // synchronous call returns before the emission propagates. Poll until
        // the desktops list grows.
        QElapsedTimer waitTimer;
        waitTimer.start();
        QStringList updated = kwinListDesktopIds();
        while (updated.size() <= current && waitTimer.elapsed() < 1500) {
            QThread::msleep(50);
            updated = kwinListDesktopIds();
        }
        if (updated.size() <= current) {
            return {};
        }
        current = updated.size();
    }

    const QStringList finalIds = kwinListDesktopIds();
    if (finalIds.size() < desiredCount) {
        return {};
    }
    return finalIds.at(desiredCount - 1);
}

void killTrackedPids(QList<qint64> &pids)
{
    for (qint64 pid : pids) {

        if (pid <= 0) {
            continue;
        }

        QProcess::execute(
            QStringLiteral("kill"),
            {QString::number(pid)}
        );
    }

    pids.clear();
}

// Parse a routine app entry. Routine apps are stored as command strings now
// rather than bare paths, so the user can pass arguments — e.g.
//   "/usr/bin/code /home/me/project"
// or kiosk-mode browser windows pinned to a single URL via:
//   "kiosk:https://www.youtube.com/watch?v=ABCDEFG"
struct ParsedAppEntry
{
    bool kiosk = false;
    QString kioskUrl;
    QString path;         // first token (path or program name) for non-kiosk entries
    QStringList args;     // remaining tokens
};

ParsedAppEntry parseAppEntry(const QString &rawEntry)
{
    ParsedAppEntry parsed;
    const QString trimmed = rawEntry.trimmed();
    if (trimmed.isEmpty()) {
        return parsed;
    }
    if (trimmed.startsWith(QStringLiteral("kiosk:"), Qt::CaseInsensitive)) {
        parsed.kiosk = true;
        parsed.kioskUrl = trimmed.mid(QStringLiteral("kiosk:").size()).trimmed();
        return parsed;
    }

    QStringList parts = QProcess::splitCommand(trimmed);
    if (parts.isEmpty()) {
        parsed.path = expandedPath(trimmed);
        return parsed;
    }
    parsed.path = expandedPath(parts.takeFirst());
    for (QString &arg : parts) {
        arg = expandedPath(arg);
    }
    parsed.args = parts;
    return parsed;
}

} // namespace

LinuxBackend::LinuxBackend()
{
    m_lockdownTimer.setInterval(1500);
    m_lockdownTimer.setSingleShot(false);
    // Receiver is the timer itself so the lambda lifetime is tied to it; we
    // never make LinuxBackend a QObject because it would force moc + a parent
    // pointer dance that the call sites don't need.
    QObject::connect(&m_lockdownTimer, &QTimer::timeout, &m_lockdownTimer, [this] {
        tickLockdownWatchdog();
    });
}

LinuxBackend::~LinuxBackend()
{
    stopLockdownWatchdog();
}

QString LinuxBackend::name() const
{
    return QStringLiteral("Linux/KWin Wayland");
}

void LinuxBackend::prepareRoutineSession(const QStringList &appPaths)
{
    Q_UNUSED(appPaths)

    QProcess::execute(
        QStringLiteral("pkill"),
        {
            QStringLiteral("-f"),
            QStringLiteral("x-terminal-emulator")
        }
    );

    terminateDesktopShell();

    if (m_savedDesktopIndex < 0) {
        m_savedDesktopIndex = kwinCurrentDesktopIndex();
    }

    const QString focusDesktopId = kwinEnsureDesktop(2, QStringLiteral("Focus 2"));

    if (!focusDesktopId.isEmpty()) {
        kwinSetCurrentDesktop(focusDesktopId);
    }

    // Even after the property echo confirms the switch, KWin sometimes still
    // races the very first window creation. A small extra settle window beats
    // the alternative (apps appearing on the old desktop).
    QThread::msleep(200);

    startLockdownWatchdog();
}

bool LinuxBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
    for (const QString &rawEntry : appPaths) {
        const ParsedAppEntry parsed = parseAppEntry(rawEntry);

        if (parsed.kiosk) {
            if (parsed.kioskUrl.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Kiosk entry has no URL: %1").arg(rawEntry);
                }
                return false;
            }
            qint64 pid = 0;
            QString launchError;
            if (!launchKioskBrowser(parsed.kioskUrl, &pid, &launchError)) {
                if (errorMessage) {
                    *errorMessage = launchError.isEmpty()
                                        ? QStringLiteral("Kiosk launch failed for %1").arg(parsed.kioskUrl)
                                        : launchError;
                }
                return false;
            }
            if (pid > 0) {
                m_sessionPids.append(pid);
            }
            continue;
        }

        if (parsed.path.isEmpty()) {
            continue;
        }

        const QFileInfo info(parsed.path);
        bool launched = false;
        qint64 pid = 0;
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            launched = launchDesktopFile(parsed.path, parsed.args, &pid);
        } else {
            QStringList parts;
            parts << parsed.path;
            parts.append(parsed.args);
            launched = launchCommand(parts, &pid);
        }

        if (!launched) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to launch %1").arg(parsed.path);
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
    stopLockdownWatchdog();

    // Kill anything we tracked from launchApps/openUrls first — this catches
    // browser windows that opened on free PIDs we wouldn't otherwise reach.
    killTrackedPids(m_sessionPids);

    for (const QString &rawEntry : appPaths) {
        const ParsedAppEntry parsed = parseAppEntry(rawEntry);
        if (parsed.kiosk) {
            // Kiosk browsers are tracked by PID, killed above. Nothing else to
            // do — we can't match on the temporary user-data-dir reliably.
            continue;
        }

        const QString candidate = parsed.path;
        if (candidate.isEmpty()) {
            continue;
        }

        const QFileInfo info(candidate);
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            const QStringList parts = desktopExecParts(candidate);
            if (!parts.isEmpty()) {
                QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QFileInfo(parts.first()).fileName()});
            }
        }
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), candidate});
    }

    restoreShellPlacement();
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
    stopLockdownWatchdog();

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

    restoreShellPlacement();
}

bool LinuxBackend::launchDesktopShell(QString *errorMessage)
{
    if (processRunning(QStringLiteral("plasmashell"))) {
        return true;
    }

    // The desktop shell goes on its own virtual desktop too so the user can
    // swipe between FocusOS and the real desktop instead of layering them.
    if (m_savedDesktopIndex < 0) {
        m_savedDesktopIndex = kwinCurrentDesktopIndex();
    }
    const QString focusDesktopId = kwinEnsureDesktop(2, QStringLiteral("Focus 2"));
    if (!focusDesktopId.isEmpty()) {
        kwinSetCurrentDesktop(focusDesktopId);
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

    // Open a terminal alongside the shell so unrestricted access actually
    // gives the user something to do — this used to fire from
    // RoutineManager::unlockOtherAccess but that obscured the admin modal.
    QString terminalError;
    openSystemTerminal(&terminalError);

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

void LinuxBackend::restoreShellPlacement()
{
    if (m_savedDesktopIndex < 0) {
        return;
    }

    const QStringList desktops = kwinListDesktopIds();
    if (m_savedDesktopIndex < desktops.size()) {
        kwinSetCurrentDesktop(desktops.at(m_savedDesktopIndex));
    }
    m_savedDesktopIndex = -1;
}

void LinuxBackend::startLockdownWatchdog()
{
    m_lockdownActive = true;
    if (!m_lockdownTimer.isActive()) {
        m_lockdownTimer.start();
    }
    // Fire once immediately so the first kill happens before the user can
    // open the spotlight.
    tickLockdownWatchdog();
}

void LinuxBackend::stopLockdownWatchdog()
{
    m_lockdownActive = false;
    if (m_lockdownTimer.isActive()) {
        m_lockdownTimer.stop();
    }
}

void LinuxBackend::tickLockdownWatchdog() const
{
    if (!m_lockdownActive) {
        return;
    }
    // While a routine is engaged we keep killing the spotlight / launcher
    // processes that let the user pull up arbitrary apps. plasmashell is in
    // here because kded6 happily respawns it; krunner is the KDE spotlight.
    static const QStringList outlawed {
        QStringLiteral("krunner"),
        QStringLiteral("plasmashell"),
        QStringLiteral("kickoff"),
        QStringLiteral("rofi"),
        QStringLiteral("dmenu"),
        QStringLiteral("wofi"),
        QStringLiteral("synapse"),
        QStringLiteral("ulauncher"),
        QStringLiteral("albert")
    };
    for (const QString &name : outlawed) {
        pkillExact(name);
    }
}
