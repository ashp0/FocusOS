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

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
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

// A routine should open a *fresh* browser window — not a restored pile of
// months-old tabs that won't even load behind the network lock. For a
// chromium-family browser launched as a routine app we force a brand-new
// window and suppress the "restore previous session" / crash-restore prompts.
// We deliberately keep the user's real profile (logged-in accounts, etc.); we
// just don't want the old tab clutter. Kiosk launches (--app) are left alone.
QStringList chromiumFreshWindowGuard(const QString &program, const QStringList &arguments)
{
    if (!isChromiumFamily(program)) {
        return arguments;
    }
    for (const QString &arg : arguments) {
        if (arg.startsWith(QStringLiteral("--app")) ||
            arg == QStringLiteral("--new-window")) {
            return arguments;
        }
    }
    QStringList augmented = arguments;
    augmented.prepend(QStringLiteral("--no-first-run"));
    augmented.prepend(QStringLiteral("--hide-crash-restore-bubble"));
    augmented.prepend(QStringLiteral("--new-window"));
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
    arguments = chromiumFreshWindowGuard(program, arguments);

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

    // Everything runs on the user's current desktop now — FocusOS no longer
    // spins up a separate "Focus" virtual desktop or pins routine windows to
    // it. The user full-screens / tiles routine apps themselves, so a second
    // desktop was just churn (and an extra qdbus/KWin-scripting surface).

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
    QStringList normalizedUrls;
    for (const QString &url : urls) {
        const QString trimmed = url.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        normalizedUrls.append(QUrl::fromUserInput(trimmed).toString());
    }
    if (normalizedUrls.isEmpty()) {
        return true;
    }

    // Prefer launching a chromium-family browser directly so the allowed URLs
    // land in ONE fresh window (no restored old tabs). xdg-open would reuse an
    // existing browser window and bury the routine sites among stale tabs.
    const QString browser = firstExecutable({
        QStringLiteral("brave-browser"),
        QStringLiteral("brave"),
        QStringLiteral("google-chrome-stable"),
        QStringLiteral("google-chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("chromium-browser"),
        QStringLiteral("microsoft-edge-stable"),
        QStringLiteral("microsoft-edge"),
        QStringLiteral("vivaldi-stable"),
        QStringLiteral("vivaldi")
    });
    if (!browser.isEmpty()) {
        QStringList parts;
        parts << browser << QStringLiteral("--new-window");
        parts.append(normalizedUrls);
        qint64 pid = 0;
        if (launchCommand(parts, &pid)) {
            if (pid > 0) {
                m_sessionPids.append(pid);
            }
            return true;
        }
        // Fall through to xdg-open if the direct launch failed.
    }

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

    for (const QString &normalized : normalizedUrls) {
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

    // The desktop shell comes up on the user's current desktop — there's no
    // separate Focus desktop to switch to any more.
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
    // No-op now that FocusOS lives on the user's single desktop. There's no
    // "home" workspace to switch back to; ShellWindow handles raising the
    // shell back to the foreground when a routine ends.
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
        QStringLiteral("kupfer"),
        QStringLiteral("cerebro"),
        QStringLiteral("kmenuedit"),
        QStringLiteral("plasmawindowed"),
        // Drop-down terminals — a single keypress drops a shell on top of any
        // routine, so they're a direct escape hatch.
        QStringLiteral("yakuake"),
        QStringLiteral("guake"),
        QStringLiteral("tilda"),
        // System trays / panels / docks that could surface app shortcuts
        QStringLiteral("waybar"),
        QStringLiteral("polybar"),
        QStringLiteral("xfce4-panel"),
        QStringLiteral("mate-panel"),
        QStringLiteral("plank"),
        QStringLiteral("latte-dock"),
        QStringLiteral("cairo-dock"),
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

bool LinuxBackend::restoreLoginSessions(QString *errorMessage)
{
    // The recovery script is installed by install.sh and granted a scoped,
    // passwordless sudoers entry. We invoke it via sudo -n (non-interactive):
    // if the install didn't set up the sudoers rule, this fails cleanly rather
    // than blocking on a password prompt the locked-down session can't answer.
    const QStringList candidates {
        QStringLiteral("/usr/local/lib/focusos/focusos-restore-sessions.sh"),
        QStringLiteral("/opt/focusos/lib/focusos-restore-sessions.sh"),
    };
    QString script;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            script = candidate;
            break;
        }
    }
    if (script.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Recovery is only available on a permanent install (run install.sh).");
        }
        return false;
    }

    const QString sudo = QStandardPaths::findExecutable(QStringLiteral("sudo"));
    if (sudo.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("sudo not found — cannot restore login sessions.");
        }
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(sudo, {QStringLiteral("-n"), script});
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(200);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Recovery timed out.");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            const QString output = QString::fromUtf8(process.readAll()).trimmed();
            *errorMessage = output.isEmpty()
                ? QStringLiteral("Recovery script failed (exit %1).").arg(process.exitCode())
                : output;
        }
        return false;
    }
    return true;
}

QString LinuxBackend::watchdogScriptPath() const
{
    // Prefer an installed copy; fall back to the in-repo packaging script so
    // a dev build (running out of <repo>/build/) gets the watchdog too.
    const QStringList candidates {
        QStringLiteral("/usr/local/lib/focusos/focusos-watchdog.sh"),
        QStringLiteral("/opt/focusos/lib/focusos-watchdog.sh"),
        QStringLiteral("/usr/local/bin/focusos-watchdog"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    // Dev fallback: <repo>/build/focusos → <repo>/packaging/linux/...
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    const QString repoScript = dir.absoluteFilePath(
        QStringLiteral("packaging/linux/focusos-watchdog.sh"));
    if (QFileInfo::exists(repoScript)) {
        return repoScript;
    }
    return {};
}

void LinuxBackend::startWatchdog(const QString &binaryPath)
{
    const QString focusDir = QDir::homePath() + QStringLiteral("/.focusos");
    QDir().mkpath(focusDir);

    // Record the binary the watchdog should respawn. The watchdog reads this
    // when its --binary arg is absent (e.g. the kiosk session command).
    if (!binaryPath.isEmpty()) {
        QFile binaryFile(focusDir + QStringLiteral("/watchdog-binary"));
        if (binaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            binaryFile.write(binaryPath.toUtf8());
            binaryFile.write("\n");
            binaryFile.close();
        }
    }

    const QString script = watchdogScriptPath();
    if (script.isEmpty()) {
        return;
    }

    // If a watchdog is already supervising us, the flock in the script makes a
    // second launch a harmless no-op (the new instance can't take the lock and
    // exits). Skip when we can already see one to avoid the churn. The script
    // runs as `bash …/focusos-watchdog.sh`, so match the full command line.
    {
        const QString pgrep = QStandardPaths::findExecutable(QStringLiteral("pgrep"));
        if (!pgrep.isEmpty()) {
            QProcess probe;
            probe.start(pgrep, {QStringLiteral("-f"), QStringLiteral("focusos-watchdog")});
            if (probe.waitForFinished(300) &&
                probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
                return;
            }
        }
    }

    const QString shell = firstExecutable({QStringLiteral("bash"), QStringLiteral("sh")});
    if (shell.isEmpty()) {
        return;
    }

    QStringList args { script };
    if (!binaryPath.isEmpty()) {
        args << QStringLiteral("--binary") << binaryPath;
    }
    QProcess::startDetached(shell, args);
}
