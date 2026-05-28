// ─── FocusOS Linux backend ──────────────────────────────────────────────────
//
// This is the daily-driver target — KDE Plasma 6 on Wayland with KWin as the
// compositor. The backend is structured around three responsibilities:
//
//   1. Launching routine apps and pinning them to the focus virtual desktop
//   2. Applying / dropping the nftables network policy (NetGate)
//   3. Running a lockdown watchdog that aggressively kills launcher /
//      spotlight surfaces while a routine is engaged
//
// FUTURE CONCERNS — things to address before this can be a true single-purpose
// productivity OS, in priority order:
//
//   * **Session-state persistence across reboots.** The user's goal is "if
//     you chose 2 hours to learn rust, it will never stop until those 2
//     hours are finished, even across reboot." Today an active routine is
//     in-memory only — power loss / crash / reboot drops it. RoutineManager
//     should checkpoint active session state (id, started_at, total
//     seconds, remaining seconds at last tick) into ~/.focusos/active.json
//     on every tick, and re-engage on launch if found and not expired.
//     Min-time floor should re-apply.
//
//   * **System-tray / notification surfaces.** KDE's StatusNotifierWatcher
//     keeps tray items alive after plasmashell dies. We don't kill it yet
//     because some routine apps (Slack-equivalents, password managers)
//     refuse to start without it. Audit each routine app individually.
//
//   * **Network monitor indicator.** No way to see if wifi is up while
//     locked down. systemStatus already plumbs battery; add NetworkManager
//     DBus state (org.freedesktop.NetworkManager.Connectivity) so the user
//     can tell whether allowed sites are reachable.
//
//   * **Notification daemon.** dbus-daemon will queue notifications for
//     org.freedesktop.Notifications. We don't currently surface them. A
//     focused user probably wants them muted by default but reachable in
//     the Settings tab.
//
//   * **Per-app sandboxing (Landlock / AppArmor / bubblewrap).** Even an
//     allowed editor can spawn a browser. The DECISIONS.md notes a
//     compositor-or-supervisor model as the right boundary; until then,
//     wrap routine apps in bwrap with no /home/$user/.config/google-chrome
//     etc. visible.
//
//   * **Display-server / compositor restart.** kwin_wayland crashes occur.
//     If kwin dies during a routine, the screen goes black and the user
//     can't reach FocusOS. Watchdog should detect missing kwin and respawn,
//     or pin focusos to a known-good DRM output as fallback.
//
//   * **DRM brightness / VT switch lockdown.** Ctrl+Alt+F2 currently
//     escapes to a TTY. The systemd-logind seat config should disable
//     `KillUserProcesses=no` and the VT shortcuts, but that's a packaging
//     concern, not a backend one.
//
//   * **Input device / keyboard layout snapshotting.** A user who hot-swaps
//     to a different keymap mid-routine can hit unknown shortcuts. Future:
//     snapshot active layout at routine start, restore at end.
//
//   * **Audio routing.** Currently relies on PipeWire being up. If it
//     isn't, ambient music silently fails. Future: surface a status chip.
//
// The above are tracked in code rather than DECISIONS.md so they stay in
// front of whoever is editing this file next.

#include "platform/linux/LinuxBackend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>

namespace {

constexpr const char *kKWinService = "org.kde.KWin";
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
    // pkill -x matches against the 15-char comm field by default, so long
    // process names like "polkit-kde-authentication-agent-1" produce a
    // warning and silently match nothing. Switch to -f for any name that
    // doesn't fit. We anchor to the command start so /foo/bar doesn't match
    // /foo/bar2.
    if (processName.size() <= 15) {
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), processName});
        return;
    }
    QProcess::execute(QStringLiteral("pkill"), {
        QStringLiteral("-f"),
        QStringLiteral("^%1($| )").arg(QRegularExpression::escape(processName))
    });
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
// We talk to KWin through tiny `qdbus6` subprocesses only. The previous Qt 6 /
// KWin 6 combination aborted the process inside libdbus during a few KWin calls:
//
//   QDBusArgument: write from a read-only object
//   dbus[…]: type struct 114 not a basic type
//   Aborted (core dumped)
//
// Crashing the routine engage also left the nftables policy installed, which
// stranded the user's wifi until reboot. The extra process spawn is well worth
// it for a strict daily-driver shell. Do not add QDBusInterface calls back to
// this file unless the crash is proven fixed on the deployed Fedora/KWin stack.
//
// FUTURE: when we own the compositor, virtual-desktop bookkeeping moves
// in-process and these DBus dances go away entirely.

