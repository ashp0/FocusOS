#include "platform/macos/MacBackend.h"

#include "blocker/BlockerPolicy.h"
#include "platform/macos/MacBackendNative.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamWriter>
#include <QtConcurrent>

#include <csignal>
#include <pwd.h>
#include <utility>
#include <unistd.h>

namespace {

struct ParsedAppEntry
{
    bool kiosk = false;
    QString kioskUrl;
    QString path;
    QStringList args;
};

QString expandedPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.startsWith(QStringLiteral("~/"))) {
        return QDir::homePath() + trimmed.mid(1);
    }
    return trimmed;
}

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

    // App bundle paths often arrive from the picker unquoted, e.g.
    // /Applications/Google Chrome.app. QProcess::splitCommand would split that
    // at the space, so detect the bundle suffix before command parsing.
    const int bundleSuffix = trimmed.indexOf(QStringLiteral(".app"), 0, Qt::CaseInsensitive);
    if (bundleSuffix >= 0) {
        const QString candidate = expandedPath(trimmed.left(bundleSuffix + 4));
        if (QFileInfo(candidate).isDir()) {
            parsed.path = candidate;
            const QString remaining = trimmed.mid(bundleSuffix + 4).trimmed();
            if (!remaining.isEmpty()) {
                parsed.args = QProcess::splitCommand(remaining);
                for (QString &arg : parsed.args) {
                    arg = expandedPath(arg);
                }
            }
            return parsed;
        }
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

bool isAppBundle(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() &&
           (info.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0 ||
            path.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive));
}

QString executableDisplayName(const QString &path)
{
    const QFileInfo info(path);
    if (isAppBundle(path)) {
        const QString name = MacBackendNative::displayNameForApplication(path);
        if (!name.isEmpty()) {
            return name;
        }
        return info.completeBaseName();
    }
    return info.fileName();
}

QString executablePathForEntry(const ParsedAppEntry &entry)
{
    if (entry.path.isEmpty()) {
        return {};
    }

    if (isAppBundle(entry.path)) {
        return MacBackendNative::applicationExecutablePath(entry.path);
    }

    if (entry.path.contains(QLatin1Char('/'))) {
        return QFileInfo(entry.path).absoluteFilePath();
    }

    const QString resolved = QStandardPaths::findExecutable(entry.path);
    return resolved.isEmpty() ? entry.path : resolved;
}

QString bundleIdentifierForEntry(const ParsedAppEntry &entry)
{
    if (entry.path.isEmpty()) {
        return {};
    }
    if (isAppBundle(entry.path)) {
        return MacBackendNative::bundleIdentifierForApplication(entry.path);
    }
    return MacBackendNative::bundleIdentifierForExecutable(executablePathForEntry(entry));
}

QStringList processNamesForCommandLines(const QStringList &entries)
{
    QStringList names;
    for (const QString &rawEntry : entries) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk || entry.path.isEmpty()) {
            continue;
        }

        const QString executable = executablePathForEntry(entry);
        const QString name = QFileInfo(executable.isEmpty() ? entry.path : executable).fileName();
        if (!name.isEmpty()) {
            names.append(name);
        }

        const QString displayName = executableDisplayName(entry.path);
        if (!displayName.isEmpty()) {
            names.append(displayName);
        }
    }
    names.removeDuplicates();
    return names;
}

QStringList bundleIdentifiersForCommandLines(const QStringList &entries)
{
    QStringList identifiers;
    for (const QString &rawEntry : entries) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk || entry.path.isEmpty()) {
            continue;
        }

        const QString identifier = bundleIdentifierForEntry(entry);
        if (!identifier.isEmpty()) {
            identifiers.append(identifier);
        }
    }
    identifiers.removeDuplicates();
    return identifiers;
}

QStringList executablePathsForCommandLines(const QStringList &entries)
{
    QStringList paths;
    for (const QString &rawEntry : entries) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk || entry.path.isEmpty()) {
            continue;
        }

        const QString executable = executablePathForEntry(entry);
        if (!executable.isEmpty()) {
            paths.append(QFileInfo(executable).absoluteFilePath());
        }
    }
    paths.removeDuplicates();
    return paths;
}

QStringList defaultBlockedProcessNames()
{
    return {
        QStringLiteral("Terminal"),
        QStringLiteral("iTerm2"),
        QStringLiteral("iTerm"),
        QStringLiteral("Warp"),
        QStringLiteral("Hyper"),
        QStringLiteral("Alacritty"),
        QStringLiteral("kitty"),
        QStringLiteral("WezTerm"),
        QStringLiteral("Activity Monitor"),
        QStringLiteral("System Settings"),
        QStringLiteral("System Preferences"),
        QStringLiteral("App Store"),
        QStringLiteral("Notes"),
        QStringLiteral("Photos"),
        QStringLiteral("Mail"),
        QStringLiteral("Messages"),
        QStringLiteral("FaceTime"),
        QStringLiteral("Safari"),
        QStringLiteral("Calendar"),
        QStringLiteral("Reminders"),
        QStringLiteral("Freeform"),
        QStringLiteral("News"),
        QStringLiteral("TV"),
        QStringLiteral("Podcasts"),
        QStringLiteral("Books"),
        QStringLiteral("Maps"),
        QStringLiteral("Contacts"),
        QStringLiteral("Discord"),
        QStringLiteral("Slack"),
        QStringLiteral("Telegram"),
        QStringLiteral("Signal"),
        QStringLiteral("Steam"),
        QStringLiteral("Spotify"),
        QStringLiteral("Music")
    };
}

QStringList defaultBlockedBundleIdentifiers()
{
    return {
        QStringLiteral("com.apple.Terminal"),
        QStringLiteral("com.googlecode.iterm2"),
        QStringLiteral("dev.warp.Warp-Stable"),
        QStringLiteral("co.zeit.hyper"),
        QStringLiteral("org.alacritty"),
        QStringLiteral("net.kovidgoyal.kitty"),
        QStringLiteral("com.github.wez.wezterm"),
        QStringLiteral("com.apple.ActivityMonitor"),
        QStringLiteral("com.apple.systempreferences"),
        QStringLiteral("com.apple.systemsettings"),
        QStringLiteral("com.apple.AppStore"),
        QStringLiteral("com.apple.Notes"),
        QStringLiteral("com.apple.Photos"),
        QStringLiteral("com.apple.mail"),
        QStringLiteral("com.apple.MobileSMS"),
        QStringLiteral("com.apple.FaceTime"),
        QStringLiteral("com.apple.Safari"),
        QStringLiteral("com.apple.iCal"),
        QStringLiteral("com.apple.reminders"),
        QStringLiteral("com.apple.freeform"),
        QStringLiteral("com.apple.news"),
        QStringLiteral("com.apple.TV"),
        QStringLiteral("com.apple.podcasts"),
        QStringLiteral("com.apple.BKAgentService"),
        QStringLiteral("com.apple.iBooksX"),
        QStringLiteral("com.apple.Maps"),
        QStringLiteral("com.apple.AddressBook"),
        QStringLiteral("com.hnc.Discord"),
        QStringLiteral("com.tinyspeck.slackmacgap"),
        QStringLiteral("ru.keepcoder.Telegram"),
        QStringLiteral("org.whispersystems.signal-desktop"),
        QStringLiteral("com.valvesoftware.steam"),
        QStringLiteral("com.spotify.client"),
        QStringLiteral("com.apple.Music")
    };
}

