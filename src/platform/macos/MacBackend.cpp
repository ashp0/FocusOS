#include "platform/macos/MacBackend.h"

#include "blocker/BlockerPolicy.h"
#include "platform/macos/MacBackendNative.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamWriter>

#include <csignal>
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

MacBackend::MacBackend() = default;

MacBackend::~MacBackend()
{
    stopLockdown();
    setDisplaySleepInhibited(false);
}

QString MacBackend::name() const
{
    return QStringLiteral("macOS");
}

void MacBackend::prepareRoutineSession(const QStringList &appPaths)
{
    m_sessionAppEntries = appPaths;
    m_desktopAccessOpen = false;

    QString presentationError;
    if (!MacBackendNative::enterKioskPresentation(&presentationError)) {
        qWarning() << presentationError;
    }

    startLockdown();
    closeDistractingApplications();
}

bool MacBackend::launchApps(const QStringList &appPaths, QString *errorMessage)
{
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

    if (m_lockdownActive) {
        startLockdown();
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
    return true;
}

void MacBackend::dropNetworkPolicy()
{
    BlockerPolicy::write(false, {});
    MacBackendNative::dropNetworkFilter();
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
    closeDistractingApplications();
    m_desktopAccessOpen = false;
    if (m_lockdownActive) {
        QString presentationError;
        if (!MacBackendNative::enterKioskPresentation(&presentationError)) {
            qWarning() << presentationError;
        }
        startLockdown();
    }
}

bool MacBackend::launchDesktopShell(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    m_desktopAccessOpen = true;
    MacBackendNative::stopExecBlocker();
    MacBackendNative::leaveKioskPresentation();

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
    QString presentationError;
    if (!MacBackendNative::enterKioskPresentation(&presentationError)) {
        qWarning() << presentationError;
    }
    if (m_lockdownActive) {
        startLockdown();
    }
}

void MacBackend::restoreShellPlacement()
{
    stopLockdown();
    m_desktopAccessOpen = false;
    MacBackendNative::leaveKioskPresentation();
}

void MacBackend::setAlwaysAllowedApps(const QStringList &commandLines)
{
    m_alwaysAllowedCommandLines = commandLines;
    if (m_lockdownActive && !m_desktopAccessOpen) {
        startLockdown();
    }
}

void MacBackend::startWatchdog(const QString &binaryPath)
{
    if (binaryPath.isEmpty()) {
        return;
    }

    const QString launchAgentsDir = QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
    QDir().mkpath(launchAgentsDir);
    const QString plistPath = QDir(launchAgentsDir).absoluteFilePath(QStringLiteral("com.focusos.watchdog.plist"));
    const QString checkpointPath = QDir::homePath() + QStringLiteral("/.focusos/active.json");

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

    const QString domain = QStringLiteral("gui/%1").arg(getuid());
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

void MacBackend::startLockdown()
{
    m_lockdownActive = true;
    if (m_desktopAccessOpen) {
        return;
    }

    QStringList allowedNames = processNamesForCommandLines(allowedCommandLines());
    QStringList allowedBundleIdentifiers = bundleIdentifiersForCommandLines(allowedCommandLines());
    QStringList allowedExecutablePaths = executablePathsForCommandLines(allowedCommandLines());

    const QString selfPath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    allowedExecutablePaths.append(selfPath);
    allowedNames.append(QFileInfo(selfPath).fileName());

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
}

void MacBackend::stopLockdown()
{
    m_lockdownActive = false;
    MacBackendNative::stopExecBlocker();
    m_sessionAppEntries.clear();
}

void MacBackend::closeDistractingApplications() const
{
    QStringList blockedNames = defaultBlockedProcessNames();
    QStringList blockedBundleIdentifiers = defaultBlockedBundleIdentifiers();
    const QStringList allowedNames = processNamesForCommandLines(allowedCommandLines());
    const QStringList allowedBundleIdentifiers = bundleIdentifiersForCommandLines(allowedCommandLines());

    for (const QString &name : allowedNames) {
        blockedNames.removeAll(name);
    }
    for (const QString &identifier : allowedBundleIdentifiers) {
        blockedBundleIdentifiers.removeAll(identifier);
    }

    blockedNames.removeDuplicates();
    blockedBundleIdentifiers.removeDuplicates();
    MacBackendNative::terminateApplications(blockedBundleIdentifiers, blockedNames, {});
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