QString findQdbusBinary()
{
    return firstExecutable({
        QStringLiteral("qdbus6"),
        QStringLiteral("qdbus-qt6"),
        QStringLiteral("qdbus")
    });
}

QString runQdbusShellOutput(const QStringList &args, int timeoutMs = 1500, bool *ok = nullptr)
{
    if (ok) {
        *ok = false;
    }
    const QString qdbus = findQdbusBinary();
    if (qdbus.isEmpty()) {
        return {};
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(qdbus, args);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(50);
        return {};
    }
    const bool success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok) {
        *ok = success;
    }
    return QString::fromUtf8(process.readAll()).trimmed();
}

bool runQdbusShell(const QStringList &args, int timeoutMs = 1500)
{
    bool ok = false;
    runQdbusShellOutput(args, timeoutMs, &ok);
    return ok;
}

// ─── KWin scripting: window-to-desktop pinning ──────────────────────────────
//
// KWin assigns newly-created windows to the desktop that is current at the
// moment the window appears, but a Chromium / Electron app with an existing
// session opens its new window on whatever desktop the existing process owns.
// That's why the user kept seeing routine apps spawn on the wrong desktop
// and having to drag them by hand. We patch around this for the lifetime of
// the routine by loading a small KWin script that captures every window
// `windowAdded` and pins it to the focus desktop.
//
// The script is referenced by an integer handle; we keep it around so
// `stop()` + `unloadScript()` can run when the routine ends.

int kwinScriptingLoad(const QString &scriptBody, const QString &pluginName)
{
    QTemporaryFile scriptFile(QDir::tempPath() + QStringLiteral("/focusos-kwin-XXXXXX.js"));
    scriptFile.setAutoRemove(false);
    if (!scriptFile.open()) {
        return -1;
    }
    scriptFile.write(scriptBody.toUtf8());
    const QString scriptPath = scriptFile.fileName();
    scriptFile.close();

    bool loaded = false;
    const QString output = runQdbusShellOutput({
        QString::fromLatin1(kKWinService),
        QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting.loadScript"),
        scriptPath,
        pluginName
    }, 2500, &loaded);
    QFile::remove(scriptPath);
    if (!loaded) {
        return -1;
    }
    const QRegularExpression numberPattern(QStringLiteral("(-?\\d+)"));
    const QRegularExpressionMatch match = numberPattern.match(output);
    const int handle = match.hasMatch() ? match.captured(1).toInt() : -1;
    if (handle < 0) {
        return -1;
    }
    // Each script lives at /Scripting/Script<handle> and has its own start().
    runQdbusShell({
        QString::fromLatin1(kKWinService),
        QStringLiteral("/Scripting/Script%1").arg(handle),
        QStringLiteral("org.kde.kwin.Script.run")
    }, 1500);
    return handle;
}

void kwinScriptingUnload(int handle, const QString &pluginName)
{
    if (handle < 0) {
        return;
    }
    runQdbusShell({
        QString::fromLatin1(kKWinService),
        QStringLiteral("/Scripting/Script%1").arg(handle),
        QStringLiteral("org.kde.kwin.Script.stop")
    }, 1000);
    runQdbusShell({
        QString::fromLatin1(kKWinService),
        QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting.unloadScript"),
        pluginName
    }, 1000);
}