QStringList browserExecutableCandidates()
{
    const QStringList bundleRelativePaths {
        QStringLiteral("Brave Browser.app/Contents/MacOS/Brave Browser"),
        QStringLiteral("Google Chrome.app/Contents/MacOS/Google Chrome"),
        QStringLiteral("Chromium.app/Contents/MacOS/Chromium"),
        QStringLiteral("Microsoft Edge.app/Contents/MacOS/Microsoft Edge"),
        QStringLiteral("Vivaldi.app/Contents/MacOS/Vivaldi"),
        QStringLiteral("Opera.app/Contents/MacOS/Opera")
    };

    QStringList roots {
        QStringLiteral("/Applications"),
        QDir::homePath() + QStringLiteral("/Applications")
    };

    QStringList candidates;
    for (const QString &root : roots) {
        for (const QString &relative : bundleRelativePaths) {
            candidates.append(QDir(root).absoluteFilePath(relative));
        }
    }
    return candidates;
}

QString firstExecutableFile(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString consoleHomePath()
{
    const QByteArray sudoUser = qgetenv("SUDO_USER");
    if (geteuid() == 0 && !sudoUser.isEmpty() && sudoUser != "root") {
        if (const struct passwd *pw = getpwnam(sudoUser.constData())) {
            if (pw->pw_dir && pw->pw_dir[0] != '\0') {
                return QString::fromLocal8Bit(pw->pw_dir);
            }
        }
    }
    return QDir::homePath();
}

uint launchdUserId()
{
    bool ok = false;
    const uint sudoUid = qEnvironmentVariable("SUDO_UID").toUInt(&ok);
    if (geteuid() == 0 && ok && sudoUid > 0) {
        return sudoUid;
    }
    return static_cast<uint>(getuid());
}

QString launchdGuiDomain()
{
    return QStringLiteral("gui/%1").arg(launchdUserId());
}

QString focusosDataDir()
{
    return QDir(consoleHomePath()).absoluteFilePath(QStringLiteral(".focusos"));
}

QString watchdogPlistPath()
{
    return QDir(focusosDataDir()).absoluteFilePath(QStringLiteral("com.focusos.watchdog.plist"));
}

// The persistent kiosk LaunchAgent (launch-at-login + KeepAlive respawn). Unlike
// the crash-recovery watchdog — which is deliberately kept OUT of
// ~/Library/LaunchAgents so a stale checkpoint can't auto-launch FocusOS after a
// reboot — this one is MEANT to auto-load every login, so it lives in the
// standard per-user LaunchAgents directory launchd bootstraps at login.
const QString kLoginAgentLabel = QStringLiteral("com.focusos.login");

QString loginAgentPlistPath()
{
    return QDir(consoleHomePath())
        .absoluteFilePath(QStringLiteral("Library/LaunchAgents/com.focusos.login.plist"));
}

// KeepAlive gate for the login agent. The agent's KeepAlive is conditional on this
// file existing (PathState), NOT an unconditional `true`. That is what makes the
// respawn lock instantly switchable WITHOUT booting out the loaded job (which would
// SIGTERM the running FocusOS): launchd re-checks the file each time FocusOS exits,
// so removing it stops respawn for the current session too. FocusOS re-creates the
// file at every launch while the agent is installed (see ensureKioskRespawnArmed),
// which also gives crash-loop safety — a build that dies before arming the flag is
// not respawned into a loop.
QString loginAgentKeepAlivePath()
{
    return QDir(focusosDataDir()).absoluteFilePath(QStringLiteral("kiosk-active"));
}

QString legacyWatchdogPlistPath()
{
    return QDir(consoleHomePath()).absoluteFilePath(
        QStringLiteral("Library/LaunchAgents/com.focusos.watchdog.plist"));
}

void bootoutWatchdog(const QString &plistPath)
{
    if (plistPath.isEmpty()) {
        return;
    }
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("bootout"), launchdGuiDomain(), plistPath});
}

void removeLegacyWatchdogLaunchAgent()
{
    const QString plistPath = legacyWatchdogPlistPath();
    bootoutWatchdog(plistPath);
    QFile::remove(plistPath);
}

QProcessEnvironment routineLaunchEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("CHROME_HEADLESS"), QStringLiteral("0"));
    return env;
}

bool launchCommand(const QString &program,
                   const QStringList &arguments,
                   qint64 *pidOut = nullptr,
                   QString *errorMessage = nullptr)
{
    QString resolvedProgram = expandedPath(program);
    if (!resolvedProgram.contains(QLatin1Char('/'))) {
        const QString resolved = QStandardPaths::findExecutable(resolvedProgram);
        if (!resolved.isEmpty()) {
            resolvedProgram = resolved;
        }
    }

    QProcess process;
    process.setProgram(resolvedProgram);
    process.setArguments(arguments);
    process.setProcessEnvironment(routineLaunchEnvironment());
    qint64 pid = 0;
    const bool launched = process.startDetached(&pid);
    if (launched && pidOut) {
        *pidOut = pid;
    }
    if (!launched && errorMessage) {
        *errorMessage = QStringLiteral("Unable to launch %1").arg(program);
    }
    return launched;
}

bool launchKioskBrowser(const QString &url,
                        qint64 *pidOut = nullptr,
                        QString *executableOut = nullptr,
                        QString *errorMessage = nullptr)
{
    const QString browser = firstExecutableFile(browserExecutableCandidates());
    if (browser.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No Chromium-family browser installed for kiosk mode");
        }
        return false;
    }

    const QString runtimeRoot = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation).isEmpty()
        ? QDir::tempPath()
        : QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString kioskProfile = QDir(runtimeRoot).absoluteFilePath(
        QStringLiteral("focusos-kiosk-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(kioskProfile);

    const QStringList args {
        QStringLiteral("--app=%1").arg(url),
        QStringLiteral("--user-data-dir=%1").arg(kioskProfile),
        QStringLiteral("--start-fullscreen"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--use-mock-keychain"),
        QStringLiteral("--disable-features=Translate")
    };

    if (executableOut) {
        *executableOut = browser;
    }
    return launchCommand(browser, args, pidOut, errorMessage);
}

bool matchesEntry(const MacBackend::LaunchedProcess &process, const QStringList &entries)
{
    if (entries.isEmpty()) {
        return true;
    }

    for (const QString &rawEntry : entries) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk) {
            if (process.sourceEntry == rawEntry) {
                return true;
            }
            continue;
        }

        const QString executable = executablePathForEntry(entry);
        const QString bundleIdentifier = bundleIdentifierForEntry(entry);
        if (!executable.isEmpty() && QFileInfo(executable).absoluteFilePath() == process.executablePath) {
            return true;
        }
        if (!bundleIdentifier.isEmpty() && bundleIdentifier == process.bundleIdentifier) {
            return true;
        }
        if (!entry.path.isEmpty() && process.sourceEntry == rawEntry) {
            return true;
        }
    }
    return false;
}

QString xmlEscaped(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return escaped;
}

} // namespace

MacBackend::MacBackend()
{
    removeLegacyWatchdogLaunchAgent();
    // Re-arm the launch-at-login respawn lock for this session if the kiosk agent
    // is installed (covers RunAtLoad launches and respawns; no-op when disabled).
    ensureKioskRespawnArmed();
}

MacBackend::~MacBackend()
{
    stopLockdown();
    MacBackendNative::setSystemDockHidden(false);
    setMissionControlDisabled(false);
    setDisplaySleepInhibited(false);
}

QString MacBackend::name() const
{
    return QStringLiteral("macOS");
}

