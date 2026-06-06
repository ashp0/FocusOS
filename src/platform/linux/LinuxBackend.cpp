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

#include "blocker/BlockerPolicy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <csignal>
#include <unistd.h>

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

// Document file types that get a dedicated viewer in launchFile(). For these we
// prefer a concrete reader (more reliable in the bare kiosk session than the
// xdg-open portal path); all other file types just go straight to xdg-open.
bool isDocumentSuffix(const QString &suffix)
{
    static const QStringList documentSuffixes {
        QStringLiteral("pdf"),  QStringLiteral("epub"), QStringLiteral("mobi"),
        QStringLiteral("azw3"), QStringLiteral("djvu"), QStringLiteral("fb2"),
        QStringLiteral("cbz"),  QStringLiteral("cbr"),  QStringLiteral("chm")
    };
    return documentSuffixes.contains(suffix.toLower());
}

// Open an arbitrary data file in its default application. For known document
// types (PDF / ebook) we prefer a concrete viewer so the file lands in a real
// window even in the bare kiosk session (where xdg-open's portal path can be
// flaky); everything else (images, office docs, video, …) is handed to xdg-open
// so it opens in whatever the user has set as default.
bool launchFile(const QString &path, const QStringList &extraArgs, qint64 *pidOut = nullptr)
{
    if (isDocumentSuffix(QFileInfo(path).suffix())) {
        const QString reader = firstExecutable({
            QStringLiteral("okular"),
            QStringLiteral("ebook-viewer"),   // Calibre's reader — handles epub/mobi/azw3
            QStringLiteral("evince"),
            QStringLiteral("atril"),
            QStringLiteral("zathura"),
            QStringLiteral("qpdfview"),
            QStringLiteral("xreader"),
            QStringLiteral("mupdf"),
            QStringLiteral("xpdf")
        });
        if (!reader.isEmpty()) {
            QStringList parts;
            parts << reader << path;
            parts.append(extraArgs);
            return launchCommand(parts, pidOut);
        }
    }

    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (!xdgOpen.isEmpty()) {
        return launchCommand({xdgOpen, path}, pidOut);
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

void seedUserServiceEnvironment()
{
    // systemd --user services inherit the environment of the *systemd user
    // manager*, NOT of whoever runs `systemctl --user start`. On a normal
    // Plasma/GNOME login the session leader seeds that manager (and the D-Bus
    // activation environment) with WAYLAND_DISPLAY / XDG_CURRENT_DESKTOP / … via
    // `dbus-update-activation-environment --systemd`. Our bare kwin_wayland
    // session never does that, so any user service launched from startup.sh —
    // e.g. Toshy's toshy-<compositor>-dbus.service, which connects to KWin over
    // the session bus to learn the focused window — comes up with no Wayland
    // display and no desktop identity and exits non-zero. `toshy-services-restart`
    // then reports every toshy-*-dbus.service as failed. Seed the manager once,
    // before we run any startup item, so those services start with the same
    // canonical KDE identity FocusOS hands its own children in
    // startDetachedWithKdeEnvironment().
    const QProcessEnvironment sys = QProcessEnvironment::systemEnvironment();
    QStringList assignments {
        QStringLiteral("XDG_CURRENT_DESKTOP=KDE"),
        QStringLiteral("XDG_SESSION_DESKTOP=KDE"),
        QStringLiteral("XDG_SESSION_TYPE=wayland"),
        QStringLiteral("KDE_FULL_SESSION=true"),
        QStringLiteral("KDE_SESSION_VERSION=6"),
    };
    // Pass the live per-session vars through verbatim if FocusOS has them (as a
    // Wayland client it will): these are what actually let a user service reach
    // the compositor.
    for (const QString &key : {QStringLiteral("WAYLAND_DISPLAY"),
                               QStringLiteral("DISPLAY"),
                               QStringLiteral("XDG_RUNTIME_DIR")}) {
        const QString value = sys.value(key);
        if (!value.isEmpty()) {
            assignments << (key + QLatin1Char('=') + value);
        }
    }

    // dbus-update-activation-environment --systemd pushes into BOTH the D-Bus
    // activation environment and the systemd --user manager in one shot. Run it
    // synchronously so the seed lands before startup.sh fires toshy-services-restart.
    const QString dbusUpdate =
        QStandardPaths::findExecutable(QStringLiteral("dbus-update-activation-environment"));
    if (!dbusUpdate.isEmpty()) {
        QProcess::execute(dbusUpdate, QStringList{QStringLiteral("--systemd")} + assignments);
    }
    // Belt-and-braces for installs without dbus-x11's helper: set the same vars
    // directly on the user manager too.
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (!systemctl.isEmpty()) {
        QProcess::execute(
            systemctl,
            QStringList{QStringLiteral("--user"), QStringLiteral("set-environment")} + assignments);
    }
}

void pkillExact(const QString &processName)
{
    // pkill -x matches against the 15-char comm field by default, so long
    // process names like "polkit-kde-authentication-agent-1" produce a
    // warning and silently match nothing. Switch to -f for any name that
    // doesn't fit. We anchor to the command start so /foo/bar doesn't match
    // /foo/bar2.
    //
    // startDetached, NOT execute: pkillExact is fire-and-forget (no caller
    // reads the result), and the lockdown watchdog calls it on the GUI thread.
    // QProcess::execute blocks until pkill exits, so a sweep of ~45 names froze
    // the Qt event loop for a fraction of a second every tick — stalling the
    // clock, the countdown, the starfield, and notes input. Detaching keeps the
    // event loop free.
    if (processName.size() <= 15) {
        QProcess::startDetached(QStringLiteral("pkill"), {QStringLiteral("-x"), processName});
        return;
    }
    QProcess::startDetached(QStringLiteral("pkill"), {
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

    // Special-character-safe path handling. Routine entries are shell-quoted
    // command strings, but the file/app pickers hand back bare filesystem paths
    // that aren't quoted. QProcess::splitCommand would then shatter a name with
    // spaces or other shell-significant characters — "…/My File (v2).pdf"
    // becomes ["…/My", "File", "(v2).pdf"] — and the launcher fails with
    // "Unable to launch …/My". So if the whole entry resolves to an existing
    // path, treat it as one argument-less target. Bare commands like
    // "flatpak run md.obsidian.Obsidian" don't exist as a file, so they still
    // fall through to the splitCommand path that supports arguments.
    const QString expandedWhole = expandedPath(trimmed);
    const QFileInfo wholeInfo(expandedWhole);
    if (wholeInfo.isAbsolute() && wholeInfo.exists()) {
        parsed.path = expandedWhole;
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

QStringList processNamesForCommandLines(const QStringList &entries)
{
    QStringList names;
    for (const QString &entry : entries) {
        const ParsedAppEntry parsed = parseAppEntry(entry);
        if (parsed.kiosk || parsed.path.isEmpty()) {
            continue;
        }

        const QFileInfo info(parsed.path);
        if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
            const QStringList parts = desktopExecParts(parsed.path);
            if (!parts.isEmpty()) {
                names.append(QFileInfo(parts.first()).fileName());
            }
            continue;
        }

        const QString name = QFileInfo(parsed.path).fileName();
        if (!name.isEmpty()) {
            names.append(name);
        }

        if (name == QStringLiteral("x-terminal-emulator")) {
            names << QStringLiteral("konsole")
                  << QStringLiteral("kgx")
                  << QStringLiteral("gnome-terminal")
                  << QStringLiteral("foot")
                  << QStringLiteral("alacritty")
                  << QStringLiteral("xterm");
        }
    }
    names.removeDuplicates();
    return names;
}

// A systemd *user service* (its cgroup leaf unit ends in ".service") is a
// background daemon — input remappers (Toshy / keyd / xremap), tray agents,
// sync clients, notification helpers, etc. — not a user-launched GUI window.
// Apps the user opens from a launcher land in a transient "app-*.scope"
// instead. We never mass-quit services when a routine engages: killing e.g. a
// keyboard remapper mid-routine would break the user's typing. This is
// deliberately versatile — nothing is matched by name, it keys off *how* the
// process was started, so any well-behaved background utility is spared.
bool isSystemdUserService(qint64 pid)
{
    QFile cgroupFile(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (!cgroupFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QString content = QString::fromUtf8(cgroupFile.readAll());
    const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString leaf = line.section(QLatin1Char('/'), -1);
        if (leaf.endsWith(QStringLiteral(".service"))) {
            return true;
        }
    }
    return false;
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
    m_sessionAllowedProcessNames = processNamesForCommandLines(appPaths);

    QProcess::execute(
        QStringLiteral("pkill"),
        {
            QStringLiteral("-f"),
            QStringLiteral("x-terminal-emulator")
        }
    );

    terminateDesktopShell();

    // Strict enforcement (Task 1): close the user's other running GUI apps so a
    // routine starts from a clean surface — nothing but the routine's own apps
    // and the always-allowed list. The QML side gives the user a short warning
    // before this fires so unsaved work can be saved.
    quitBackgroundApps(appPaths);

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
        } else if (info.isFile() && !info.isExecutable()) {
            // A data file the user added via "Open File" (PDF, ebook, image,
            // office doc, video…). Hand it to its default application instead of
            // trying to exec the file itself. Bare command names like
            // "flatpak run …" don't exist as files, so they fall through to exec.
            launched = launchFile(parsed.path, parsed.args, &pid);
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
    m_sessionAllowedProcessNames.clear();
}

bool LinuxBackend::hasLiveRoutineApps() const
{
    // The routine's apps are exactly the PIDs we tracked from launchApps /
    // openUrls. kill(pid, 0) probes for liveness without sending a signal: 0 means
    // the process still exists (or is a zombie we can signal), ESRCH means it's
    // gone. One survivor is enough to say "the app the user was in is still open".
    for (qint64 pid : m_sessionPids) {
        if (pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0) {
            return true;
        }
    }
    return false;
}

void LinuxBackend::endRoutineLockdown()
{
    // Stand down the launcher-killing sweep without touching the routine's apps
    // (they stay open for the finish prompt / admin desktop). The extension
    // presence watchdog keeps running on its own if a network lock is still live.
    stopLockdownWatchdog();
    m_sessionAllowedProcessNames.clear();
}

QSet<QString> LinuxBackend::criticalKeepSet() const
{
    return {
        // FocusOS + its session scaffolding
        QStringLiteral("focusos"),
        QStringLiteral("focusos-session"),
        QStringLiteral("focusos-watchdog.sh"),
        QStringLiteral("bash"), QStringLiteral("sh"), QStringLiteral("zsh"),
        // Compositor + display server
        QStringLiteral("kwin_wayland"), QStringLiteral("kwin_wayland_wrapper"),
        QStringLiteral("kwin"), QStringLiteral("Xwayland"), QStringLiteral("xwayland"),
        // Desktop portals (file pickers, screenshots — routine apps need them)
        QStringLiteral("xdg-desktop-portal"),
        QStringLiteral("xdg-desktop-portal-kde"),
        QStringLiteral("xdg-desktop-portal-gtk"),
        QStringLiteral("xdg-desktop-por"), // /proc comm is truncated to 15 chars
        QStringLiteral("xdg-document-portal"), QStringLiteral("xdg-permission-store"),
        // Audio stack
        QStringLiteral("pipewire"), QStringLiteral("pipewire-pulse"),
        QStringLiteral("wireplumber"), QStringLiteral("pulseaudio"),
        // Bus + login session
        QStringLiteral("dbus-daemon"), QStringLiteral("dbus-broker"),
        QStringLiteral("dbus-run-session"),
        QStringLiteral("systemd"), QStringLiteral("systemd-logind"),
        // KDE global-shortcut / kded helpers FocusOS relies on for media keys
        QStringLiteral("kglobalacceld"),
        QStringLiteral("kglobalaccel5"), QStringLiteral("kglobalaccel6"),
        QStringLiteral("kded5"), QStringLiteral("kded6"),
        QStringLiteral("polkit-kde-authentication-agent-1"), QStringLiteral("polkitd"),
        // Misc session agents that hold credentials / mounts
        QStringLiteral("gvfsd"), QStringLiteral("gvfsd-fuse"),
        QStringLiteral("ssh-agent"), QStringLiteral("gpg-agent")
    };
}

void LinuxBackend::freezeBackgroundProcesses()
{
    // macOS-style "sleep": SIGSTOP every graphical app the user has left running
    // so the CPU parks at idle while the machine sleeps, WITHOUT a kernel suspend
    // (which is what black-screens some hardware on wake). Paired with DPMS-off +
    // paused music this gets near-suspend power draw with an instant, risk-free
    // wake — any input thaws the apps again (thawBackgroundProcesses()).
    //
    // This only ever runs on the home screen: deep-idle is suppressed during a
    // routine, so we never freeze the routine's own apps mid-session. The same
    // conservative keep-set as the engage-time app sweep protects the compositor,
    // audio, dbus, portals, the global-shortcut daemon and FocusOS itself —
    // freezing any of those would wedge the session or the wake path.
    if (!m_frozenPids.isEmpty()) {
        return; // already frozen — don't double-stop
    }
    const QSet<QString> keep = criticalKeepSet();
    const uid_t myUid = ::getuid();
    const qint64 myPid = QCoreApplication::applicationPid();

    QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid || pid <= 1 || pid == myPid) {
            continue;
        }

        const QString base = QStringLiteral("/proc/") + entry;
        if (QFileInfo(base).ownerId() != myUid) {
            continue; // only touch our own processes
        }

        QString name = QFileInfo(QFile::symLinkTarget(base + QStringLiteral("/exe"))).fileName();
        if (name.isEmpty()) {
            QFile commFile(base + QStringLiteral("/comm"));
            if (commFile.open(QIODevice::ReadOnly)) {
                name = QString::fromUtf8(commFile.readAll()).trimmed();
            }
        }
        if (name.isEmpty() || keep.contains(name)) {
            continue;
        }

        // Spare background system utilities (a systemd user service is a daemon,
        // not a window the user opened) — same heuristic as the app sweep.
        if (isSystemdUserService(pid)) {
            continue;
        }

        // Only freeze graphical clients (WAYLAND_DISPLAY / DISPLAY in environ);
        // leaving pure daemons running is the safe default.
        QFile environFile(base + QStringLiteral("/environ"));
        if (!environFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray environ = environFile.readAll();
        const bool graphical = environ.contains("WAYLAND_DISPLAY=") ||
                               environ.contains("DISPLAY=");
        if (!graphical) {
            continue;
        }

        if (::kill(static_cast<pid_t>(pid), SIGSTOP) == 0) {
            m_frozenPids.append(pid);
        }
    }
}

void LinuxBackend::thawBackgroundProcesses()
{
    // Resume exactly the PIDs we froze for the deep-idle sleep — never a blanket
    // SIGCONT — so we don't disturb anything the user (or a debugger) had
    // legitimately stopped. Idempotent: a no-op when nothing is frozen.
    for (const qint64 pid : m_frozenPids) {
        ::kill(static_cast<pid_t>(pid), SIGCONT);
    }
    m_frozenPids.clear();
}

QStringList LinuxBackend::sweepBackgroundApps(const QStringList &allowedCommandLines, bool dryRun)
{
    // Walk /proc and SIGTERM every GUI app the current user is running that
    // isn't part of the routine. "GUI app" = a process of ours whose environ
    // carries WAYLAND_DISPLAY / DISPLAY — that filters out the daemons and
    // session plumbing, so we only close visible apps. We deliberately use
    // SIGTERM (not SIGKILL) so apps can run their own save-on-quit handlers.
    //
    // Two layers of safety keep this from logging the user out:
    //   1. A hardcoded keep-set of session-critical processes (compositor,
    //      portal, audio, dbus, FocusOS itself, the session/watchdog scripts).
    //   2. The routine's apps + the always-allowed list, by process basename.
    QSet<QString> keep = criticalKeepSet();

    // Add the routine + always-allowed apps by process basename so we never
    // terminate something the user explicitly permitted.
    const auto addAllowed = [&keep](const QStringList &names) {
        for (const QString &name : names) {
            if (!name.isEmpty()) {
                keep.insert(name);
                // comm is capped at 15 chars — match the truncated form too.
                keep.insert(name.left(15));
            }
        }
    };
    addAllowed(processNamesForCommandLines(allowedCommandLines));
    addAllowed(m_sessionAllowedProcessNames);
    addAllowed(alwaysAllowedProcessNames());

    const uid_t myUid = ::getuid();
    const qint64 myPid = QCoreApplication::applicationPid();

    QStringList acted; // names SIGTERM'd (or, in dry-run, that would be)
    QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid || pid <= 1 || pid == myPid) {
            continue;
        }

        const QString base = QStringLiteral("/proc/") + entry;

        // Only touch our own processes.
        if (QFileInfo(base).ownerId() != myUid) {
            continue;
        }

        // Resolve the executable name (prefer the exe symlink; comm is
        // truncated and can be rewritten by the process).
        QString name = QFileInfo(QFile::symLinkTarget(base + QStringLiteral("/exe"))).fileName();
        if (name.isEmpty()) {
            QFile commFile(base + QStringLiteral("/comm"));
            if (commFile.open(QIODevice::ReadOnly)) {
                name = QString::fromUtf8(commFile.readAll()).trimmed();
            }
        }
        if (name.isEmpty() || keep.contains(name)) {
            continue;
        }

        // Spare background system utilities (Toshy and friends). A systemd user
        // service is a daemon, not a window the user opened — see the helper.
        if (isSystemdUserService(pid)) {
            continue;
        }

        // GUI heuristic: a graphical client has WAYLAND_DISPLAY or DISPLAY in
        // its environment. Pure daemons usually don't — leaving them alone is
        // the safe default.
        QFile environFile(base + QStringLiteral("/environ"));
        if (!environFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray environ = environFile.readAll();
        const bool graphical = environ.contains("WAYLAND_DISPLAY=") ||
                               environ.contains("DISPLAY=");
        if (!graphical) {
            continue;
        }

        acted.append(name);
        if (!dryRun) {
            ::kill(static_cast<pid_t>(pid), SIGTERM);
        }
    }

    acted.removeDuplicates();
    acted.sort();
    return acted;
}

void LinuxBackend::quitBackgroundApps(const QStringList &allowedCommandLines)
{
    const QStringList killed = sweepBackgroundApps(allowedCommandLines, /*dryRun=*/false);

    // Leave a record so the user can audit, after the fact, exactly which GUI
    // apps a strict engage closed — the keep-set is conservative but this is the
    // ground truth on their own machine. Best-effort; never blocks the engage.
    if (!killed.isEmpty()) {
        QFile log(QDir::homePath() + QStringLiteral("/.focusos/lockdown.log"));
        if (log.open(QIODevice::Append | QIODevice::Text)) {
            const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            log.write(QStringLiteral("%1 quitBackgroundApps SIGTERM: %2\n")
                          .arg(stamp, killed.join(QStringLiteral(", ")))
                          .toUtf8());
        }
    }
}

QStringList LinuxBackend::previewBackgroundAppQuit(const QStringList &allowedCommandLines)
{
    return sweepBackgroundApps(allowedCommandLines, /*dryRun=*/true);
}

void LinuxBackend::lockScreen()
{
    // Best-effort physical blank. The QML black overlay covers the rest, so any
    // single one of these succeeding is enough; failures are harmless.
    //
    // 1) loginctl lock-session — emits the login1 "Lock" signal (also what the
    //    power key triggers via 90-focusos-logind.conf).
    QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    // 2) DPMS off via kscreen-doctor (KDE) when available.
    const QString kscreen = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (!kscreen.isEmpty()) {
        QProcess::startDetached(kscreen, {QStringLiteral("--dpms"), QStringLiteral("off")});
    }
}

void LinuxBackend::unlockScreen()
{
    const QString kscreen = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (!kscreen.isEmpty()) {
        QProcess::startDetached(kscreen, {QStringLiteral("--dpms"), QStringLiteral("on")});
    }
}

void LinuxBackend::sleepDisplay()
{
    // Blank the panel without the in-app lock overlay or the m_screenLocked
    // state — the monitor wakes on the next keypress / mouse move. KDE/Wayland
    // flips DPMS via kscreen-doctor; fall back to xset on X11 sessions.
    const QString kscreen = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (!kscreen.isEmpty()) {
        QProcess::startDetached(kscreen, {QStringLiteral("--dpms"), QStringLiteral("off")});
        return;
    }
    const QString xset = QStandardPaths::findExecutable(QStringLiteral("xset"));
    if (!xset.isEmpty()) {
        QProcess::startDetached(xset, {QStringLiteral("dpms"), QStringLiteral("force"),
                                       QStringLiteral("off")});
    }
}

void LinuxBackend::wakeDisplay()
{
    // Force the panel back on. Most compositors wake DPMS on the next input on
    // their own, but after a deep-idle blank we flip it explicitly so the screen
    // is guaranteed lit when the user returns (or the machine resumes).
    const QString kscreen = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (!kscreen.isEmpty()) {
        QProcess::startDetached(kscreen, {QStringLiteral("--dpms"), QStringLiteral("on")});
        return;
    }
    const QString xset = QStandardPaths::findExecutable(QStringLiteral("xset"));
    if (!xset.isEmpty()) {
        QProcess::startDetached(xset, {QStringLiteral("dpms"), QStringLiteral("force"),
                                       QStringLiteral("on")});
    }
}

bool LinuxBackend::suspendSystem()
{
    // Opt-in whole-machine suspend for the deep-idle sleep. systemctl is the
    // portable path (delegates to logind, already configured for this session);
    // fall back to loginctl suspend. Detached + fire-and-forget — the kernel takes
    // over and FocusOS is frozen until the machine resumes. Best-effort: if
    // neither tool is present we return false and the caller stays in soft sleep.
    //
    // SAFETY: which sleep state the kernel enters is governed by the packaged
    // /etc/systemd/sleep.conf.d/90-focusos-sleep.conf drop-in, which pins
    // SuspendState to "freeze" (s2idle / "suspend-then-idle"). That avoids the S3
    // suspend-to-RAM path that black-screens some hardware (e.g. the 2017 iMac) on
    // wake. Without that drop-in installed this still issues a plain suspend, so
    // the safe default remains the process-freeze soft sleep, not this call.
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (!systemctl.isEmpty()) {
        return QProcess::startDetached(systemctl, {QStringLiteral("suspend")});
    }
    const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
    if (!loginctl.isEmpty()) {
        return QProcess::startDetached(loginctl, {QStringLiteral("suspend")});
    }
    return false;
}

// Shared tail for both the sync and async apply paths: load an already-resolved
// ruleset and, on success, arm the blocker policy + extension-presence watchdog.
// Runs on the main thread (touches LinuxBackend state + the watchdog timer).
bool LinuxBackend::commitNetworkPolicy(const QString &ruleset,
                                       const QStringList &allowedHosts,
                                       QString *errorMessage)
{
    // NetGate is the network-level backstop. Only arm the extension policy if it
    // succeeds — if the backstop can't apply the routine aborts, and writing an
    // active policy here would strand the browser behind a half-applied lock.
    if (!m_netGate.applyRuleset(ruleset, errorMessage)) {
        return false;
    }
    BlockerPolicy::write(true, allowedHosts);

    // Arm the extension-presence watchdog: remember the allowlist + the resolved
    // ruleset (so a post-clamp restore needn't re-resolve DNS), and start the
    // timer (independent of the app-lockdown sweep, which a network-only routine
    // might not run).
    m_activeAllowedHosts = allowedHosts;
    m_activeRuleset = ruleset;
    m_networkLockActive = true;
    m_extensionBanActive = false;
    m_extensionSeenAlive = false;
    m_extensionMissingSinceMs = 0;
    m_lastExtensionAlertMs = 0;
    ensureWatchdogTimer();
    return true;
}

bool LinuxBackend::applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage)
{
    // Synchronous path (DNS on the calling thread) — kept for callers where a
    // brief block is acceptable. The engage path uses applyNetworkPolicyAsync.
    return commitNetworkPolicy(m_netGate.buildRuleset(allowedHosts), allowedHosts, errorMessage);
}

void LinuxBackend::applyNetworkPolicyAsync(const QStringList &allowedHosts,
                                           std::function<void(bool, const QString &)> onComplete)
{
    // Resolve DNS + render the ruleset on a worker thread (the multi-second part
    // that froze the UI on engage), then hop back to the GUI thread to do the
    // fast privileged nft swap and arm the watchdogs.
    auto *watcher = new QFutureWatcher<QString>();
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, qApp,
                     [this, watcher, allowedHosts, onComplete = std::move(onComplete)]() {
                         const QString ruleset = watcher->result();
                         watcher->deleteLater();
                         QString error;
                         const bool ok = commitNetworkPolicy(ruleset, allowedHosts, &error);
                         onComplete(ok, error);
                     });
    watcher->setFuture(QtConcurrent::run([this, allowedHosts]() -> QString {
        return m_netGate.buildRuleset(allowedHosts);
    }));
}

void LinuxBackend::applyBrowserBlockerPolicy(const QStringList &allowedHosts)
{
    // Browser routine: enforce the allowlist inside the blocker extension only —
    // hand it the signed policy and leave nftables alone. The extension gates
    // navigations at the browser layer, so a system-wide egress block would be
    // redundant and would break allowed sites' off-host subresources.
    BlockerPolicy::write(true, allowedHosts);
}

void LinuxBackend::dropNetworkPolicy()
{
    m_networkLockActive = false;
    m_extensionBanActive = false;
    m_activeAllowedHosts.clear();
    m_activeRuleset.clear();
    maybeStopWatchdogTimer();
    BlockerPolicy::write(false, {});
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

void LinuxBackend::ensureWatchdogTimer()
{
    if (!m_lockdownTimer.isActive()) {
        m_lockdownTimer.start();
    }
}

void LinuxBackend::maybeStopWatchdogTimer()
{
    // The single timer drives both the app-lockdown sweep and the
    // extension-presence check; only stop it once neither needs it.
    if (!m_lockdownActive && !m_networkLockActive && m_lockdownTimer.isActive()) {
        m_lockdownTimer.stop();
    }
}

void LinuxBackend::startLockdownWatchdog()
{
    m_lockdownActive = true;
    ensureWatchdogTimer();
    // Fire once immediately so the first kill happens before the user can
    // open the spotlight.
    tickLockdownWatchdog();
}

void LinuxBackend::stopLockdownWatchdog()
{
    m_lockdownActive = false;
    maybeStopWatchdogTimer();
}

void LinuxBackend::tickLockdownWatchdog()
{
    if (m_networkLockActive) {
        enforceBlockerExtension();
    }
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
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("konsole"),
        QStringLiteral("kgx"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("foot"),
        QStringLiteral("alacritty"),
        QStringLiteral("xterm"),
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
    // Sweep the whole deny-list with just TWO pkill invocations instead of ~45.
    // pkill's pattern is an extended regex, so all the short names collapse into
    // one `-x` (exact-name) alternation and all the long names into one `-f`
    // (full-cmdline) alternation. This runs every 1.5s for the entire routine, so
    // dropping ~45 process spawns per tick to 2 is a real CPU/wakeup saving (it
    // matters on battery). We still fork a single detached `sh -c` so the GUI
    // thread never blocks. Every name below comes from the hard-coded `outlawed`
    // list (plain [a-zA-Z-] literals, regex-safe); user-supplied always-allowed
    // names are only ever used to *exclude* entries, so nothing untrusted is
    // interpolated into the pattern.
    QStringList allowedProcesses = alwaysAllowedProcessNames();
    allowedProcesses.append(m_sessionAllowedProcessNames);
    allowedProcesses.removeDuplicates();
    QStringList exactNames;   // matched against the (≤15-char) process name
    QStringList longNames;    // matched against the full command line
    for (const QString &name : outlawed) {
        if (allowedProcesses.contains(name)) {
            continue;
        }
        if (name.size() <= 15) {
            exactNames.append(name);
        } else {
            longNames.append(name);
        }
    }
    QStringList commands;
    if (!exactNames.isEmpty()) {
        // -x anchors the regex to the whole name, so the top-level alternation
        // matches a name that exactly equals any one of the listed processes.
        commands.append(QStringLiteral("pkill -x -- '%1'").arg(exactNames.join(QLatin1Char('|'))));
    }
    if (!longNames.isEmpty()) {
        // -f anchored to the command start (matches pkillExact's long-name path).
        commands.append(QStringLiteral("pkill -f -- '^(%1)($| )'").arg(longNames.join(QLatin1Char('|'))));
    }
    if (!commands.isEmpty()) {
        QProcess::startDetached(QStringLiteral("sh"),
                                {QStringLiteral("-c"), commands.join(QStringLiteral("; "))});
    }
}

// The native host rewrites ~/.focusos/blocker/host-alive every ~1.5s while a
// browser is connected to it. Treat the extension as alive if that beacon was
// touched within the last few seconds (a couple of missed ticks of slack).
bool LinuxBackend::blockerExtensionAlive() const
{
    const QFileInfo info(BlockerPolicy::heartbeatFilePath());
    if (!info.exists()) {
        return false;
    }
    const qint64 ageMs = info.lastModified().msecsTo(QDateTime::currentDateTime());
    return ageMs >= 0 && ageMs < 6000;
}

// True if a *user* Chromium-family browser is running. Reads /proc/<pid>/comm
// directly (cheap, no subprocess) — comm is truncated to 15 chars by the
// kernel, which isChromiumFamily's prefix match already tolerates.
//
// FocusOS's own kiosk browsers are excluded: they run on a throwaway
// `focusos-kiosk-*` profile with no extensions by design, so judging them
// "extension missing" and clamping the network would break the very site the
// routine pinned. We spot them (and their renderer children, which inherit the
// flag) by the --user-data-dir marker in /proc/<pid>/cmdline.
bool LinuxBackend::chromiumBrowserRunning() const
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool isPid = false;
        pid.toLongLong(&isPid);
        if (!isPid) {
            continue;
        }
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!comm.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString name = QString::fromUtf8(comm.readAll()).trimmed();
        if (name.isEmpty() || !isChromiumFamily(name)) {
            continue;
        }
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cmdline.open(QIODevice::ReadOnly)) {
            // cmdline args are NUL-separated; a plain contains() is enough.
            const QByteArray raw = cmdline.readAll();
            if (raw.contains("focusos-kiosk-")) {
                continue; // FocusOS kiosk browser — not a user-controlled browser.
            }
        }
        return true;
    }
    return false;
}

void LinuxBackend::enforceBlockerExtension()
{
    if (!m_networkLockActive) {
        return;
    }

    // Manual mute switch. If ~/.focusos/blocker/presence-check-off exists, skip
    // the extension-presence enforcement entirely (lift any active clamp) — an
    // escape hatch while the host/extension wiring is being debugged so a false
    // "extension missing" can't strand the user behind a full-deny + nag loop.
    if (QFileInfo::exists(BlockerPolicy::blockerDir() + QStringLiteral("/presence-check-off"))) {
        if (m_extensionBanActive) {
            QString error;
            m_netGate.applyRuleset(m_activeRuleset, &error);
            m_extensionBanActive = false;
        }
        m_extensionMissingSinceMs = 0;
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    const bool alive = blockerExtensionAlive();
    if (alive) {
        m_extensionSeenAlive = true;
    }

    // Don't arm the ban until the extension has proven it can connect this
    // session. A never-connecting extension means broken wiring (our problem),
    // not circumvention — clamping + nagging then just strands the user.
    if (!m_extensionSeenAlive) {
        m_extensionMissingSinceMs = 0;
        return;
    }

    // Only escalate when a *user* browser is actually open: a closed browser has
    // no extension to enable, and clamping then would needlessly strand allowed
    // non-browser apps. (FocusOS kiosk browsers are excluded — see the helper.)
    const bool missing = chromiumBrowserRunning() && !alive;

    if (!missing) {
        m_extensionMissingSinceMs = 0;
        if (m_extensionBanActive) {
            // Extension is back (or the browser closed) — restore the routine
            // allowlist so allowed sites work again.
            QString error;
            m_netGate.applyRuleset(m_activeRuleset, &error);
            m_extensionBanActive = false;
            m_lastExtensionAlertMs = 0;
        }
        return;
    }

    // Debounce: require the condition to hold continuously before clamping, so
    // browser/extension/native-host startup lag (session start, or the user
    // restarting their browser) doesn't trip a false ban.
    if (m_extensionMissingSinceMs == 0) {
        m_extensionMissingSinceMs = nowMs;
    }
    constexpr qint64 kMissingDebounceMs = 15000;
    if (!m_extensionBanActive && (nowMs - m_extensionMissingSinceMs) < kMissingDebounceMs) {
        return;
    }

    // Clamp once on entry (not every tick); keep the ban latched even if the
    // clamp call fails so we don't thrash nft or re-alert per tick.
    if (!m_extensionBanActive) {
        m_extensionBanActive = true;
        QString error;
        m_netGate.applyFullDeny(&error);
    }
    // Nag on entry and every 30s while clamped, so the user can't miss why
    // nothing loads. Rate-limited independently of the clamp.
    if (m_lastExtensionAlertMs == 0 || (nowMs - m_lastExtensionAlertMs) > 30000) {
        showExtensionDisabledAlert();
        m_lastExtensionAlertMs = nowMs;
    }
}

// Pop a real window the user can't miss. We can't rely on notify-send: the
// routine lockdown kills plasmashell, taking KDE's notification daemon with it.
// kdialog / zenity / xmessage each draw their own window straight through the
// compositor (still alive), so they show regardless. notify-send is a last
// resort.
void LinuxBackend::showExtensionDisabledAlert() const
{
    const QString title = QStringLiteral("FocusOS — Enable the Blocker extension");
    const QString message = QStringLiteral(
        "The FocusOS Blocker browser extension is disabled or missing.\n\n"
        "Internet access is BLOCKED until you re-enable it:\n"
        "open your browser's extensions page and turn FocusOS Blocker back on.");

    if (!firstExecutable({QStringLiteral("kdialog")}).isEmpty()) {
        QProcess::startDetached(QStringLiteral("kdialog"),
                                {QStringLiteral("--title"), title,
                                 QStringLiteral("--sorry"), message});
        return;
    }
    if (!firstExecutable({QStringLiteral("zenity")}).isEmpty()) {
        QProcess::startDetached(QStringLiteral("zenity"),
                                {QStringLiteral("--warning"),
                                 QStringLiteral("--title"), title,
                                 QStringLiteral("--text"), message});
        return;
    }
    if (!firstExecutable({QStringLiteral("xmessage")}).isEmpty()) {
        QProcess::startDetached(QStringLiteral("xmessage"),
                                {QStringLiteral("-center"),
                                 title + QStringLiteral("\n\n") + message});
        return;
    }
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("-u"), QStringLiteral("critical"),
                             title, message});
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

void LinuxBackend::setDisplaySleepInhibited(bool inhibited)
{
    if (inhibited) {
        if (m_displayInhibitor.state() != QProcess::NotRunning) {
            return;
        }
        const QString inhibit = QStandardPaths::findExecutable(QStringLiteral("systemd-inhibit"));
        if (inhibit.isEmpty()) {
            return;
        }
        // Hold a logind idle/sleep inhibitor for the lifetime of `sleep
        // infinity`; powerdevil / KWin honor it and won't blank or sleep the
        // display. Terminating the helper releases the lock.
        m_displayInhibitor.setProgram(inhibit);
        m_displayInhibitor.setArguments({
            QStringLiteral("--what=idle:sleep"),
            QStringLiteral("--who=FocusOS"),
            QStringLiteral("--why=Focus routine active"),
            QStringLiteral("--mode=block"),
            QStringLiteral("sleep"),
            QStringLiteral("infinity")
        });
        m_displayInhibitor.start();
        return;
    }

    if (m_displayInhibitor.state() != QProcess::NotRunning) {
        m_displayInhibitor.terminate();
        if (!m_displayInhibitor.waitForFinished(1000)) {
            m_displayInhibitor.kill();
            m_displayInhibitor.waitForFinished(200);
        }
    }
}

void LinuxBackend::releaseDisplaySleepInhibitors()
{
    // The crash handler can't drive a QProcess we may not own (the inhibitor
    // could belong to a predecessor the respawn watchdog just replaced), and the
    // helper is started detached, so it outlives a crashing FocusOS. Sweep every
    // FocusOS-tagged systemd-inhibit by its --who marker instead. The pattern
    // matches only our own inhibitor command line, never the FocusOS binary or
    // an unrelated lock. startDetached so this is safe from the signal handler:
    // the pkill survives our own death and completes independently.
    QProcess::startDetached(QStringLiteral("pkill"),
                            {QStringLiteral("-f"),
                             QStringLiteral("--"),
                             QStringLiteral("--who=FocusOS")});
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

// Drop the cross-process "stop respawning, this exit is intentional" marker that
// the kiosk respawn chain watches. Both the --kiosk watchdog and the session
// wrapper (focusos-session.sh) poll ~/.focusos/session-exit: the watchdog stops
// respawning FocusOS when it appears, and the wrapper, once kwin exits, ends the
// login session (back to the SDDM greeter) instead of falling through to the
// stock Plasma desktop. Without this marker a plain quit is simply respawned by
// the watchdog — which is why sign out / restart / shut down appeared to do
// nothing in the permanent kiosk install. Best-effort: a dev run outside the
// kiosk chain just exits the process normally and ignores the (harmless) file.
void LinuxBackend::writeSessionExitMarker(const QString &intent)
{
    const QString focusDir = QDir::homePath() + QStringLiteral("/.focusos");
    QDir().mkpath(focusDir);
    QFile marker(focusDir + QStringLiteral("/session-exit"));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(intent.toUtf8());
        marker.write("\n");
        marker.close();
    }
}

bool LinuxBackend::signOut(QString *errorMessage)
{
    // Drop any network policy first so a failed/partial sign-out doesn't strand
    // the machine behind nftables.
    dropNetworkPolicy();

    // Signal the respawn chain that this quit is intentional, THEN quit. When
    // FocusOS is the session shell (launched via the --kiosk watchdog under
    // kwin's --exit-with-session), the marker makes the watchdog stop respawning
    // and makes focusos-session.sh end the login session — the display manager
    // reclaims the VT and shows the greeter. The older path tried to escalate
    // through loginctl terminate-session/terminate-user and pkill kwin, but when
    // those didn't take cleanly the user was stranded on a blank screen with no
    // greeter; routing the exit back out through the session wrapper is cleaner.
    Q_UNUSED(errorMessage);
    writeSessionExitMarker(QStringLiteral("signout"));
    QCoreApplication::quit();
    return true;
}

// Hand the machine to logind for reboot / poweroff. systemctl is the portable
// path (it delegates to logind, already configured for this session); fall back
// to loginctl, which gained the same verbs in modern systemd. Detached +
// fire-and-forget: the kernel tears the session down, so the respawn watchdog
// dies with it — no need to stand it down first. Drop the network policy so a
// failed/partial power action never strands the box behind nftables.
bool LinuxBackend::restartMachine(QString *errorMessage)
{
    dropNetworkPolicy();
    // Stop the respawn chain so FocusOS isn't relaunched in the seconds between
    // issuing the reboot and the kernel taking the machine down.
    writeSessionExitMarker(QStringLiteral("restart"));
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (!systemctl.isEmpty()) {
        return QProcess::startDetached(systemctl, {QStringLiteral("reboot")});
    }
    const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
    if (!loginctl.isEmpty()) {
        return QProcess::startDetached(loginctl, {QStringLiteral("reboot")});
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Neither systemctl nor loginctl is available to restart");
    }
    return false;
}

bool LinuxBackend::shutdownMachine(QString *errorMessage)
{
    dropNetworkPolicy();
    writeSessionExitMarker(QStringLiteral("shutdown"));
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (!systemctl.isEmpty()) {
        return QProcess::startDetached(systemctl, {QStringLiteral("poweroff")});
    }
    const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
    if (!loginctl.isEmpty()) {
        return QProcess::startDetached(loginctl, {QStringLiteral("poweroff")});
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Neither systemctl nor loginctl is available to shut down");
    }
    return false;
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

namespace {

// True if any running process's /proc/<pid>/comm matches one of `names`. comm is
// truncated to 15 chars by the kernel, so compare against that prefix too.
bool anyProcessRunning(const QStringList &names)
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool isPid = false;
        pid.toLongLong(&isPid);
        if (!isPid) {
            continue;
        }
        QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!commFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString comm = QString::fromUtf8(commFile.readAll()).trimmed();
        if (comm.isEmpty()) {
            continue;
        }
        for (const QString &name : names) {
            if (comm == name || comm == name.left(15)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

void LinuxBackend::ensureGlobalShortcutsDaemon()
{
    // Already up (something started it, or a previous FocusOS launch did) —
    // nothing to do. KGlobalAccel will bind to whichever instance is running.
    if (anyProcessRunning({QStringLiteral("kglobalacceld"),
                           QStringLiteral("kglobalaccel6"),
                           QStringLiteral("kglobalaccel5")})) {
        return;
    }

    // The KF6 daemon is usually shipped in a libexec dir that isn't on PATH, so
    // probe the common locations as well as PATH. KF5's binary was named with a
    // version suffix; keep it as a fallback for older installs.
    QString daemon = firstExecutable({QStringLiteral("kglobalacceld"),
                                      QStringLiteral("kglobalaccel6"),
                                      QStringLiteral("kglobalaccel5")});
    if (daemon.isEmpty()) {
        static const QStringList libexecCandidates {
            QStringLiteral("/usr/libexec/kglobalacceld"),
            QStringLiteral("/usr/lib/kglobalacceld"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec/kglobalacceld"),
            QStringLiteral("/usr/lib/aarch64-linux-gnu/libexec/kglobalacceld"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/kglobalaccel6"),
        };
        for (const QString &candidate : libexecCandidates) {
            if (QFileInfo::exists(candidate)) {
                daemon = candidate;
                break;
            }
        }
    }
    if (daemon.isEmpty()) {
        return;
    }
    startDetachedWithKdeEnvironment(daemon);
}

void LinuxBackend::runSessionStartupItems()
{
    // Run at most once per login session. The respawn watchdog relaunches FocusOS
    // after a crash/kill, but the rest of the session (and the daemons we started)
    // is still up — re-running autostart would spawn duplicate agents. A marker in
    // $XDG_RUNTIME_DIR is the right scope: the kernel wipes that dir on logout, so
    // a genuine fresh login re-runs the items while a mid-session respawn skips.
    const QString runtimeDir =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (!runtimeDir.isEmpty()) {
        const QString marker = runtimeDir + QStringLiteral("/focusos-startup-done");
        if (QFileInfo::exists(marker)) {
            return;
        }
        QFile markerFile(marker);
        if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            markerFile.close();
        }
    }

    // The user-editable startup script is the *only* login hook now. We used to
    // also replay every ~/.config/autostart/*.desktop entry to bring up tray
    // agents on the bare kwin_wayland session, but that was too blunt: some
    // distros ship an autostart entry that pulls in the whole Plasma desktop
    // (plasmashell / a full session), which is exactly the environment FocusOS
    // replaces — so the shell ended up launching Plasma on top of itself. Rather
    // than guess which entries are safe, we hand the user explicit control: list
    // the few things you actually want (e.g. `toshy-services-restart`) in
    // ~/.focusos/startup.sh, editable from the SYSTEM tab of the Settings modal.
    //
    // Run it through a shell so it can be a plain list of commands without a +x
    // bit. Skip an empty/whitespace-only file so we don't spawn a no-op sh.
    // Seed the systemd --user manager / D-Bus activation environment first, so
    // user services kicked off by the script (Toshy in particular) inherit a
    // working Wayland + KDE environment instead of failing on the bare session.
    seedUserServiceEnvironment();

    const QString scriptPath = QDir::homePath() + QStringLiteral("/.focusos/startup.sh");
    QFile script(scriptPath);
    if (script.exists() && script.open(QIODevice::ReadOnly)) {
        const QString contents = QString::fromUtf8(script.readAll());
        script.close();
        if (!contents.trimmed().isEmpty()) {
            const QString shell = firstExecutable({QStringLiteral("bash"), QStringLiteral("sh")});
            if (!shell.isEmpty()) {
                startDetachedWithKdeEnvironment(shell, {scriptPath});
            }
        }
    }
}