void kwinRunOneShotScript(const QString &scriptBody, const QString &pluginPrefix)
{
    const QString pluginName = QStringLiteral("%1-%2").arg(pluginPrefix).arg(QDateTime::currentMSecsSinceEpoch());
    const int handle = kwinScriptingLoad(scriptBody, pluginName);
    if (handle < 0) {
        return;
    }
    QThread::msleep(120);
    kwinScriptingUnload(handle, pluginName);
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
    unloadWindowPinScript();
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

    // Pin every newly-created window to the focus desktop for the duration
    // of the routine. Without this, Chromium/Electron apps with an existing
    // background process open their windows on whatever desktop the parent
    // process is on. The script also creates/switches to the Focus desktop,
    // avoiding fragile VirtualDesktopManager DBus marshalling entirely.
    loadWindowPinScript();

    // KWin script execution is async relative to qdbus returning. Give the
    // compositor a small settle window before launching routine apps.
    QThread::msleep(280);

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
    unloadWindowPinScript();

    // Kill anything we tracked from launchApps/openUrls first — this catches
    // browser windows that opened on free PIDs we wouldn't otherwise reach.
    killTrackedPids(m_sessionPids);

    // Build the always-allowed allowlist so we don't accidentally pkill an
    // app the user pinned. pkill -f matches against the full command line,
    // so a partial match on a routine path could brush against an
    // always-allowed editor running with a similar invocation.
    const QStringList alwaysAllowed = alwaysAllowedProcessNames();

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
        const QString candidateName = info.fileName();
        if (alwaysAllowed.contains(candidateName)) {
            // The routine listed an always-allowed app explicitly. Leave it
            // running so the user keeps their editor/calendar between
            // routines.
            continue;
        }

        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            const QStringList parts = desktopExecParts(candidate);
            if (!parts.isEmpty()) {
                const QString basename = QFileInfo(parts.first()).fileName();
                if (!alwaysAllowed.contains(basename)) {
                    QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), basename});
                }
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
    unloadWindowPinScript();

    // The temporary access timer fires this. We want to take the user back
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

    // The desktop shell goes on the same Focus desktop so the user can inspect
    // the real desktop only inside the authorized access window.
    kwinRunOneShotScript(QStringLiteral(R"(
        try {
            var desktops = workspace.desktops || [];
            while (desktops.length < 2 && workspace.createDesktop) {
                workspace.createDesktop(desktops.length, "Focus 2");
                desktops = workspace.desktops || desktops;
            }
            if (desktops.length >= 2) {
                workspace.currentDesktop = desktops[1];
            }
        } catch (e) {}
    )"), QStringLiteral("focusos-access-desktop"));

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
    kwinRunOneShotScript(QStringLiteral(R"(
        try {
            var desktops = workspace.desktops || [];
            if (desktops.length > 0) {
                workspace.currentDesktop = desktops[0];
            }
        } catch (e) {}
    )"), QStringLiteral("focusos-restore-desktop"));
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
    //
    // This is the strict-lockdown surface — the only legitimate way out of
    // the routine should be the FocusOS shell itself (TOTP unlock) or
    // logging out via SDDM. Anything that pops a launcher or spawns a
    // taskbar goes back here.
    //
    // FUTURE: extend this to an allowlist sweep — enumerate processes via
    // /proc, and kill anything user-owned that isn't:
    //   - focusos itself
    //   - kwin_wayland / xdg-desktop-portal (compositor + portal)
    //   - routine apps (tracked PIDs)
    //   - always-allowed apps (m_alwaysAllowedCommandLines)
    // We don't do that yet because misclassifying a session-critical helper
    // would log the user out. The current deny-list is a safer first pass.
    static const QStringList outlawed {
        // Launcher / spotlight surfaces
        QStringLiteral("krunner"),
        QStringLiteral("plasmashell"),
        QStringLiteral("kickoff"),
        QStringLiteral("rofi"),
        QStringLiteral("dmenu"),
        QStringLiteral("wofi"),
        QStringLiteral("fuzzel"),
        QStringLiteral("synapse"),
        QStringLiteral("ulauncher"),
        QStringLiteral("albert"),
        QStringLiteral("kmenuedit"),
        QStringLiteral("plasmawindowed"),
        // System trays / panels that could surface app shortcuts
        QStringLiteral("waybar"),
        QStringLiteral("polybar"),
        QStringLiteral("xfce4-panel"),
        QStringLiteral("mate-panel"),
        // Activity / overview surfaces
        QStringLiteral("kactivitymanagerd"),
        // File managers and system-control surfaces. These are powerful
        // escape hatches during a routine because they can launch arbitrary
        // .desktop files, open terminals, mount drives, or change policy.
        QStringLiteral("dolphin"),
        QStringLiteral("nautilus"),
        QStringLiteral("nemo"),
        QStringLiteral("thunar"),
        QStringLiteral("pcmanfm"),
        QStringLiteral("caja"),
        QStringLiteral("systemsettings"),
        QStringLiteral("plasma-discover"),
        QStringLiteral("gnome-software"),
        QStringLiteral("apper"),
        QStringLiteral("muon"),
        QStringLiteral("plasma-systemmonitor"),
        QStringLiteral("gnome-system-monitor"),
        QStringLiteral("ksysguard"),
        QStringLiteral("missioncenter"),
        // Common time-sinks (kept here even if the network lock blocks the
        // backend — the app surface is still a temptation)
        QStringLiteral("discord"),
        QStringLiteral("slack"),
        QStringLiteral("telegram-desktop"),
        QStringLiteral("Signal"),
        QStringLiteral("steam"),
        QStringLiteral("spotify")
    };
    const QStringList alwaysAllowed = alwaysAllowedProcessNames();
    for (const QString &name : outlawed) {
        if (alwaysAllowed.contains(name)) {
            continue;
        }
        pkillExact(name);
    }
}

QStringList LinuxBackend::alwaysAllowedProcessNames() const
{
    QStringList names;
    names.reserve(m_alwaysAllowedCommandLines.size());
    for (const QString &entry : m_alwaysAllowedCommandLines) {
        const QString trimmed = entry.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QStringList parts = QProcess::splitCommand(trimmed);
        if (parts.isEmpty()) {
            continue;
        }
        QString first = parts.first();
        const QFileInfo info(first);
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            const QStringList execParts = desktopExecParts(first);
            if (!execParts.isEmpty()) {
                names.append(QFileInfo(execParts.first()).fileName());
            }
            continue;
        }
        names.append(QFileInfo(first).fileName());
    }
    return names;
}