void MacBackend::prepareRoutineSession(const QStringList &appPaths)
{
    qInfo() << "[engage] prepareRoutineSession: begin, apps=" << appPaths.size();
    m_sessionAppEntries = appPaths;
    m_desktopAccessOpen = false;

    // FocusOS moves into its OWN native-fullscreen Space during a routine (handled
    // by ShellWindow), so the routine's app windows stay reachable in the desktop
    // Space. Two consequences here:
    //  • Leave the kiosk presentation (don't DisableProcessSwitching) so the user
    //    can ⌘-Tab / Mission Control between FocusOS and the allowed apps.
    //  • Neuter the Dock (tiny + empty) rather than just hiding it, so a Mission
    //    Control swipe-up never reveals a useful launch surface. The user's real
    //    Dock is snapshotted and restored when the routine ends / desktop unlocks.
    qInfo() << "[engage] prepareRoutineSession: leaveKioskPresentation + neuterDock";
    MacBackendNative::leaveKioskPresentation();
    MacBackendNative::neuterDock(true);
    // A routine intentionally keeps Mission Control / Spaces available so the user
    // can swipe between FocusOS's fullscreen Space and the routine's app windows —
    // re-enable it if the locked home screen had it disabled.
    setMissionControlDisabled(false);

    qInfo() << "[engage] prepareRoutineSession: startLockdown";
    startLockdown();
    qInfo() << "[engage] prepareRoutineSession: quitBackgroundApps";

    // Strict enforcement parity with Linux: close the user's other GUI apps so a
    // routine starts from a clean surface — nothing but the routine's own apps,
    // the always-allowed list, Finder, and FocusOS. The QML side previews this
    // (previewBackgroundAppQuit) before engage so unsaved work can be saved.
    quitBackgroundApps(appPaths);
}

bool MacBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
    const bool restoreLaunchEnforcement = m_lockdownActive && !m_desktopAccessOpen;
    if (restoreLaunchEnforcement) {
        MacBackendNative::stopExecBlocker();
        MacBackendNative::stopLaunchWatcher();
    }
    const auto restoreLockdown = qScopeGuard([this, restoreLaunchEnforcement]() {
        if (restoreLaunchEnforcement) {
            startLockdown();
        }
    });

    for (const QString &rawEntry : appPaths) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk) {
            if (entry.kioskUrl.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Kiosk entry has no URL: %1").arg(rawEntry);
                }
                return false;
            }

            qint64 pid = 0;
            QString executable;
            QString launchError;
            if (!launchKioskBrowser(entry.kioskUrl, &pid, &executable, &launchError)) {
                if (errorMessage) {
                    *errorMessage = launchError;
                }
                return false;
            }
            if (pid > 0) {
                m_launchedProcesses.append({
                    pid,
                    rawEntry,
                    QFileInfo(executable).absoluteFilePath(),
                    MacBackendNative::bundleIdentifierForExecutable(executable),
                    QFileInfo(executable).completeBaseName()
                });
            }
            continue;
        }

        if (entry.path.isEmpty()) {
            continue;
        }

        if (isAppBundle(entry.path)) {
            MacBackendNative::NativeLaunchResult result =
                MacBackendNative::launchApplicationBundle(entry.path, entry.args);
            if (!result.launched) {
                if (errorMessage) {
                    *errorMessage = result.errorMessage.isEmpty()
                        ? QStringLiteral("Unable to launch %1").arg(entry.path)
                        : result.errorMessage;
                }
                return false;
            }
            if (result.pid > 0) {
                m_launchedProcesses.append({
                    result.pid,
                    rawEntry,
                    result.executablePath,
                    result.bundleIdentifier,
                    result.displayName
                });
            }
            continue;
        }

        // A data file added via "Open File" (PDF, image, office doc, video…):
        // hand it to its default application via `open` rather than exec'ing it.
        const QFileInfo entryInfo(entry.path);
        if (entryInfo.isFile() && !entryInfo.isExecutable()) {
            if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                         {entryInfo.absoluteFilePath()})) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Unable to open %1").arg(entry.path);
                }
                return false;
            }
            continue;
        }

        qint64 pid = 0;
        QString launchError;
        if (!launchCommand(entry.path, entry.args, &pid, &launchError)) {
            if (errorMessage) {
                *errorMessage = launchError;
            }
            return false;
        }
        if (pid > 0) {
            const QString executable = executablePathForEntry(entry);
            m_launchedProcesses.append({
                pid,
                rawEntry,
                QFileInfo(executable).absoluteFilePath(),
                MacBackendNative::bundleIdentifierForExecutable(executable),
                QFileInfo(executable).fileName()
            });
        }
    }

    return true;
}

bool MacBackend::openUrls(const QStringList &urls, QString *errorMessage)
{
    QStringList normalizedUrls;
    for (const QString &url : urls) {
        const QString trimmed = url.trimmed();
        if (!trimmed.isEmpty()) {
            normalizedUrls.append(QUrl::fromUserInput(trimmed).toString());
        }
    }
    if (normalizedUrls.isEmpty()) {
        return true;
    }

    const bool restoreLaunchEnforcement = m_lockdownActive && !m_desktopAccessOpen;
    if (restoreLaunchEnforcement) {
        MacBackendNative::stopExecBlocker();
        MacBackendNative::stopLaunchWatcher();
    }
    const auto restoreLockdown = qScopeGuard([this, restoreLaunchEnforcement]() {
        if (restoreLaunchEnforcement) {
            startLockdown();
        }
    });

    const QString browser = firstExecutableFile(browserExecutableCandidates());
    if (!browser.isEmpty()) {
        QStringList args {
            QStringLiteral("--new-window"),
            QStringLiteral("--no-first-run"),
            QStringLiteral("--no-default-browser-check"),
            QStringLiteral("--use-mock-keychain")
        };
        args.append(normalizedUrls);

        qint64 pid = 0;
        if (launchCommand(browser, args, &pid)) {
            if (pid > 0) {
                m_launchedProcesses.append({
                    pid,
                    QStringLiteral("urls:%1").arg(normalizedUrls.join(QLatin1Char(' '))),
                    QFileInfo(browser).absoluteFilePath(),
                    MacBackendNative::bundleIdentifierForExecutable(browser),
                    QFileInfo(browser).completeBaseName()
                });
            }
            return true;
        }
    }

    for (const QString &url : normalizedUrls) {
        if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"), {url})) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to open %1").arg(url);
            }
            return false;
        }
    }
    return true;
}

void MacBackend::terminateApps(const QStringList &appPaths)
{
    killTrackedProcesses(appPaths);

    QStringList bundleIdentifiers;
    QStringList displayNames;
    QStringList executablePaths;
    const QStringList alwaysAllowedBundles = bundleIdentifiersForCommandLines(m_alwaysAllowedCommandLines);
    const QStringList alwaysAllowedNames = processNamesForCommandLines(m_alwaysAllowedCommandLines);
    const QStringList alwaysAllowedPaths = executablePathsForCommandLines(m_alwaysAllowedCommandLines);

    for (const QString &rawEntry : appPaths) {
        const ParsedAppEntry entry = parseAppEntry(rawEntry);
        if (entry.kiosk || entry.path.isEmpty()) {
            continue;
        }

        const QString bundleIdentifier = bundleIdentifierForEntry(entry);
        const QString executablePath = executablePathForEntry(entry);
        const QString displayName = executableDisplayName(entry.path);

        if (!bundleIdentifier.isEmpty() && !alwaysAllowedBundles.contains(bundleIdentifier)) {
            bundleIdentifiers.append(bundleIdentifier);
        }
        if (!displayName.isEmpty() && !alwaysAllowedNames.contains(displayName)) {
            displayNames.append(displayName);
        }
        if (!executablePath.isEmpty() && !alwaysAllowedPaths.contains(executablePath)) {
            executablePaths.append(QFileInfo(executablePath).absoluteFilePath());
        }
    }

    bundleIdentifiers.removeDuplicates();
    displayNames.removeDuplicates();
    executablePaths.removeDuplicates();
    MacBackendNative::terminateApplications(bundleIdentifiers, displayNames, executablePaths);
}

bool MacBackend::applyNetworkPolicy(const QStringList &allowedHosts, QString *errorMessage)
{
    QString networkError;
    if (!MacBackendNative::applyNetworkFilter(allowedHosts, &networkError)) {
        if (errorMessage) {
            *errorMessage = networkError;
        }
        return false;
    }

    BlockerPolicy::write(true, allowedHosts);
    armBlockerPresenceWatch(allowedHosts);
    return true;
}

void MacBackend::applyNetworkPolicyAsync(const QStringList &allowedHosts,
                                         std::function<void(bool, const QString &)> onComplete)
{
    // Mirror LinuxBackend exactly: resolve DNS + render the pf ruleset on a
    // managed QtConcurrent worker thread (the multi-second part that froze the UI
    // on engage), then hop back to the GUI thread to do the privileged pfctl swap
    // and the blocker-policy write.
    //
    // The previous implementation ran the WHOLE apply — including the pfctl
    // QProcess (driven by waitForStarted/waitForFinished) and BlockerPolicy::write
    // — on a RAW detached std::thread. Driving QProcess off a non-QThread whose
    // QThreadData is torn down when the thread exits is the crash the REFLECTIONS
    // routine hit (it is the only routine that takes this path: network-locked,
    // not full-access, no browser). Keeping all QProcess/QObject work on the GUI
    // thread removes that hazard and matches the proven Linux path.
    auto *watcher = new QFutureWatcher<QString>();
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, qApp,
                     [this, watcher, allowedHosts, onComplete = std::move(onComplete)]() {
                         qInfo() << "[engage] applyNetworkPolicyAsync: finished callback (in QFutureWatcher::finished)";
                         const QString ruleset = watcher->result();
                         watcher->deleteLater();
                         QString error;
                         const bool ok = MacBackendNative::commitNetworkFilter(ruleset, &error);
                         qInfo() << "[engage] applyNetworkPolicyAsync: commit ok=" << ok << "— calling onComplete (→finishEngage→nested run loop)";
                         if (ok) {
                             BlockerPolicy::write(true, allowedHosts);
                             armBlockerPresenceWatch(allowedHosts);
                         }
                         onComplete(ok, error);
                         qInfo() << "[engage] applyNetworkPolicyAsync: onComplete returned (back in finished callback)";
                     });
    watcher->setFuture(QtConcurrent::run([allowedHosts]() -> QString {
        return MacBackendNative::buildNetworkFilterRuleset(allowedHosts);
    }));
}

void MacBackend::applyBrowserBlockerPolicy(const QStringList &allowedHosts)
{
    // Browser routine: hand the allowlist to the blocker extension (via the
    // signed policy the native host reads) and leave pf untouched — the
    // extension does the URL gating, not the system firewall.
    BlockerPolicy::write(true, allowedHosts);
}

void MacBackend::dropNetworkPolicy()
{
    disarmBlockerPresenceWatch();
    BlockerPolicy::write(false, {});
    MacBackendNative::dropNetworkFilter();
}

// === Blocker extension presence clamp (mirrors LinuxBackend) ====================

void MacBackend::armBlockerPresenceWatch(const QStringList &allowedHosts)
{
    m_activeAllowedHosts = allowedHosts;
    m_networkLockActive = true;
    m_extensionSeenAlive = false;
    m_extensionBanActive = false;
    m_extensionMissingSinceMs = 0;
    m_lastExtensionAlertMs = 0;

    if (!m_presenceTimerWired) {
        m_presenceTimerWired = true;
        m_presenceTimer.setInterval(1500);
        // QTimer is itself a QObject, so it can be the connection context even
        // though MacBackend is not a QObject (same pattern as LinuxBackend).
        QObject::connect(&m_presenceTimer, &QTimer::timeout, &m_presenceTimer,
                         [this] { enforceBlockerExtension(); });
    }
    if (!m_presenceTimer.isActive()) {
        m_presenceTimer.start();
    }
}

void MacBackend::disarmBlockerPresenceWatch()
{
    m_networkLockActive = false;
    m_extensionBanActive = false;
    m_extensionSeenAlive = false;
    m_extensionMissingSinceMs = 0;
    m_lastExtensionAlertMs = 0;
    m_activeAllowedHosts.clear();
    if (m_presenceTimer.isActive()) {
        m_presenceTimer.stop();
    }
}

// The native host rewrites host-alive every ~1.5s while a browser is connected.
// Alive == touched within the last few seconds (a couple of missed ticks slack).
bool MacBackend::blockerExtensionAlive() const
{
    const QFileInfo info(BlockerPolicy::heartbeatFilePath());
    if (!info.exists()) {
        return false;
    }
    const qint64 ageMs = info.lastModified().msecsTo(QDateTime::currentDateTime());
    return ageMs >= 0 && ageMs < 6000;
}

// True if a *user* Chromium-family browser is running. macOS has no /proc, so we
// shell out to `ps` once per tick (cheap) and read full command lines. FocusOS's
// own kiosk browsers are excluded by their --user-data-dir=focusos-kiosk-* marker
// (same as Linux): they run a throwaway profile with no extension by design, so
// judging them "extension missing" would clamp the network on the very site the
// routine pinned.
bool MacBackend::chromiumBrowserRunning() const
{
    QProcess ps;
    ps.start(QStringLiteral("/bin/ps"),
             {QStringLiteral("-axww"), QStringLiteral("-o"), QStringLiteral("command=")});
    if (!ps.waitForStarted(1000)) {
        return false;
    }
    if (!ps.waitForFinished(2000)) {
        ps.kill();
        ps.waitForFinished(200);
        return false;
    }
    const QList<QByteArray> lines = ps.readAllStandardOutput().split('\n');
    // Match the GUI browser executable, not helper/renderer child processes or
    // arbitrary tools that merely mention a browser name.
    static const QStringList kBrowserExecMarkers = {
        QStringLiteral("Brave Browser.app/Contents/MacOS/Brave Browser"),
        QStringLiteral("Google Chrome.app/Contents/MacOS/Google Chrome"),
        QStringLiteral("Chromium.app/Contents/MacOS/Chromium"),
        QStringLiteral("Microsoft Edge.app/Contents/MacOS/Microsoft Edge"),
        QStringLiteral("Vivaldi.app/Contents/MacOS/Vivaldi"),
    };
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw);
        if (line.contains(QStringLiteral("focusos-kiosk-"))) {
            continue; // FocusOS kiosk browser — not a user-controlled browser.
        }
        if (line.contains(QStringLiteral("--type="))) {
            continue; // renderer/GPU/utility child process, not the main browser.
        }
        for (const QString &marker : kBrowserExecMarkers) {
            if (line.contains(marker)) {
                return true;
            }
        }
    }
    return false;
}