void LinuxBackend::setAlwaysAllowedApps(const QStringList &commandLines)
{
    m_alwaysAllowedCommandLines = commandLines;
}

void LinuxBackend::loadWindowPinScript()
{
    if (m_windowPinScriptHandle >= 0) {
        // Existing script from a previous routine that wasn't cleaned up —
        // tear it down before installing the new one so we don't end up
        // with two scripts double-handling windowAdded.
        unloadWindowPinScript();
    }

    // KWin 6 scripting API: workspace.windowAdded fires for every new
    // toplevel. The script creates/switches to desktop 2, stores that desktop
    // object, then pins each new non-transient window there. This deliberately
    // avoids Qt DBus property reads/writes, which are what crashed Fedora.
    const QString scriptBody = QStringLiteral(R"(
        var focusosDesktop = null;

        function focusosEnsureDesktop() {
            try {
                var desktops = workspace.desktops || [];
                while (desktops.length < 2 && workspace.createDesktop) {
                    workspace.createDesktop(desktops.length, "Focus 2");
                    desktops = workspace.desktops || desktops;
                }
                focusosDesktop = desktops.length >= 2 ? desktops[1] : workspace.currentDesktop;
                if (focusosDesktop) {
                    workspace.currentDesktop = focusosDesktop;
                }
            } catch (e) {
                focusosDesktop = workspace.currentDesktop;
            }
        }

        function focusosPinNewWindow(window) {
            if (!window) return;
            if (window.transient) return;
            try {
                if (!focusosDesktop) focusosEnsureDesktop();
                window.desktops = [focusosDesktop || workspace.currentDesktop];
            } catch (e) { /* KWin <6.0 didn't expose .desktops */ }
        }

        focusosEnsureDesktop();
        workspace.windowAdded.connect(focusosPinNewWindow);
    )");
    m_windowPinScriptPlugin = QStringLiteral("focusos-window-pin-%1")
                                  .arg(QDateTime::currentMSecsSinceEpoch());
    m_windowPinScriptHandle = kwinScriptingLoad(scriptBody, m_windowPinScriptPlugin);
}

void LinuxBackend::unloadWindowPinScript()
{
    if (m_windowPinScriptHandle < 0) {
        return;
    }
    kwinScriptingUnload(m_windowPinScriptHandle, m_windowPinScriptPlugin);
    m_windowPinScriptHandle = -1;
    m_windowPinScriptPlugin.clear();
}