void MacBackend::enforceBlockerExtension()
{
    if (!m_networkLockActive) {
        return;
    }

    // Manual mute switch: ~/.focusos/blocker/presence-check-off lifts the clamp
    // entirely (escape hatch while debugging host/extension wiring), matching
    // Linux. A false "extension missing" can't then strand the user.
    if (QFileInfo::exists(BlockerPolicy::blockerDir() + QStringLiteral("/presence-check-off"))) {
        if (m_extensionBanActive) {
            QString error;
            MacBackendNative::applyNetworkFilter(m_activeAllowedHosts, &error);
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
            MacBackendNative::applyNetworkFilter(m_activeAllowedHosts, &error);
            m_extensionBanActive = false;
            m_lastExtensionAlertMs = 0;
        }
        return;
    }

    // Debounce: require the condition to hold continuously before clamping, so
    // browser/extension/native-host startup lag doesn't trip a false ban.
    if (m_extensionMissingSinceMs == 0) {
        m_extensionMissingSinceMs = nowMs;
    }
    constexpr qint64 kMissingDebounceMs = 15000;
    if (!m_extensionBanActive && (nowMs - m_extensionMissingSinceMs) < kMissingDebounceMs) {
        return;
    }

    // Clamp once on entry (not every tick); keep the ban latched even if the
    // clamp call fails so we don't thrash pf or re-alert per tick. An empty
    // allowlist renders "block drop out all" + DNS/loopback only — full deny.
    if (!m_extensionBanActive) {
        m_extensionBanActive = true;
        QString error;
        MacBackendNative::applyNetworkFilter(QStringList{}, &error);
    }
    // Nag on entry and every 30s while clamped so the user can't miss why nothing
    // loads. Rate-limited independently of the clamp.
    if (m_lastExtensionAlertMs == 0 || (nowMs - m_lastExtensionAlertMs) > 30000) {
        showExtensionDisabledAlert();
        m_lastExtensionAlertMs = nowMs;
    }
}

void MacBackend::showExtensionDisabledAlert() const
{
    // Best-effort, non-blocking. A network-locked routine is fullscreen kiosk, so
    // we use a system alert via osascript (drawn by the WindowServer, survives the
    // kiosk presentation) rather than a Qt dialog we'd have to host.
    const QString script = QStringLiteral(
        "display alert \"FocusOS — Enable the Blocker extension\" "
        "message \"The FocusOS Blocker browser extension is disabled or missing. "
        "Internet access is BLOCKED until you re-enable it: open Brave's "
        "Extensions page and turn FocusOS Blocker back on.\" as critical");
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"),
                            {QStringLiteral("-e"), script});
}

bool MacBackend::openSystemTerminal(QString *errorMessage)
{
    const QStringList terminalBundles {
        QStringLiteral("/System/Applications/Utilities/Terminal.app"),
        QStringLiteral("/Applications/Utilities/Terminal.app"),
        QStringLiteral("/Applications/iTerm.app"),
        QStringLiteral("/Applications/iTerm2.app"),
        QStringLiteral("/Applications/Warp.app")
    };

    for (const QString &bundle : terminalBundles) {
        if (!QFileInfo(bundle).isDir()) {
            continue;
        }

        MacBackendNative::NativeLaunchResult result = MacBackendNative::launchApplicationBundle(bundle);
        if (result.launched) {
            if (result.pid > 0) {
                m_launchedProcesses.append({
                    result.pid,
                    bundle,
                    result.executablePath,
                    result.bundleIdentifier,
                    result.displayName
                });
            }
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unable to open a macOS terminal");
    }
    return false;
}

void MacBackend::terminateUnrestrictedApps()
{
    // Re-close anything the temporary admin desktop opened, back to the routine's
    // own apps + always-allowed list.
    quitBackgroundApps(m_sessionAppEntries);
    m_desktopAccessOpen = false;
    if (m_lockdownActive) {
        // A routine is still running underneath the closed admin desktop: drop back
        // into the routine posture — navigation allowed, neutered Dock, launch
        // watcher re-armed. ShellWindow re-enters the fullscreen routine Space.
        MacBackendNative::leaveKioskPresentation();
        MacBackendNative::neuterDock(true);
        startLockdown();
    } else {
        // Back to the locked home screen: real Dock (the kiosk presentation hides
        // it while FocusOS is frontmost) + full key/Spotlight/Mission-Control block.
        MacBackendNative::setSystemDockHidden(false);
        applyBaselineKioskPosture();
    }
}

bool MacBackend::launchDesktopShell(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    m_desktopAccessOpen = true;
    restoreAquaUiLockdown();
    MacBackendNative::stopExecBlocker();
    MacBackendNative::stopLaunchWatcher();
    // Full admin access: lift the system-shortcut blocker too so Spotlight, the
    // Dock and launchers work while the authorized desktop window is open.
    MacBackendNative::stopInputBlocker();
    MacBackendNative::setSystemDockHidden(false);
    MacBackendNative::leaveKioskPresentation();
    // Correct 6-digit code accepted → restore normal macOS, including Mission
    // Control / Spaces that the locked home screen disabled.
    setMissionControlDisabled(false);

    // The macOS "desktop shell" is Finder/Dock/SystemUIServer. They are managed
    // by launchd and stay running; kiosk mode merely hides and disables their
    // surfaces. Opening Terminal gives the temporary access window useful teeth.
    QString terminalError;
    openSystemTerminal(&terminalError);
    return true;
}

void MacBackend::terminateDesktopShell()
{
    m_desktopAccessOpen = false;
    if (m_lockdownActive) {
        // Routine still running: re-arm the routine posture (navigation allowed,
        // neutered Dock, launch watcher) rather than the full home-screen lock.
        MacBackendNative::leaveKioskPresentation();
        MacBackendNative::neuterDock(true);
        startLockdown();
    } else {
        // Re-raise the baseline home lock (kiosk + full shortcut blocker).
        MacBackendNative::setSystemDockHidden(false);
        applyBaselineKioskPosture();
    }
}

void MacBackend::restoreShellPlacement()
{
    // A routine ended: drop the exec blocker but keep FocusOS a locked shell on
    // the home screen — re-raise the baseline kiosk + shortcut blocker rather than
    // leaving the desktop fully open.
    stopLockdown();
    m_desktopAccessOpen = false;
    // Routine fully ended → back to the home screen: restore the normal Dock (the
    // home screen relies on the kiosk presentation to hide it while focused).
    MacBackendNative::setSystemDockHidden(false);
    applyBaselineKioskPosture();
}

void MacBackend::applyHomeScreenLock()
{
    // Fresh launch: launching FocusOS is itself the lock. Until now the locked
    // home posture (Dock hidden, Spotlight/Mission-Control/launcher blocker, no
    // Spaces) was only raised when a routine *ended* — at first launch the Dock
    // and Mission Control stayed reachable, which defeated the "looking to lock
    // in" intent. Raise the baseline lock now. If a crashed/killed routine is
    // about to be resumed, RoutineManager's deferred resumeActiveSessionIfPresent
    // runs after this (inside the event loop) and prepareRoutineSession() steps
    // the posture down to the routine layout, so this is safe to apply blindly.
    if (m_lockdownActive || m_desktopAccessOpen) {
        return;
    }
    applyBaselineKioskPosture();
}

void MacBackend::setAlwaysAllowedApps(const QStringList &commandLines)
{
    m_alwaysAllowedCommandLines = commandLines;
    if (m_lockdownActive && !m_desktopAccessOpen) {
        startLockdown();
    }
}

void MacBackend::applyAquaUiLockdown()
{
    QString error;
    if (!MacBackendNative::applyAquaUiLockdown(&error) && !error.isEmpty()) {
        qWarning() << error;
    }
}

void MacBackend::restoreAquaUiLockdown()
{
    MacBackendNative::restoreAquaUiLockdown();
}

void MacBackend::startWatchdog(const QString &binaryPath)
{
    if (binaryPath.isEmpty()) {
        return;
    }

    // The watchdog is a same-login crash recovery job, not a login item. Older
    // builds wrote it under ~/Library/LaunchAgents, which lets a stale active.json
    // auto-launch FocusOS after a reboot. Keep the job transient by bootstrapping a
    // plist from ~/.focusos, a directory launchd does not auto-load on login.
    removeLegacyWatchdogLaunchAgent();

    QDir().mkpath(focusosDataDir());
    const QString plistPath = watchdogPlistPath();
    const QString checkpointPath = QDir(focusosDataDir()).absoluteFilePath(QStringLiteral("active.json"));

    QSaveFile file(plistPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n"
        << "  <key>Label</key><string>com.focusos.watchdog</string>\n"
        << "  <key>ProgramArguments</key>\n"
        << "  <array><string>" << xmlEscaped(binaryPath) << "</string></array>\n"
        << "  <key>KeepAlive</key>\n"
        << "  <dict>\n"
        << "    <key>PathState</key>\n"
        << "    <dict><key>" << xmlEscaped(checkpointPath) << "</key><true/></dict>\n"
        << "  </dict>\n"
        << "  <key>RunAtLoad</key><false/>\n"
        << "  <key>ProcessType</key><string>Interactive</string>\n"
        << "</dict>\n"
        << "</plist>\n";
    if (!file.commit()) {
        return;
    }

    const QString domain = launchdGuiDomain();
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("bootout"), domain, plistPath});
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("bootstrap"), domain, plistPath});
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("kickstart"), QStringLiteral("-k"),
                       QStringLiteral("%1/com.focusos.watchdog").arg(domain)});
}

bool MacBackend::restoreLoginSessions(QString *errorMessage)
{
    if (errorMessage) {
        *errorMessage = QStringLiteral("macOS does not use FocusOS-managed alternate login sessions.");
    }
    return false;
}

void MacBackend::setDisplaySleepInhibited(bool inhibited)
{
    if (inhibited) {
        if (m_displayAssertionId != 0 || m_caffeinate.state() != QProcess::NotRunning) {
            return;
        }

        QString assertionError;
        if (MacBackendNative::createDisplaySleepAssertion(&m_displayAssertionId, &assertionError)) {
            return;
        }

        const QString caffeinate = QStringLiteral("/usr/bin/caffeinate");
        if (QFileInfo(caffeinate).isExecutable()) {
            m_caffeinate.start(caffeinate, {
                QStringLiteral("-d"),
                QStringLiteral("-w"),
                QString::number(QCoreApplication::applicationPid())
            });
        } else {
            qWarning() << assertionError;
        }
        return;
    }

    if (m_displayAssertionId != 0) {
        MacBackendNative::releaseDisplaySleepAssertion(m_displayAssertionId);
        m_displayAssertionId = 0;
    }

    if (m_caffeinate.state() != QProcess::NotRunning) {
        m_caffeinate.terminate();
        if (!m_caffeinate.waitForFinished(1000)) {
            m_caffeinate.kill();
            m_caffeinate.waitForFinished(200);
        }
    }
}

void MacBackend::releaseDisplaySleepInhibitors()
{
    // IOPM assertions are owned by the process that created them and disappear
    // on crash/exit, so there is no predecessor assertion to sweep. The
    // caffeinate fallback is launched with -w <FocusOS pid>, which also exits
    // when the owning FocusOS process dies.
    setDisplaySleepInhibited(false);
}

void MacBackend::quitBackgroundApps(const QStringList &allowedCommandLines)
{
    QStringList keepEntries = m_alwaysAllowedCommandLines;
    keepEntries.append(allowedCommandLines);
    keepEntries.removeDuplicates();

    qInfo() << "[engage] quitBackgroundApps: sweeping, keepEntries=" << keepEntries.size();
    const QStringList killed = MacBackendNative::sweepOtherApplications(
        bundleIdentifiersForCommandLines(keepEntries),
        processNamesForCommandLines(keepEntries),
        executablePathsForCommandLines(keepEntries),
        /*dryRun=*/false);

    // Leave an audit trail of exactly which GUI apps a strict engage closed —
    // matches LinuxBackend::quitBackgroundApps. Best-effort; never blocks engage.
    if (!killed.isEmpty()) {
        QFile log(QDir::homePath() + QStringLiteral("/.focusos/lockdown.log"));
        if (log.open(QIODevice::Append | QIODevice::Text)) {
            const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            log.write(QStringLiteral("%1 quitBackgroundApps terminate: %2\n")
                          .arg(stamp, killed.join(QStringLiteral(", ")))
                          .toUtf8());
        }
    }
}

QStringList MacBackend::previewBackgroundAppQuit(const QStringList &allowedCommandLines)
{
    QStringList keepEntries = m_alwaysAllowedCommandLines;
    keepEntries.append(allowedCommandLines);
    keepEntries.removeDuplicates();
    return MacBackendNative::sweepOtherApplications(
        bundleIdentifiersForCommandLines(keepEntries),
        processNamesForCommandLines(keepEntries),
        executablePathsForCommandLines(keepEntries),
        /*dryRun=*/true);
}

void MacBackend::freezeBackgroundProcesses()
{
    // DELIBERATELY a no-op on macOS. The deep-idle "app sleep" used to SIGSTOP every
    // regular GUI app (parity with the Linux freeze), but SIGSTOP-ing a macOS GUI
    // app that the WindowServer is still compositing/synchronizing with can wedge
    // the compositor — and stacking `pmset sleepnow` (the opt-in deep-sleep suspend)
    // on top of that left the machine beach-balling forever, because the only thing
    // that thaws the apps is a Qt input event that can no longer reach FocusOS once
    // the UI is stalled. That is the "computer keeps spinning" hang.
    //
    // The freeze buys nothing real on macOS anyway: the kernel already App-Naps idle
    // apps, and macOS sleep (display-sleep, and the opt-in pmset sleepnow) is
    // reliable on its own and recovers cleanly on input. So deep-idle on macOS is
    // now just: pause music + sleep the display (+ opt-in clean system sleep), with
    // no app SIGSTOP to deadlock against. m_frozenPids therefore stays empty and
    // thawBackgroundProcesses() has nothing to resume.
}

void MacBackend::thawBackgroundProcesses()
{
    // Resume exactly the pids we froze — never a blanket SIGCONT. With the SIGSTOP
    // freeze disabled above this is normally empty, but keep the safe resume so any
    // pids stopped by an older build (or a future opt-in freeze) are released.
    MacBackendNative::resumeProcesses(m_frozenPids);
    m_frozenPids.clear();
}

void MacBackend::lockScreen()
{
    // Blank the panel; with "require password after sleep" set this also locks.
    // The QML black overlay covers the rest, so this is best-effort.
    QProcess::startDetached(QStringLiteral("/usr/bin/pmset"),
                            {QStringLiteral("displaysleepnow")});
}

void MacBackend::unlockScreen()
{
    wakeDisplay();
}

void MacBackend::sleepDisplay()
{
    // Blank the panel without the in-app lock overlay — the monitor wakes on the
    // next input. macOS analog of the Linux DPMS-off.
    QProcess::startDetached(QStringLiteral("/usr/bin/pmset"),
                            {QStringLiteral("displaysleepnow")});
}

void MacBackend::wakeDisplay()
{
    // Force the panel back on after a deep-idle blank. caffeinate -u declares user
    // activity (the documented way to wake the display); a short timeout releases
    // the assertion immediately so it doesn't keep the screen awake afterwards.
    QProcess::startDetached(QStringLiteral("/usr/bin/caffeinate"),
                            {QStringLiteral("-u"), QStringLiteral("-t"), QStringLiteral("1")});
}

bool MacBackend::suspendSystem()
{
    // Opt-in whole-machine suspend for the deep-idle sleep. macOS sleep is
    // reliable (no S3/black-screen workaround needed), so this is a plain
    // pmset sleepnow. Best-effort: returns false if pmset can't be started.
    return QProcess::startDetached(QStringLiteral("/usr/bin/pmset"),
                                   {QStringLiteral("sleepnow")});
}

void MacBackend::runSessionStartupItems()
{
    // No Plasma-style autostart replay on macOS (launchd handles login items).
    // What this hook gives us is a guaranteed post-window-show moment to clean up
    // any persisted Dock/Aqua override from a crash before raising the home-screen
    // kiosk posture again. A resumed routine re-applies the Aqua lockdown
    // immediately via prepareRoutineSession().
    removeLegacyWatchdogLaunchAgent();
    restoreAquaUiLockdown();
    MacBackendNative::setSystemDockHidden(false);
    applyBaselineKioskPosture();
}

bool MacBackend::signOut(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    // Drop the firewall first so a sign-out never strands the machine behind pf.
    dropNetworkPolicy();
    // Lift the lockdown surfaces and tear down the respawn watchdog, otherwise a
    // loaded recovery job could relaunch FocusOS while the session is closing.
    endRoutineLockdown();
    const QString domain = launchdGuiDomain();
    const QString plistPath = watchdogPlistPath();
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("bootout"), domain, plistPath});
    removeLegacyWatchdogLaunchAgent();

    // Log out of the GUI session → back to the macOS login window. The raw
    // 'aevtrlgo' Apple event logs out immediately without a confirmation dialog
    // (the kiosk presentation would otherwise swallow the dialog). If osascript
    // can't run, fall back to quitting (the watchdog is already torn down).
    const bool loggedOut = QProcess::startDetached(
        QStringLiteral("/usr/bin/osascript"),
        {QStringLiteral("-e"),
         QStringLiteral("tell application \"loginwindow\" to «event aevtrlgo»")});
    if (!loggedOut) {
        QCoreApplication::quit();
    }
    return true;
}

bool MacBackend::persistentKioskEnabled() const
{
    // The next-login state is exactly "is the agent plist on disk": launchd
    // auto-loads ~/Library/LaunchAgents/*.plist at login, so the file's presence
    // is what makes FocusOS launch-at-login + KeepAlive-respawned.
    return QFileInfo::exists(loginAgentPlistPath());
}

bool MacBackend::setPersistentKiosk(bool enabled, QString *errorMessage)
{
    const QString plistPath = loginAgentPlistPath();
    const QString flagPath = loginAgentKeepAlivePath();

    if (!enabled) {
        // Removing the KeepAlive flag stops respawn for the CURRENT session too —
        // launchd re-evaluates the PathState gate when FocusOS next exits, sees the
        // file gone, and does not relaunch. No `bootout` (which would SIGTERM the
        // running app) is needed. Also boot the job out to clean up any LEGACY agent
        // that used unconditional KeepAlive=true (it ignores the flag); the running
        // app survives because, if it is the agent's instance, bootout only removes
        // the KeepAlive contract — FocusOS keeps running until the user quits it.
        QFile::remove(flagPath);
        if (QFile::exists(plistPath) && !QFile::remove(plistPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not remove %1").arg(plistPath);
            }
            return false;
        }
        QProcess::execute(QStringLiteral("/bin/launchctl"),
                          {QStringLiteral("bootout"),
                           QStringLiteral("%1/%2").arg(launchdGuiDomain(), kLoginAgentLabel)});
        return true;
    }

    // The binary launchd should relaunch. canonicalFilePath resolves symlinks so it
    // matches the real app binary; that binary self-elevates via `sudo -n` when the
    // NOPASSWD rule is installed (execv preserves the PID, so KeepAlive still tracks
    // it after the elevation re-exec).
    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const QString binaryPath = self.isEmpty() ? QCoreApplication::applicationFilePath() : self;

    QDir().mkpath(QFileInfo(plistPath).absolutePath());
    QSaveFile file(plistPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write %1").arg(plistPath);
        }
        return false;
    }
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n"
        << "  <key>Label</key><string>" << kLoginAgentLabel << "</string>\n"
        << "  <key>ProgramArguments</key>\n"
        << "  <array><string>" << xmlEscaped(binaryPath) << "</string></array>\n"
        << "  <key>RunAtLoad</key><true/>\n"
        // KeepAlive ONLY while the flag file exists (PathState), never unconditional —
        // so the respawn lock can be lifted instantly by deleting the flag (authorized
        // quit / disable) without booting out the job.
        << "  <key>KeepAlive</key>\n"
        << "  <dict>\n"
        << "    <key>PathState</key>\n"
        << "    <dict><key>" << xmlEscaped(flagPath) << "</key><true/></dict>\n"
        << "  </dict>\n"
        << "  <key>ProcessType</key><string>Interactive</string>\n"
        << "</dict>\n"
        << "</plist>\n";
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not save %1").arg(plistPath);
        }
        return false;
    }

    // Arm the KeepAlive gate now so the lock is live as soon as the agent loads.
    ensureKioskRespawnArmed();

    // Deliberately NOT bootstrapped now. Bootstrapping a RunAtLoad job in this live
    // session would start a SECOND FocusOS, which hits the single-instance lock and
    // exits — and KeepAlive would then throttle-respawn that doomed instance every
    // ~10s. Instead launchd auto-loads the agent at the NEXT login, where it owns
    // the one true instance. So enabling ARMS launch-at-login + un-quittable for the
    // next login; it never spawns a duplicate now. Re-`enable` in case a prior
    // disable left the label flagged off.
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("enable"),
                       QStringLiteral("%1/%2").arg(launchdGuiDomain(), kLoginAgentLabel)});
    return true;
}

void MacBackend::ensureKioskRespawnArmed()
{
    // Re-create the KeepAlive flag on every launch while the agent is installed, so
    // a fresh login (RunAtLoad) or a respawn comes back un-quittable. Only when the
    // plist exists — otherwise the kiosk is disabled and we must not re-arm it.
    if (!QFileInfo::exists(loginAgentPlistPath())) {
        return;
    }
    const QString flagPath = loginAgentKeepAlivePath();
    QDir().mkpath(QFileInfo(flagPath).absolutePath());
    if (!QFileInfo::exists(flagPath)) {
        QFile flag(flagPath);
        if (flag.open(QIODevice::WriteOnly | QIODevice::Text)) {
            flag.write("1\n");
        }
    }
}

void MacBackend::prepareForAuthorizedQuit()
{
    // The 6-digit-gated quit. Just lift the KeepAlive gate (delete the flag) and the
    // crash watchdog — then the caller's QCoreApplication::quit() exits cleanly and
    // launchd does NOT respawn (PathState gate is now false). No bootout / SIGTERM
    // race. The login agent plist stays installed, so FocusOS still launches at the
    // next login (where ensureKioskRespawnArmed re-arms the lock).
    QFile::remove(loginAgentKeepAlivePath());
    bootoutWatchdog(watchdogPlistPath());
    removeLegacyWatchdogLaunchAgent();
    // Belt-and-suspenders for any legacy unconditional-KeepAlive agent still loaded
    // from a previous login (it ignores the flag): boot it out so it can't respawn.
    QProcess::execute(QStringLiteral("/bin/launchctl"),
                      {QStringLiteral("bootout"),
                       QStringLiteral("%1/%2").arg(launchdGuiDomain(), kLoginAgentLabel)});
}

void MacBackend::setMissionControlDisabled(bool disabled)
{
    if (m_missionControlDisabled == disabled) {
        return;  // already in the requested state — don't churn `killall Dock`
    }
    m_missionControlDisabled = disabled;
    MacBackendNative::setMissionControlDisabled(disabled);
}

void MacBackend::applyBaselineKioskPosture()
{
    QString presentationError;
    if (!MacBackendNative::enterKioskPresentation(&presentationError)) {
        qWarning() << presentationError;
    }
    // Home screen is the locked shell: block EVERYTHING (allowNavigation=false) so
    // there's no Spotlight / Mission Control / switcher route off it.
    QString inputError;
    if (!MacBackendNative::startInputBlocker(/*allowNavigation=*/false, &inputError) && !m_inputBlockerWarned) {
        m_inputBlockerWarned = true;
        qWarning() << inputError;
    }
    // The key blocker can't stop the trackpad swipe-up into Mission Control, so the
    // locked home screen ALSO hard-disables Mission Control / Spaces outright. This
    // is what makes "session ended → locked until the 6-digit code" actually hold:
    // the only thing that re-enables it is a routine engaging or the user unlocking
    // the desktop with their code (launchDesktopShell).
    setMissionControlDisabled(true);
}

void MacBackend::startLockdown()
{
    qInfo() << "[engage] startLockdown: begin, desktopAccessOpen=" << m_desktopAccessOpen;
    m_lockdownActive = true;
    if (m_desktopAccessOpen) {
        return;
    }

    // NOTE: we intentionally do NOT applyAquaUiLockdown() during a routine. That
    // SIP-off path disables the Dock / Mission Control launchd agents, which would
    // make it impossible to swipe between FocusOS's fullscreen Space and the
    // routine's app windows — the whole point of the new routine layout. Focus is
    // enforced instead by the launch-watcher (kills any disallowed app the instant
    // it launches), the network lock, and the neutered Dock. The Aqua-lockdown
    // machinery stays available, and restoreAquaUiLockdown() is still called on the
    // teardown paths to revive anything a previous/legacy build left disabled.

    // Swallow the launch/escape shortcuts (Spotlight, Launchpad, screenshots, the
    // Dock toggle, force-quit) but LET the navigation shortcuts (Mission Control,
    // Spaces, ⌘-Tab) through so the user can reach the routine apps. Needs
    // Accessibility access to actually suppress; warn once if it can't.
    QString inputError;
    if (!MacBackendNative::startInputBlocker(/*allowNavigation=*/true, &inputError) && !m_inputBlockerWarned) {
        m_inputBlockerWarned = true;
        qWarning() << inputError;
    }

    QStringList allowedNames = processNamesForCommandLines(allowedCommandLines());
    QStringList allowedBundleIdentifiers = bundleIdentifiersForCommandLines(allowedCommandLines());
    QStringList allowedExecutablePaths = executablePathsForCommandLines(allowedCommandLines());

    const QString selfPath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    allowedExecutablePaths.append(selfPath);
    allowedNames.append(QFileInfo(selfPath).fileName());

    for (const LaunchedProcess &process : std::as_const(m_launchedProcesses)) {
        if (!process.displayName.isEmpty()) {
            allowedNames.append(process.displayName);
        }
        if (!process.bundleIdentifier.isEmpty()) {
            allowedBundleIdentifiers.append(process.bundleIdentifier);
        }
        if (!process.executablePath.isEmpty()) {
            allowedExecutablePaths.append(process.executablePath);
        }
    }

    QStringList blockedNames = defaultBlockedProcessNames();
    QStringList blockedBundleIdentifiers = defaultBlockedBundleIdentifiers();
    for (const QString &name : allowedNames) {
        blockedNames.removeAll(name);
    }
    for (const QString &identifier : allowedBundleIdentifiers) {
        blockedBundleIdentifiers.removeAll(identifier);
    }
    blockedNames.removeDuplicates();
    blockedBundleIdentifiers.removeDuplicates();
    allowedNames.removeDuplicates();
    allowedBundleIdentifiers.removeDuplicates();
    allowedExecutablePaths.removeDuplicates();

    QString error;
    if (!MacBackendNative::startExecBlocker(blockedNames,
                                            blockedBundleIdentifiers,
                                            allowedNames,
                                            allowedBundleIdentifiers,
                                            allowedExecutablePaths,
                                            &error)) {
        qWarning() << error;
    }

    // Userland fallback for the (entitlement-gated) exec-blocker: reap blocked
    // apps the instant they launch — Apple Music from the media key, Dock launches
    // of Spotify/Discord/a terminal — so the lock holds even in an adhoc build.
    // Same blocklist/allowlist the exec-blocker uses; safe alongside it.
    qInfo() << "[engage] startLockdown: startLaunchWatcher";
    MacBackendNative::startLaunchWatcher(blockedNames,
                                         blockedBundleIdentifiers,
                                         allowedNames,
                                         allowedBundleIdentifiers,
                                         allowedExecutablePaths);
    qInfo() << "[engage] startLockdown: done";
}

void MacBackend::stopLockdown()
{
    m_lockdownActive = false;
    restoreAquaUiLockdown();
    MacBackendNative::stopExecBlocker();
    MacBackendNative::stopLaunchWatcher();
    MacBackendNative::stopInputBlocker();
    m_sessionAppEntries.clear();
}

void MacBackend::endRoutineLockdown()
{
    // Stand down the exec blocker + key blocker WITHOUT touching the routine's
    // apps (they stay open for the finish prompt / admin desktop), and restore
    // the normal presentation so the "Access Desktop" path — Spotlight, Dock,
    // launchers — works again. Mirrors LinuxBackend::endRoutineLockdown stopping
    // the launcher-killing sweep. The pf network lock (if any) is dropped
    // separately by RoutineManager.
    m_lockdownActive = false;
    restoreAquaUiLockdown();
    MacBackendNative::stopExecBlocker();
    MacBackendNative::stopLaunchWatcher();
    MacBackendNative::stopInputBlocker();
    MacBackendNative::setSystemDockHidden(false);
    MacBackendNative::leaveKioskPresentation();
    // Generic stand-down (sign-out, or lifting the lock so the completion prompt /
    // Access-Desktop path can run): restore Mission Control. On the routine-end path
    // restoreShellPlacement() immediately re-raises the locked baseline (which
    // re-disables it), so the home screen still ends up locked; sign-out leaves it
    // genuinely restored.
    setMissionControlDisabled(false);
}

void MacBackend::killTrackedProcesses(const QStringList &entries)
{
    for (auto it = m_launchedProcesses.begin(); it != m_launchedProcesses.end();) {
        if (it->pid <= 0 || !matchesEntry(*it, entries)) {
            ++it;
            continue;
        }

        ::kill(static_cast<pid_t>(it->pid), SIGTERM);
        it = m_launchedProcesses.erase(it);
    }
}

QStringList MacBackend::allowedCommandLines() const
{
    QStringList allowed = m_alwaysAllowedCommandLines;
    allowed.append(m_sessionAppEntries);
    allowed.removeDuplicates();
    return allowed;
}
