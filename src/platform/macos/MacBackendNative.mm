#include "platform/macos/MacBackendNative.h"

#include "blocker/BlockerPolicy.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#include <atomic>
#include <csignal>

#if defined(FOCUSOS_HAS_ENDPOINT_SECURITY) && __has_include(<EndpointSecurity/EndpointSecurity.h>)
#define FOCUSOS_ENDPOINT_SECURITY_AVAILABLE 1
#import <EndpointSecurity/EndpointSecurity.h>
#else
#define FOCUSOS_ENDPOINT_SECURITY_AVAILABLE 0
#endif

#include <QAbstractSocket>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <pwd.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

NSString *toNSString(const QString &value)
{
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString fromNSString(NSString *value)
{
    if (!value) {
        return {};
    }
    return QString::fromUtf8([value UTF8String]);
}

NSArray<NSString *> *toNSArray(const QStringList &values)
{
    NSMutableArray<NSString *> *array = [NSMutableArray arrayWithCapacity:values.size()];
    for (const QString &value : values) {
        [array addObject:toNSString(value)];
    }
    return array;
}

NSString *standardizedPath(NSString *path)
{
    return path.length > 0 ? [path stringByStandardizingPath] : path;
}

NSBundle *bundleForPath(const QString &bundlePath)
{
    return [NSBundle bundleWithPath:standardizedPath(toNSString(bundlePath))];
}

QString displayNameForBundle(NSBundle *bundle, const QString &bundlePath)
{
    if (bundle) {
        NSString *displayName = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
        if (displayName.length == 0) {
            displayName = [bundle objectForInfoDictionaryKey:@"CFBundleName"];
        }
        if (displayName.length > 0) {
            return fromNSString(displayName);
        }
    }

    NSString *name = [[NSFileManager defaultManager] displayNameAtPath:toNSString(bundlePath)];
    if (name.length > 0) {
        return fromNSString([name stringByDeletingPathExtension]);
    }
    return QFileInfo(bundlePath).completeBaseName();
}

NSString *applicationBundlePathForExecutable(NSString *executablePath)
{
    NSString *path = standardizedPath(executablePath);
    while (path.length > 1) {
        if ([[path pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame) {
            return path;
        }
        NSString *parent = [path stringByDeletingLastPathComponent];
        if ([parent isEqualToString:path]) {
            break;
        }
        path = parent;
    }
    return nil;
}

// ---------------------------------------------------------------------------
// pf-based outbound allowlist (the macOS analog of the Linux NetGate nftables
// firewall). The Network Extension content filter was the "correct" macOS API,
// but it needs a System Extension + the Network Extension entitlement + an
// approval dance that is impractical without an Apple Developer account. pf
// (`pfctl`) is built into macOS, needs only root, and matches NetGate's model:
// block all egress except DNS, loopback, and the resolved allowlist addresses.
// FocusOS already runs as root for the Endpoint Security exec-blocker, so the
// same privilege covers the firewall.

const QString kPfctl = QStringLiteral("/sbin/pfctl");
const QString kApplePfConf = QStringLiteral("/etc/pf.conf");

QString consoleUserName()
{
    const QByteArray sudoUser = qgetenv("SUDO_USER");
    if (geteuid() == 0 && !sudoUser.isEmpty() && sudoUser != "root") {
        return QString::fromLocal8Bit(sudoUser);
    }
    if (const struct passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return {};
}

uint consoleUserId()
{
    bool ok = false;
    const uint sudoUid = qEnvironmentVariable("SUDO_UID").toUInt(&ok);
    if (geteuid() == 0 && ok && sudoUid > 0) {
        return sudoUid;
    }
    return static_cast<uint>(getuid());
}

QString consoleHomePath()
{
    const QString user = consoleUserName();
    if (!user.isEmpty()) {
        if (const struct passwd *pw = getpwnam(user.toLocal8Bit().constData())) {
            if (pw->pw_dir && pw->pw_dir[0] != '\0') {
                return QString::fromLocal8Bit(pw->pw_dir);
            }
        }
    }
    const QByteArray home = qgetenv("HOME");
    if (!home.isEmpty()) {
        return QString::fromLocal8Bit(home);
    }
    return QDir::homePath();
}

QString focusosDataDir()
{
    return QDir(consoleHomePath()).absoluteFilePath(QStringLiteral(".focusos"));
}

bool hostMatchesAny(const QString &host, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (host == needle || host.endsWith(QLatin1Char('.') + needle)) {
            return true;
        }
    }
    return false;
}

// Companion domains + stable CIDR netblocks implied by a primary allowed host.
// Kept in sync with the Linux NetGate::serviceCompanions list — YouTube is the
// canonical case where pinning only youtube.com's IPs loads the page but the
// player spins, because the video bytes stream from per-session googlevideo
// hostnames we can't resolve ahead of time.
QStringList serviceCompanions(const QString &host)
{
    static const QStringList googleHostRoots {
        QStringLiteral("youtube.com"), QStringLiteral("youtu.be"),
        QStringLiteral("youtube-nocookie.com"), QStringLiteral("ytimg.com"),
        QStringLiteral("googlevideo.com"), QStringLiteral("google.com"),
        QStringLiteral("gstatic.com")
    };
    if (!hostMatchesAny(host, googleHostRoots)) {
        return {};
    }
    return QStringList {
        QStringLiteral("www.youtube.com"), QStringLiteral("m.youtube.com"),
        QStringLiteral("youtubei.googleapis.com"), QStringLiteral("yt3.ggpht.com"),
        QStringLiteral("ggpht.com"), QStringLiteral("i.ytimg.com"),
        QStringLiteral("s.ytimg.com"), QStringLiteral("ytimg.com"),
        QStringLiteral("googlevideo.com"), QStringLiteral("googleapis.com"),
        QStringLiteral("gstatic.com"), QStringLiteral("www.gstatic.com"),
        QStringLiteral("fonts.gstatic.com"), QStringLiteral("play.google.com"),
        QStringLiteral("accounts.google.com"), QStringLiteral("accounts.youtube.com"),
        // Google's stable IPv4 netblocks — googlevideo edge servers live here.
        QStringLiteral("64.233.160.0/19"), QStringLiteral("66.102.0.0/20"),
        QStringLiteral("66.249.64.0/19"), QStringLiteral("72.14.192.0/18"),
        QStringLiteral("74.125.0.0/16"), QStringLiteral("108.177.0.0/17"),
        QStringLiteral("142.250.0.0/15"), QStringLiteral("172.217.0.0/16"),
        QStringLiteral("172.253.0.0/16"), QStringLiteral("173.194.0.0/16"),
        QStringLiteral("209.85.128.0/17"), QStringLiteral("216.58.192.0/19"),
        QStringLiteral("216.239.32.0/19"),
        // Google's IPv6 netblocks.
        QStringLiteral("2001:4860::/32"), QStringLiteral("2404:6800::/32"),
        QStringLiteral("2607:f8b0::/32"), QStringLiteral("2800:3f0::/32"),
        QStringLiteral("2a00:1450::/32"), QStringLiteral("2c0f:fb50::/32")
    };
}

// Build a pf ruleset that drops all outbound traffic except loopback, DNS, and
// the resolved allowlist. Does the slow DNS resolution inline (callers run the
// engage network step off the GUI thread). Mirrors NetGate::buildRuleset.
QString buildPfRuleset(const QStringList &allowedHosts)
{
    QSet<QString> v4;  // plain addresses and CIDR ranges
    QSet<QString> v6;

    const auto insertAddress = [&v4, &v6](QHostAddress address) {
        if (address.isNull()) {
            return;
        }
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            v4.insert(address.toString());
            return;
        }
        if (address.protocol() != QAbstractSocket::IPv6Protocol) {
            return;
        }
        address.setScopeId(QString());
        QString text = address.toString();
        const int zone = text.indexOf(QLatin1Char('%'));
        if (zone >= 0) {
            text = text.left(zone);
        }
        if (text.contains(QLatin1Char('.'))) {  // IPv4-mapped — fold back to v4
            bool ok = false;
            const quint32 mapped = address.toIPv4Address(&ok);
            if (ok) {
                v4.insert(QHostAddress(mapped).toString());
            }
            return;
        }
        v6.insert(text);
    };

    // Expand companions, dedupe preserving order.
    QStringList expanded;
    QSet<QString> seen;
    const auto addEntry = [&expanded, &seen](const QString &raw) {
        const QString t = raw.trimmed();
        if (!t.isEmpty() && !seen.contains(t)) {
            seen.insert(t);
            expanded.append(t);
        }
    };
    for (const QString &entry : allowedHosts) {
        addEntry(entry);
        QString host = entry.trimmed().toLower();
        const QUrl url = QUrl::fromUserInput(host);
        if (!url.host().isEmpty()) {
            host = url.host().toLower();
        }
        for (const QString &companion : serviceCompanions(host)) {
            addEntry(companion);
        }
    }

    for (const QString &entry : expanded) {
        QString host = entry.trimmed().toLower();
        if (host.isEmpty()) {
            continue;
        }

        // A genuine CIDR passes straight into the pf table; a URL also contains
        // '/', so validate as a subnet first before treating the slash as a path.
        if (host.contains(QLatin1Char('/'))) {
            const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(host);
            if (!subnet.first.isNull() && subnet.second >= 0) {
                if (subnet.first.protocol() == QAbstractSocket::IPv6Protocol) {
                    v6.insert(host);
                } else {
                    v4.insert(host);
                }
                continue;
            }
        }

        const QUrl url = QUrl::fromUserInput(host);
        if (!url.host().isEmpty()) {
            host = url.host().toLower();
        } else {
            host = host.section(QLatin1Char('/'), 0, 0).section(QLatin1Char(':'), 0, 0);
        }

        const QHostAddress literal(host);
        if (!literal.isNull()) {
            insertAddress(literal);
            continue;
        }

        // Resolve the host and its www. counterpart (redirect targets often live
        // on a different CDN — see the qt.io note in NetGate).
        QStringList names { host };
        if (host.startsWith(QStringLiteral("www."))) {
            names.append(host.mid(4));
        } else {
            names.append(QStringLiteral("www.") + host);
        }
        for (const QString &name : names) {
            const QHostInfo info = QHostInfo::fromName(name);
            for (const QHostAddress &address : info.addresses()) {
                insertAddress(address);
            }
        }
    }

    const auto sortedJoin = [](const QSet<QString> &set) {
        QStringList list(set.begin(), set.end());
        list.sort();
        return list.join(QStringLiteral(", "));
    };

    QString rules;
    rules += QStringLiteral("# FocusOS network lock (pf) — generated, do not edit by hand\n");
    rules += QStringLiteral("set block-policy drop\n");
    rules += QStringLiteral("set skip on lo0\n");
    if (!v4.isEmpty()) {
        rules += QStringLiteral("table <focusos4> persist { %1 }\n").arg(sortedJoin(v4));
    }
    if (!v6.isEmpty()) {
        rules += QStringLiteral("table <focusos6> persist { %1 }\n").arg(sortedJoin(v6));
    }
    rules += QStringLiteral("block drop out all\n");
    rules += QStringLiteral("pass out proto { tcp udp } to any port 53 keep state\n");
    if (!v4.isEmpty()) {
        rules += QStringLiteral("pass out inet to <focusos4> keep state\n");
    }
    if (!v6.isEmpty()) {
        rules += QStringLiteral("pass out inet6 to <focusos6> keep state\n");
    }
    rules += QStringLiteral("# Allowlist requested by the routine:\n");
    for (const QString &host : allowedHosts) {
        rules += QStringLiteral("#   %1\n").arg(host);
    }
    return rules;
}

// Run pfctl with the given args, capturing a human-readable error. Translates
// the unprivileged failure (no passwordless pfctl rule) into actionable text.
//
// pfctl needs root, but we deliberately do NOT run the whole GUI app as root:
// a sudo-launched root process loses its TCC identity, which re-breaks the
// Accessibility grant the CGEventTap blocker relies on. Instead the app stays
// the normal user and drives ONLY pfctl through a NOPASSWD sudoers rule scoped
// to /sbin/pfctl (installed by SystemStatus::enableElevatedLaunch). `sudo -n`
// never prompts, so a missing rule fails fast here rather than hanging the UI.
// `stdinData` feeds `pfctl -f -` so the ruleset never has to be written to a
// root-owned file.
bool runPfctl(const QStringList &arguments, QString *errorMessage,
              const QByteArray &stdinData = {})
{
    const bool root = (geteuid() == 0);
    QProcess pfctl;
    pfctl.setProcessChannelMode(QProcess::MergedChannels);
    if (root) {
        pfctl.start(kPfctl, arguments);
    } else {
        QStringList sudoArgs { QStringLiteral("-n"), kPfctl };
        sudoArgs += arguments;
        pfctl.start(QStringLiteral("/usr/bin/sudo"), sudoArgs);
    }
    if (!pfctl.waitForStarted(3000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to start /sbin/pfctl");
        }
        return false;
    }
    if (!stdinData.isEmpty()) {
        pfctl.write(stdinData);
    }
    pfctl.closeWriteChannel();
    if (!pfctl.waitForFinished(5000)) {
        pfctl.kill();
        pfctl.waitForFinished(200);
        if (errorMessage) {
            *errorMessage = QStringLiteral("pfctl did not finish within 5 seconds");
        }
        return false;
    }
    // pfctl prints informational lines ("pf enabled") on success and exits 0;
    // "ALTQ" warnings are noise. Treat a non-zero exit as the real failure.
    if (pfctl.exitStatus() != QProcess::NormalExit || pfctl.exitCode() != 0) {
        QString output = QString::fromUtf8(pfctl.readAll()).trimmed();
        // `sudo -n` with no matching NOPASSWD rule prints "a password is required"
        // / "sudo: ..." and never runs pfctl — that's the "I enabled it but it
        // still fails" case: the rule was never installed (or names a different
        // user/binary).
        const bool noSudoRule = !root &&
            (output.contains(QStringLiteral("password is required"), Qt::CaseInsensitive) ||
             output.contains(QStringLiteral("a terminal is required"), Qt::CaseInsensitive) ||
             output.contains(QStringLiteral("sudo:"), Qt::CaseInsensitive));
        const bool unprivileged = noSudoRule ||
            output.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive) ||
            output.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive) ||
            output.contains(QStringLiteral("/dev/pf"), Qt::CaseInsensitive);
        if (errorMessage) {
            if (unprivileged) {
                *errorMessage = QStringLiteral(
                    "FocusOS can't drive the pf firewall yet. Open Settings → SYSTEM, "
                    "enter your Mac admin password and tap \"Enable passwordless "
                    "firewall\" — that grants FocusOS passwordless pfctl access. No "
                    "restart needed; engage the routine again afterwards.");
            } else {
                *errorMessage = output.isEmpty()
                    ? QStringLiteral("pfctl refused the ruleset (unknown error)")
                    : output.left(400);
            }
        }
        return false;
    }
    return true;
}

void performOnMainThreadSync(void (^block)(void))
{
    if ([NSThread isMainThread]) {
        block();
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), block);
}

// Resolve a QWindow winId() (an NSView* on macOS, occasionally an NSWindow*) to
// its NSWindow. Must be called on the main thread.
NSWindow *windowFromNsView(void *nsView)
{
    if (!nsView) {
        return nil;
    }
    id object = (__bridge id)nsView;
    if ([object isKindOfClass:[NSView class]]) {
        return [(NSView *)object window];
    }
    if ([object isKindOfClass:[NSWindow class]]) {
        return (NSWindow *)object;
    }
    return nil;
}

void runAsConsoleUser(const QString &program, const QStringList &args)
{
    const bool root = (geteuid() == 0);
    const QString consoleUser = consoleUserName();
    const bool viaUser = root && !consoleUser.isEmpty() && consoleUser != QStringLiteral("root");

    if (viaUser) {
        QStringList full {QStringLiteral("-H"), QStringLiteral("-u"), consoleUser, program};
        full += args;
        QProcess::execute(QStringLiteral("/usr/bin/sudo"), full);
        return;
    }
    QProcess::execute(program, args);
}

QStringList aquaUiLockdownLabels()
{
    return {
        QStringLiteral("com.apple.Dock.agent"),
        QStringLiteral("com.apple.Finder"),
        QStringLiteral("com.apple.Spotlight"),
        QStringLiteral("com.apple.SystemUIServer.agent"),
        QStringLiteral("com.apple.controlcenter"),
        QStringLiteral("com.apple.notificationcenterui.agent"),
        QStringLiteral("com.apple.Siri.agent"),
        QStringLiteral("com.apple.talagent")
    };
}

QString aquaStatePath()
{
    return QDir(focusosDataDir()).absoluteFilePath(QStringLiteral("macos-ui-lockdown.state"));
}

QString aquaRestoreScriptPathInternal()
{
    return QDir(focusosDataDir()).absoluteFilePath(QStringLiteral("restore-macos-ui.sh"));
}

QString launchdGuiDomain()
{
    return QStringLiteral("gui/%1").arg(consoleUserId());
}

bool runLaunchctl(const QStringList &arguments, QString *output = nullptr, int timeoutMs = 5000)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("/bin/launchctl"), arguments);
    if (!process.waitForStarted(2000)) {
        if (output) {
            *output = QStringLiteral("Unable to start launchctl");
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(200);
        if (output) {
            *output = QStringLiteral("launchctl timed out");
        }
        return false;
    }
    if (output) {
        *output = QString::fromLocal8Bit(process.readAll()).trimmed();
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// Map a system UI launchd label to its LaunchAgent plist on disk. The filename is
// usually the label with any trailing ".agent" stripped (com.apple.Dock.agent ->
// com.apple.Dock.plist), but a few keep the full label (com.apple.Siri.agent ->
// com.apple.Siri.agent.plist), so try the literal label first, then the stripped
// form, and return whichever exists.
QString systemAgentPlistPath(const QString &label)
{
    const QString dir = QStringLiteral("/System/Library/LaunchAgents/");
    QStringList candidates;
    candidates << dir + label + QStringLiteral(".plist");
    if (label.endsWith(QStringLiteral(".agent"))) {
        candidates << dir + label.left(label.size() - 6) + QStringLiteral(".plist");
    }
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

// Whether a launchd label is currently loaded (bootstrapped) in the user's GUI
// domain. A `bootout` leaves the label "enabled" but NOT loaded, and `kickstart`
// alone cannot revive it — only `bootstrap` reloads it. This lets the restore path
// tell "needs reviving" from "already healthy" so it never restarts a live agent.
bool launchdLabelLoaded(const QString &label)
{
    QString discard;  // drain the (sometimes large) print output so the pipe can't fill
    return runLaunchctl({QStringLiteral("print"),
                         QStringLiteral("%1/%2").arg(launchdGuiDomain(), label)},
                        &discard, 4000);
}

bool launchdLabelDisabled(const QString &label)
{
    QString output;
    if (!runLaunchctl({QStringLiteral("print-disabled"), launchdGuiDomain()}, &output)) {
        return false;
    }

    const QString escaped = QRegularExpression::escape(label);
    const QRegularExpression pattern(QStringLiteral("\"%1\"\\s*=>\\s*([A-Za-z0-9_-]+)").arg(escaped));
    const QRegularExpressionMatch match = pattern.match(output);
    if (!match.hasMatch()) {
        return false;
    }
    const QString value = match.captured(1).toLower();
    return value == QStringLiteral("true") || value == QStringLiteral("disabled");
}

QStringList readAquaState()
{
    QFile file(aquaStatePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList labels;
    while (!file.atEnd()) {
        const QString label = QString::fromUtf8(file.readLine()).trimmed();
        if (!label.isEmpty() && !label.startsWith(QLatin1Char('#'))) {
            labels.append(label);
        }
    }
    labels.removeDuplicates();
    return labels;
}

bool writeAquaRestoreScript(const QStringList &labels, QString *errorMessage)
{
    QDir().mkpath(focusosDataDir());

    QSaveFile state(aquaStatePath());
    if (!state.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write %1").arg(aquaStatePath());
        }
        return false;
    }
    for (const QString &label : labels) {
        state.write(label.toUtf8());
        state.write("\n");
    }
    if (!state.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to save %1").arg(aquaStatePath());
        }
        return false;
    }

    QSaveFile script(aquaRestoreScriptPathInternal());
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write %1").arg(aquaRestoreScriptPathInternal());
        }
        return false;
    }
    const QString uid = QString::number(consoleUserId());
    script.write("#!/bin/sh\n");
    script.write("set +e\n");
    script.write(QStringLiteral("UID_VALUE='%1'\n").arg(uid).toUtf8());
    script.write(QStringLiteral("STATE='%1'\n").arg(aquaStatePath().replace(QLatin1Char('\''), QStringLiteral("'\\''"))).toUtf8());
    script.write("if [ -f \"$STATE\" ]; then\n");
    script.write("  while IFS= read -r label; do\n");
    script.write("    [ -n \"$label\" ] || continue\n");
    script.write("    case \"$label\" in \\#*) continue ;; esac\n");
    script.write("    /bin/launchctl enable \"gui/$UID_VALUE/$label\" >/dev/null 2>&1 || true\n");
    script.write("  done < \"$STATE\"\n");
    script.write("  while IFS= read -r label; do\n");
    script.write("    [ -n \"$label\" ] || continue\n");
    script.write("    case \"$label\" in \\#*) continue ;; esac\n");
    script.write("    /bin/launchctl kickstart -k \"gui/$UID_VALUE/$label\" >/dev/null 2>&1 || true\n");
    script.write("  done < \"$STATE\"\n");
    script.write("  /bin/rm -f \"$STATE\"\n");
    script.write("fi\n");
    if (!script.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to save %1").arg(aquaRestoreScriptPathInternal());
        }
        return false;
    }
    ::chmod(aquaRestoreScriptPathInternal().toLocal8Bit().constData(), 0755);
    return true;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string basename(std::string path)
{
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::set<std::string> normalizedSet(const QStringList &values)
{
    std::set<std::string> result;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            result.insert(lowercase(trimmed.toUtf8().toStdString()));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// CGEventTap key blocker — swallows the system shortcuts the kiosk presentation
// options can't reach (Spotlight, Mission Control, Spaces, Launchpad, the Dock
// toggle, screenshots, force-quit, the switchers). The macOS analog of the
// Linux launcher-killing watchdog: deny the user a route to a launcher.
//
// virtual keycodes (Carbon kVK_*), kept inline so we don't pull in <Carbon/...>.
enum : CGKeyCode {
    kKeySpace      = 49,
    kKeyTab        = 48,
    kKeyGrave      = 50,  // `
    kKeyEscape     = 53,
    kKeyD          = 2,
    kKeyLeftArrow  = 123,
    kKeyRightArrow = 124,
    kKeyDownArrow  = 125,
    kKeyUpArrow    = 126,
    kKeyF3         = 99,
    kKeyF4         = 118,
    kKeyANSI_3     = 20,
    kKeyANSI_4     = 21,
    kKeyANSI_5     = 23,
};

// Set when a routine lockdown wants the system shortcuts dead. The tap callback
// reads it without a lock (single writer on the main thread, plain bool reads in
// the callback are fine) so it can be flipped on/off without tearing the tap up.
std::atomic<bool> g_inputBlockActive{false};
// When true (during a routine), let the navigation shortcuts through — Mission
// Control, Spaces, ⌘-Tab, ⌘-` — so the user can reach the routine's app windows
// in the desktop Space while FocusOS sits in its own fullscreen Space. The
// launch/escape surfaces stay blocked regardless. False on the home screen.
std::atomic<bool> g_inputAllowNavigation{false};
CFMachPortRef g_eventTap = nullptr;
CFRunLoopSourceRef g_eventTapSource = nullptr;

bool eventShouldBeBlocked(CGEventType type, CGEventRef event)
{
    if (type != kCGEventKeyDown) {
        return false;
    }

    const CGEventFlags flags = CGEventGetFlags(event);
    const bool cmd = (flags & kCGEventFlagMaskCommand) != 0;
    const bool ctrl = (flags & kCGEventFlagMaskControl) != 0;
    const bool alt = (flags & kCGEventFlagMaskAlternate) != 0;
    const bool shift = (flags & kCGEventFlagMaskShift) != 0;
    const CGKeyCode key =
        static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

    // During a routine the user navigates between FocusOS's fullscreen Space and
    // the routine apps' desktop Space, so the navigation shortcuts are allowed; on
    // the home screen they're swallowed so there's no route off the shell.
    const bool allowNav = g_inputAllowNavigation.load(std::memory_order_relaxed);

    switch (key) {
    case kKeySpace:                 // Spotlight (⌘-Space), Finder search (⌥⌘-Space)
        return cmd;                 // always blocked — a launch surface
    case kKeyTab:                   // app switcher (⌘-Tab / ⌥⌘-Tab)
    case kKeyGrave:                 // window switcher (⌘-`)
        return cmd && !allowNav;    // navigation — allowed during a routine
    case kKeyUpArrow:               // Mission Control (⌃-↑)
    case kKeyDownArrow:             // App Exposé (⌃-↓)
    case kKeyLeftArrow:             // move one Space left (⌃-←)
    case kKeyRightArrow:            // move one Space right (⌃-→)
        return ctrl && !allowNav;   // Spaces / Mission Control — allowed in routine
    case kKeyF3:                    // Mission Control key
        return !allowNav;           // allowed in routine so the user can swipe back
    case kKeyF4:                    // Launchpad key
        return true;                // always blocked — a launch surface
    case kKeyD:                     // toggle Dock (⌥⌘D)
        return cmd && alt;
    case kKeyANSI_3:                // screenshot to file (⌘⇧3)
    case kKeyANSI_4:                // screenshot selection (⌘⇧4)
    case kKeyANSI_5:                // screenshot/record UI (⌘⇧5)
        return cmd && shift;
    case kKeyEscape:                // force-quit dialog (⌥⌘Esc)
        return cmd && alt;
    default:
        return false;
    }
}

CGEventRef eventTapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void *)
{
    // The tap can be disabled by the system (a slow callback or user input
    // timeout). Re-arm it and let the event through.
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_eventTap) {
            CGEventTapEnable(g_eventTap, true);
        }
        return event;
    }

    if (g_inputBlockActive.load(std::memory_order_relaxed) &&
        eventShouldBeBlocked(type, event)) {
        return nullptr;  // swallow it
    }
    return event;
}

bool installEventTapLocked(QString *errorMessage)
{
    if (g_eventTap) {
        CGEventTapEnable(g_eventTap, true);
        return true;
    }

    const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
    CFMachPortRef tap = CGEventTapCreate(kCGSessionEventTap,
                                         kCGHeadInsertEventTap,
                                         kCGEventTapOptionDefault,
                                         mask,
                                         eventTapCallback,
                                         nullptr);
    if (!tap) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not install the keyboard event tap. Grant FocusOS "
                "Accessibility access in System Settings → Privacy & Security → "
                "Accessibility, then re-engage — otherwise Spotlight, Mission "
                "Control and the screenshot keys stay reachable.");
        }
        return false;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    g_eventTap = tap;
    g_eventTapSource = source;
    return true;
}

void removeEventTapLocked()
{
    if (g_eventTap) {
        CGEventTapEnable(g_eventTap, false);
    }
    if (g_eventTapSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), g_eventTapSource, kCFRunLoopCommonModes);
        CFRelease(g_eventTapSource);
        g_eventTapSource = nullptr;
    }
    if (g_eventTap) {
        CFRelease(g_eventTap);
        g_eventTap = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Regular (Dock-visible) GUI-app enumeration for the engage sweep and the
// deep-idle freeze. Background agents (LSUIElement / activationPolicy != Regular)
// are excluded by construction, so the system UI plumbing is never touched.
// Finder and FocusOS are always kept.

bool applicationIsKept(NSRunningApplication *application,
                       const std::set<std::string> &keepBundles,
                       const std::set<std::string> &keepNames,
                       const std::set<std::string> &keepPaths)
{
    if (application.processIdentifier == NSRunningApplication.currentApplication.processIdentifier) {
        return true;
    }
    const std::string bundleId = lowercase(fromNSString(application.bundleIdentifier).toUtf8().toStdString());
    if (bundleId == "com.apple.finder") {
        return true;  // the desktop — quitting/freezing it wedges the session
    }
    if (!bundleId.empty() && keepBundles.contains(bundleId)) {
        return true;
    }
    const std::string name = lowercase(fromNSString(application.localizedName).toUtf8().toStdString());
    if (!name.empty() && keepNames.contains(name)) {
        return true;
    }
    const std::string path = lowercase(fromNSString(application.executableURL.path).toUtf8().toStdString());
    if (!path.empty() && keepPaths.contains(path)) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// NSWorkspace launch watcher — userland blocklist enforcement (see header). The
// observer block runs on the main queue; it reads the policy under a mutex so
// MacBackend can update it (re-engage, always-allowed change) without tearing the
// observer down.
std::mutex g_launchWatcherMutex;
std::set<std::string> g_lwBlockedNames;
std::set<std::string> g_lwBlockedBundleIds;
std::set<std::string> g_lwAllowedNames;
std::set<std::string> g_lwAllowedBundleIds;
std::set<std::string> g_lwAllowedPaths;
id g_launchObserver = nil;

bool launchShouldBlockLocked(NSRunningApplication *application)
{
    // Never touch FocusOS itself or Finder (the desktop).
    if (application.processIdentifier == NSRunningApplication.currentApplication.processIdentifier) {
        return false;
    }
    const std::string bundleId =
        lowercase(fromNSString(application.bundleIdentifier).toUtf8().toStdString());
    if (bundleId == "com.apple.finder") {
        return false;
    }
    const std::string name =
        lowercase(fromNSString(application.localizedName).toUtf8().toStdString());
    const std::string path =
        lowercase(fromNSString(application.executableURL.path).toUtf8().toStdString());

    std::lock_guard<std::mutex> lock(g_launchWatcherMutex);
    // Allowed wins (routine apps, always-allowed list, the kiosk browser path).
    if ((!path.empty() && g_lwAllowedPaths.contains(path)) ||
        (!name.empty() && g_lwAllowedNames.contains(name)) ||
        (!bundleId.empty() && g_lwAllowedBundleIds.contains(bundleId))) {
        return false;
    }
    // Deny all other regular, Dock-visible GUI launches during a strict routine.
    // This closes gaps like Notes/Photos without needing to enumerate every
    // possible distraction. AUTH_EXEC remains blocklist-based because it cannot
    // reliably distinguish user apps from helper/background execs.
    Q_UNUSED(g_lwBlockedNames);
    Q_UNUSED(g_lwBlockedBundleIds);
    return true;
}

#if FOCUSOS_ENDPOINT_SECURITY_AVAILABLE

std::string tokenToString(es_string_token_t token)
{
    if (token.data == nullptr || token.length == 0) {
        return {};
    }
    return std::string(token.data, token.length);
}

class EndpointSecurityExecBlocker
{
public:
    ~EndpointSecurityExecBlocker()
    {
        stop();
    }

    bool start(const QStringList &blockedNames,
               const QStringList &blockedBundleIdentifiers,
               const QStringList &allowedNames,
               const QStringList &allowedBundleIdentifiers,
               const QStringList &allowedExecutablePaths,
               QString *errorMessage)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_blockedNames = normalizedSet(blockedNames);
            m_blockedBundleIdentifiers = normalizedSet(blockedBundleIdentifiers);
            m_allowedNames = normalizedSet(allowedNames);
            m_allowedBundleIdentifiers = normalizedSet(allowedBundleIdentifiers);
            m_allowedExecutablePaths = normalizedSet(allowedExecutablePaths);
            m_running = true;
        }

        if (m_client) {
            es_clear_cache(m_client);
            return true;
        }

        es_client_t *client = nullptr;
        es_new_client_result_t result = es_new_client(&client, ^(es_client_t *callbackClient, const es_message_t *message) {
            if (message->event_type != ES_EVENT_TYPE_AUTH_EXEC) {
                return;
            }
            const bool denied = shouldDeny(message);
            es_respond_auth_result(callbackClient,
                                   message,
                                   denied ? ES_AUTH_RESULT_DENY : ES_AUTH_RESULT_ALLOW,
                                   false);
        });

        if (result != ES_NEW_CLIENT_RESULT_SUCCESS) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            if (errorMessage) {
                *errorMessage = QStringLiteral("Endpoint Security exec blocker could not start (code %1). Run FocusOS as root and sign it with com.apple.developer.endpoint-security.client.")
                    .arg(static_cast<int>(result));
            }
            return false;
        }

        es_event_type_t events[] = { ES_EVENT_TYPE_AUTH_EXEC };
        if (es_subscribe(client, events, 1) != ES_RETURN_SUCCESS) {
            es_delete_client(client);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            if (errorMessage) {
                *errorMessage = QStringLiteral("Endpoint Security AUTH_EXEC subscription failed");
            }
            return false;
        }

        es_clear_cache(client);
        m_client = client;
        return true;
    }

    void stop()
    {
        es_client_t *client = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            client = m_client;
            m_client = nullptr;
        }

        if (client) {
            es_unsubscribe_all(client);
            es_delete_client(client);
        }
    }

private:
    bool shouldDeny(const es_message_t *message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || message->event.exec.target == nullptr) {
            return false;
        }

        const es_process_t *target = message->event.exec.target;
        std::string executablePath;
        if (target->executable != nullptr) {
            executablePath = lowercase(tokenToString(target->executable->path));
        }
        const std::string executableName = lowercase(basename(executablePath));
        const std::string signingId = lowercase(tokenToString(target->signing_id));

        if (m_allowedExecutablePaths.contains(executablePath) ||
            m_allowedNames.contains(executableName) ||
            m_allowedBundleIdentifiers.contains(signingId)) {
            return false;
        }

        return m_blockedNames.contains(executableName) ||
               m_blockedBundleIdentifiers.contains(signingId);
    }

    std::mutex m_mutex;
    es_client_t *m_client = nullptr;
    bool m_running = false;
    std::set<std::string> m_blockedNames;
    std::set<std::string> m_blockedBundleIdentifiers;
    std::set<std::string> m_allowedNames;
    std::set<std::string> m_allowedBundleIdentifiers;
    std::set<std::string> m_allowedExecutablePaths;
};

EndpointSecurityExecBlocker &execBlocker()
{
    static EndpointSecurityExecBlocker blocker;
    return blocker;
}

#endif

// ---------------------------------------------------------------------------
// Home-screen kiosk activation guard. NSApplicationPresentationOptions
// (HideDock, DisableProcessSwitching=⌘-Tab, HideMenuBar, DisableForceQuit …)
// ONLY take effect while FocusOS is the *active* (frontmost) app — when another
// app is frontmost the system ignores them entirely. That is why the lock used
// to require a CLICK on the FocusOS window after a routine ended or at launch:
// until the click made FocusOS active, the Dock / ⌘-Tab / Mission Control were
// all reachable. The fix is two-fold: (1) actually land activation (the old
// activateIgnoringOtherApps: alone is unreliable on modern macOS), and (2) keep
// FocusOS frontmost on the locked home screen by re-activating + re-asserting the
// options the instant it resigns active. The home screen has nothing else to
// focus (the engage sweep closed the user's apps; the launch watcher reaps new
// ones), so reclaiming focus there is correct and makes the lock un-bypassable.
// The guard is installed by enterKioskPresentation and removed by
// leaveKioskPresentation, so it is OFF during a routine / admin desktop access
// (where the user is meant to move focus to the allowed apps).
id g_resignActiveObserver = nil;

void applyKioskPresentationAndActivate()
{
    // Caller guarantees the main thread.
    [NSApplication sharedApplication];
    const NSApplicationPresentationOptions options =
        NSApplicationPresentationHideDock |
        NSApplicationPresentationHideMenuBar |
        NSApplicationPresentationDisableAppleMenu |
        NSApplicationPresentationDisableProcessSwitching |
        NSApplicationPresentationDisableForceQuit |
        NSApplicationPresentationDisableSessionTermination |
        NSApplicationPresentationDisableHideApplication;
    @try {
        [NSApp setPresentationOptions:options];
    } @catch (NSException *) {
    }
    // Become frontmost so the options above are honored. activateIgnoringOtherApps:
    // is deprecated and frequently ignored on macOS 14+, so also drive the
    // NSRunningApplication API (the documented modern path) and pull every window
    // forward — the cover window becomes key, the click-through overlay panel does
    // not (it is non-activating).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
    [[NSRunningApplication currentApplication]
        activateWithOptions:NSApplicationActivateAllWindows |
                            NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
}

void installKioskActivationGuardLocked()
{
    if (g_resignActiveObserver != nil) {
        return;
    }
    g_resignActiveObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidResignActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) {
        // FocusOS just lost frontmost while the home-screen lock is up → the kiosk
        // presentation options went inert (Dock / ⌘-Tab / Mission Control would
        // reappear). Reclaim focus and re-assert them so the lock holds without a
        // click. A short delay lets the system settle and prevents a tight
        // activation loop if something briefly contends for focus.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.05 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            if (g_resignActiveObserver == nil) {
                return;  // guard was lifted (routine engaged / desktop access) meanwhile
            }
            applyKioskPresentationAndActivate();
        });
    }];
}

void removeKioskActivationGuardLocked()
{
    if (g_resignActiveObserver != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_resignActiveObserver];
        g_resignActiveObserver = nil;
    }
}

} // namespace

namespace MacBackendNative {

QString applicationExecutablePath(const QString &bundlePath)
{
    NSBundle *bundle = bundleForPath(bundlePath);
    if (!bundle) {
        return {};
    }
    return fromNSString([bundle executablePath]);
}

QString bundleIdentifierForApplication(const QString &bundlePath)
{
    NSBundle *bundle = bundleForPath(bundlePath);
    if (!bundle) {
        return {};
    }
    return fromNSString([bundle bundleIdentifier]);
}

QString bundleIdentifierForExecutable(const QString &executablePath)
{
    NSString *bundlePath = applicationBundlePathForExecutable(toNSString(executablePath));
    if (bundlePath.length == 0) {
        return {};
    }
    NSBundle *bundle = [NSBundle bundleWithPath:bundlePath];
    return fromNSString([bundle bundleIdentifier]);
}

QString displayNameForApplication(const QString &bundlePath)
{
    return displayNameForBundle(bundleForPath(bundlePath), bundlePath);
}

NativeLaunchResult launchApplicationBundle(const QString &bundlePath, const QStringList &arguments)
{
    NativeLaunchResult result;
    NSBundle *bundle = bundleForPath(bundlePath);
    if (!bundle) {
        result.errorMessage = QStringLiteral("Application bundle not found: %1").arg(bundlePath);
        return result;
    }

    NSURL *url = [NSURL fileURLWithPath:standardizedPath(toNSString(bundlePath))];

    NSError *error = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSMutableDictionary *configuration = [NSMutableDictionary dictionary];
    if (!arguments.isEmpty()) {
        configuration[NSWorkspaceLaunchConfigurationArguments] = toNSArray(arguments);
    }

    NSRunningApplication *application =
        [[NSWorkspace sharedWorkspace] launchApplicationAtURL:url
                                                      options:NSWorkspaceLaunchDefault
                                                configuration:configuration
                                                        error:&error];
#pragma clang diagnostic pop

    if (!application) {
        result.errorMessage = QStringLiteral("Unable to launch %1: %2")
            .arg(bundlePath, error ? fromNSString([error localizedDescription])
                                   : QStringLiteral("unknown error"));
        return result;
    }

    result.launched = true;
    result.pid = application.processIdentifier;
    result.executablePath = fromNSString([bundle executablePath]);
    result.bundleIdentifier = fromNSString([bundle bundleIdentifier]);
    result.displayName = displayNameForBundle(bundle, bundlePath);
    return result;
}

void terminateApplications(const QStringList &bundleIdentifiers,
                           const QStringList &displayNames,
                           const QStringList &executablePaths)
{
    const std::set<std::string> bundleSet = normalizedSet(bundleIdentifiers);
    const std::set<std::string> nameSet = normalizedSet(displayNames);
    const std::set<std::string> pathSet = normalizedSet(executablePaths);
    NSMutableArray<NSRunningApplication *> *matched = [NSMutableArray array];

    for (NSRunningApplication *application in [[NSWorkspace sharedWorkspace] runningApplications]) {
        const std::string bundleId = lowercase(fromNSString(application.bundleIdentifier).toUtf8().toStdString());
        const std::string localizedName = lowercase(fromNSString(application.localizedName).toUtf8().toStdString());
        const std::string executablePath = lowercase(fromNSString(application.executableURL.path).toUtf8().toStdString());

        const bool matches = (!bundleId.empty() && bundleSet.contains(bundleId)) ||
                             (!localizedName.empty() && nameSet.contains(localizedName)) ||
                             (!executablePath.empty() && pathSet.contains(executablePath));
        if (!matches) {
            continue;
        }

        [matched addObject:application];
        [application terminate];
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.5];
    while ([deadline timeIntervalSinceNow] > 0) {
        bool allTerminated = true;
        for (NSRunningApplication *application in matched) {
            if (!application.terminated) {
                allTerminated = false;
                break;
            }
        }
        if (allTerminated) {
            return;
        }
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }

    for (NSRunningApplication *application in matched) {
        if (!application.terminated) {
            [application forceTerminate];
        }
    }
}

bool enterKioskPresentation(QString *errorMessage)
{
    __block QString error;
    performOnMainThreadSync(^{
        @try {
            // Apply the kiosk presentation options AND reliably become frontmost
            // (the options are inert unless FocusOS is the active app), then install
            // the resign-active guard so the lock re-asserts itself automatically if
            // focus ever slips — no click on the FocusOS window required.
            applyKioskPresentationAndActivate();
            installKioskActivationGuardLocked();
        } @catch (NSException *exception) {
            error = QStringLiteral("Unable to enter macOS kiosk presentation mode: %1")
                .arg(fromNSString([exception reason]));
        }
    });

    if (!error.isEmpty()) {
        if (errorMessage) {
            *errorMessage = error;
        }
        return false;
    }
    return true;
}

void coverScreenIncludingNotch(void *nsView)
{
    if (!nsView) {
        return;
    }
    performOnMainThreadSync(^{
        id object = (__bridge id)nsView;
        NSWindow *window = nil;
        if ([object isKindOfClass:[NSView class]]) {
            window = [(NSView *)object window];
        } else if ([object isKindOfClass:[NSWindow class]]) {
            window = (NSWindow *)object;
        }
        if (!window) {
            return;
        }
        NSScreen *screen = window.screen ?: [NSScreen mainScreen];
        if (!screen) {
            return;
        }
        @try {
            // Float above the menu bar so the content owns the notch strip too.
            // (The menu bar is already hidden by the kiosk presentation options;
            // this is what lets the window's frame extend up to y=0.)
            window.level = NSMainMenuWindowLevel + 1;
            // screen.frame is the FULL backing frame, notch strip included —
            // screen.visibleFrame would re-introduce the safe-area inset.
            [window setFrame:screen.frame display:YES];
        } @catch (NSException *) {
        }
    });
}

void restoreStandardWindowLevel(void *nsView)
{
    if (!nsView) {
        return;
    }
    performOnMainThreadSync(^{
        id object = (__bridge id)nsView;
        NSWindow *window = nil;
        if ([object isKindOfClass:[NSView class]]) {
            window = [(NSView *)object window];
        } else if ([object isKindOfClass:[NSWindow class]]) {
            window = (NSWindow *)object;
        }
        if (!window) {
            return;
        }
        @try {
            // Back to an ordinary window: normal level (below the menu bar) and
            // collection behavior so it tiles/moves like any app window. The
            // caller (ShellWindow) restores the decorated style + windowed frame
            // on the Qt side.
            window.level = NSNormalWindowLevel;
            window.collectionBehavior = NSWindowCollectionBehaviorDefault;
        } @catch (NSException *) {
        }
    });
}

void enterNativeFullScreen(void *nsView)
{
    performOnMainThreadSync(^{
        NSWindow *window = windowFromNsView(nsView);
        if (!window) {
            return;
        }
        @try {
            // Native fullscreen needs a normal-level, fullscreen-capable window;
            // drop the above-the-menu-bar kiosk level coverScreenIncludingNotch set.
            window.level = NSNormalWindowLevel;
            window.collectionBehavior =
                (window.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenAuxiliary) |
                NSWindowCollectionBehaviorFullScreenPrimary;
            if ((window.styleMask & NSWindowStyleMaskFullScreen) == 0) {
                [window toggleFullScreen:nil];
            }
        } @catch (NSException *) {
        }
    });
}

void exitNativeFullScreen(void *nsView)
{
    performOnMainThreadSync(^{
        NSWindow *window = windowFromNsView(nsView);
        if (!window) {
            return;
        }
        @try {
            if ((window.styleMask & NSWindowStyleMaskFullScreen) != 0) {
                [window toggleFullScreen:nil];
            }
        } @catch (NSException *) {
        }
    });
}

bool windowIsNativeFullScreen(void *nsView)
{
    __block bool fullScreen = false;
    performOnMainThreadSync(^{
        NSWindow *window = windowFromNsView(nsView);
        if (window) {
            fullScreen = (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
        }
    });
    return fullScreen;
}

void setWindowJoinsAllSpaces(void *nsView, bool onAll)
{
    performOnMainThreadSync(^{
        NSWindow *window = windowFromNsView(nsView);
        if (!window) {
            return;
        }
        @try {
            const NSWindowCollectionBehavior allSpaces =
                NSWindowCollectionBehaviorCanJoinAllSpaces |
                NSWindowCollectionBehaviorStationary |
                NSWindowCollectionBehaviorFullScreenAuxiliary |
                NSWindowCollectionBehaviorIgnoresCycle;
            if (onAll) {
                window.collectionBehavior |= allSpaces;
            } else {
                window.collectionBehavior &= ~allSpaces;
            }
        } @catch (NSException *) {
        }
    });
}

void raiseWindowToSystemOverlayLevel(void *nsView)
{
    performOnMainThreadSync(^{
        NSWindow *window = windowFromNsView(nsView);
        if (!window) {
            return;
        }
        @try {
            // The progress overlay must paint over EVERYTHING on EVERY display:
            // other apps, the Dock, the menu bar, and native-fullscreen Spaces.
            // NSMainMenuWindowLevel+1 (what coverScreenIncludingNotch sets) is below
            // other apps' regular windows, so the border vanished the moment a
            // routine app took focus. Screen-saver level sits above ordinary,
            // floating, and even most system windows, which is what "global overlay"
            // needs. Pin it onto all Spaces (and above fullscreen Spaces) and make it
            // click-through at the AppKit layer too, belt-and-suspenders with Qt's
            // WindowTransparentForInput flag.
            window.level = NSScreenSaverWindowLevel + 1;
            window.ignoresMouseEvents = YES;
            // Guarantee the panel is actually drawable + transparent (not an opaque
            // black fill, not alpha 0): defensive against any state that would render
            // the border invisible. Matches the verified standalone overlay setup.
            window.opaque = NO;
            window.backgroundColor = [NSColor clearColor];
            window.hasShadow = NO;
            if (window.alphaValue < 0.99) {
                window.alphaValue = 1.0;
            }
            window.contentView.wantsLayer = YES;
            window.collectionBehavior |=
                NSWindowCollectionBehaviorCanJoinAllSpaces |
                NSWindowCollectionBehaviorStationary |
                NSWindowCollectionBehaviorFullScreenAuxiliary |
                NSWindowCollectionBehaviorIgnoresCycle;
            // The overlay is a Qt::Tool window → an NSPanel, which defaults to
            // hidesOnDeactivate=YES: it disappears the instant FocusOS is no longer
            // the active app (i.e. the moment a routine app takes focus on the
            // desktop Space). That's why the countdown border was "not on all
            // workspaces". Pin it visible regardless of which app is frontmost.
            window.hidesOnDeactivate = NO;
            if ([window isKindOfClass:[NSPanel class]]) {
                NSPanel *panel = (NSPanel *)window;
                panel.floatingPanel = YES;
                panel.becomesKeyOnlyIfNeeded = YES;
                // Non-activating: the overlay must never become key/main, even
                // momentarily, or macOS pulls FocusOS forward and the panel sticks
                // to FocusOS's Space instead of floating across all of them. This
                // is the difference between "shows only on the FocusOS window" and
                // a true system-wide overlay.
                panel.styleMask |= NSWindowStyleMaskNonactivatingPanel;
            }
            // orderFrontRegardless ignores the "app is not active" gate that the
            // ordinary orderFront:/Qt show() path honors — so the border keeps
            // painting on top even while a routine app on the desktop Space is the
            // frontmost application.
            [window orderFrontRegardless];
        } @catch (NSException *) {
        }
    });
}

void setMissionControlDisabled(bool disabled)
{
    // Hard-disable Mission Control / Spaces / App Exposé for the locked home screen
    // so a routine end leaves the machine genuinely locked — the CGEventTap key
    // blocker only swallows the F3 / ⌃-arrow KEYS and cannot stop the trackpad
    // swipe-up gesture, so the gesture remained a route to Mission Control (and a
    // visible Spaces strip) even on the locked shell. `mcx-expose-disabled` is the
    // documented MDM/profile key that turns the whole Exposé/Spaces subsystem off;
    // it kills both the gesture and the keys. Cleared again the moment the user
    // enters their 6-digit code (Access Desktop) or a routine engages (a routine
    // deliberately keeps Spaces navigation so the user can reach the routine apps).
    // Targets the console user's preference domain even when FocusOS runs as root.
    const QString defaults = QStringLiteral("/usr/bin/defaults");
    const QString dockDomain = QStringLiteral("com.apple.dock");
    // The trackpad/Magic-Mouse swipe-up that opens Mission Control / App Exposé is
    // separate from the keyboard route the CGEventTap already swallows, and from
    // mcx-expose-disabled (which macOS 26 no longer honors live). It's driven by the
    // multitouch gesture prefs, so flip those off too on the locked home screen.
    // Three- and four-finger vertical swipes cover whichever the user has bound to
    // Mission Control. Both the built-in and Bluetooth trackpad domains are written.
    const QStringList trackpadDomains {
        QStringLiteral("com.apple.AppleMultitouchTrackpad"),
        QStringLiteral("com.apple.driver.AppleBluetoothMultitouch.trackpad")
    };
    // Vertical = Mission Control / App Exposé (swipe up). Horizontal = switch between
    // Spaces / full-screen apps (swipe sideways) — without disabling this the user
    // could swipe to an adjacent Space and get stranded there (the swipe-back is the
    // same blocked gesture, and ⌃-arrow is swallowed by the CGEventTap). Kill both
    // axes for three- and four-finger swipes so the locked home screen is a dead end
    // in every direction.
    const QStringList swipeKeys {
        QStringLiteral("TrackpadThreeFingerVertSwipeGesture"),
        QStringLiteral("TrackpadFourFingerVertSwipeGesture"),
        QStringLiteral("TrackpadThreeFingerHorizSwipeGesture"),
        QStringLiteral("TrackpadFourFingerHorizSwipeGesture")
    };
    if (disabled) {
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("mcx-expose-disabled"),
                                    QStringLiteral("-bool"), QStringLiteral("true")});
        for (const QString &domain : trackpadDomains) {
            for (const QString &key : swipeKeys) {
                runAsConsoleUser(defaults, {QStringLiteral("write"), domain, key,
                                            QStringLiteral("-int"), QStringLiteral("0")});
            }
        }
    } else {
        // `mcx-expose-disabled` is not a key ordinary users set, so deleting it
        // simply returns Mission Control to its default (enabled) state. The error
        // path (key absent) is harmless. Likewise delete the swipe-gesture overrides
        // so the trackpad reverts to the user's System Settings defaults.
        runAsConsoleUser(defaults, {QStringLiteral("delete"), dockDomain,
                                    QStringLiteral("mcx-expose-disabled")});
        for (const QString &domain : trackpadDomains) {
            for (const QString &key : swipeKeys) {
                runAsConsoleUser(defaults, {QStringLiteral("delete"), domain, key});
            }
        }
    }
    runAsConsoleUser(QStringLiteral("/usr/bin/killall"), {QStringLiteral("Dock")});
}

void leaveKioskPresentation()
{
    performOnMainThreadSync(^{
        @try {
            // Lift the self-reactivation guard FIRST so dropping the presentation
            // options (and letting a routine app take focus) doesn't trigger a
            // re-grab. Then restore the default presentation.
            removeKioskActivationGuardLocked();
            [NSApplication sharedApplication];
            [NSApp setPresentationOptions:NSApplicationPresentationDefault];
        } @catch (NSException *) {
        }
    });
}

void setSystemDockHidden(bool hidden)
{
    const QString defaults = QStringLiteral("/usr/bin/defaults");
    const QString dockDomain = QStringLiteral("com.apple.dock");
    if (hidden) {
        runAsConsoleUser(QStringLiteral("/bin/sh"),
                         {QStringLiteral("-c"),
                          QStringLiteral("/bin/mkdir -p \"$HOME/.focusos\"; "
                                         "if [ ! -f \"$HOME/.focusos/dock-pre-focusos.plist\" ]; then "
                                         "/usr/bin/defaults export com.apple.dock "
                                         "\"$HOME/.focusos/dock-pre-focusos.plist\" >/dev/null 2>&1 "
                                         "|| /usr/bin/touch \"$HOME/.focusos/dock-pre-focusos.plist\"; "
                                         "fi")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide"), QStringLiteral("-bool"), QStringLiteral("true")});
        // A huge reveal delay means the Dock never slides in on a bottom-edge
        // hover, and a zero animation time keeps the hide instant.
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide-delay"), QStringLiteral("-float"), QStringLiteral("1000")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide-time-modifier"), QStringLiteral("-float"), QStringLiteral("0")});
    } else {
        // Restore the user's original Dock domain when we have a snapshot. If a
        // snapshot could not be written (or this is cleanup after an older build),
        // fall back to an ordinary visible Dock and remove FocusOS's delay keys.
        runAsConsoleUser(QStringLiteral("/bin/sh"),
                         {QStringLiteral("-c"),
                          QStringLiteral("if [ -s \"$HOME/.focusos/dock-pre-focusos.plist\" ]; then "
                                         "/usr/bin/defaults import com.apple.dock "
                                         "\"$HOME/.focusos/dock-pre-focusos.plist\" >/dev/null 2>&1; "
                                         "/bin/rm -f \"$HOME/.focusos/dock-pre-focusos.plist\"; "
                                         "else "
                                         "/bin/rm -f \"$HOME/.focusos/dock-pre-focusos.plist\"; "
                                         "/usr/bin/defaults delete com.apple.dock autohide-delay >/dev/null 2>&1; "
                                         "/usr/bin/defaults delete com.apple.dock autohide-time-modifier >/dev/null 2>&1; "
                                         "/usr/bin/defaults write com.apple.dock autohide -bool false; "
                                         "fi")});
    }
    // Restart the Dock so it re-reads the preference. Harmless if it isn't running.
    runAsConsoleUser(QStringLiteral("/usr/bin/killall"), {QStringLiteral("Dock")});
}

void neuterDock(bool neuter)
{
    const QString defaults = QStringLiteral("/usr/bin/defaults");
    const QString dockDomain = QStringLiteral("com.apple.dock");
    if (neuter) {
        // Snapshot the user's real Dock once — shared with setSystemDockHidden, so
        // whichever modifies the Dock first takes the snapshot and the single
        // restore path (setSystemDockHidden(false) / neuterDock(false)) brings the
        // full Dock back. Guarded so we never overwrite the snapshot with a
        // half-neutered Dock.
        runAsConsoleUser(QStringLiteral("/bin/sh"),
                         {QStringLiteral("-c"),
                          QStringLiteral("/bin/mkdir -p \"$HOME/.focusos\"; "
                                         "if [ ! -f \"$HOME/.focusos/dock-pre-focusos.plist\" ]; then "
                                         "/usr/bin/defaults export com.apple.dock "
                                         "\"$HOME/.focusos/dock-pre-focusos.plist\" >/dev/null 2>&1 "
                                         "|| /usr/bin/touch \"$HOME/.focusos/dock-pre-focusos.plist\"; "
                                         "fi")});
        // Tiny tiles, no magnification, and an empty Dock (no pinned apps, no
        // recents) so a Mission Control swipe-up during the routine shows nothing
        // worth launching. Autohidden with a huge reveal delay so it never slides
        // in on the desktop Space either.
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("tilesize"), QStringLiteral("-int"), QStringLiteral("16")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("magnification"), QStringLiteral("-bool"), QStringLiteral("false")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("persistent-apps"), QStringLiteral("-array")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("persistent-others"), QStringLiteral("-array")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("recent-apps"), QStringLiteral("-array")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("show-recents"), QStringLiteral("-bool"), QStringLiteral("false")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide"), QStringLiteral("-bool"), QStringLiteral("true")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide-delay"), QStringLiteral("-float"), QStringLiteral("1000")});
        runAsConsoleUser(defaults, {QStringLiteral("write"), dockDomain,
                                    QStringLiteral("autohide-time-modifier"), QStringLiteral("-float"), QStringLiteral("0")});
        runAsConsoleUser(QStringLiteral("/usr/bin/killall"), {QStringLiteral("Dock")});
        return;
    }

    // Restore is identical to lifting the hidden posture — delegate so there's a
    // single Dock-restore path (imports the snapshot, or clears our override keys
    // when no snapshot exists).
    setSystemDockHidden(false);
}

QString aquaUiRestoreScriptPath()
{
    return aquaRestoreScriptPathInternal();
}

bool applyAquaUiLockdown(QString *errorMessage)
{
    const QString domain = launchdGuiDomain();
    QStringList focusDisabled = readAquaState();
    QStringList failures;

    for (const QString &label : aquaUiLockdownLabels()) {
        if (launchdLabelDisabled(label)) {
            continue;  // pre-existing user/admin override; do not own/restore it
        }

        QString output;
        if (!runLaunchctl({QStringLiteral("disable"), QStringLiteral("%1/%2").arg(domain, label)}, &output)) {
            failures.append(output.isEmpty()
                ? QStringLiteral("%1: launchctl disable failed").arg(label)
                : QStringLiteral("%1: %2").arg(label, output.left(180)));
            continue;
        }

        focusDisabled.append(label);
        // Once disabled, boot it out so the already-running instance disappears
        // now instead of waiting for the next login.
        runLaunchctl({QStringLiteral("bootout"), QStringLiteral("%1/%2").arg(domain, label)}, nullptr, 3000);
    }

    focusDisabled.removeDuplicates();
    if (!focusDisabled.isEmpty()) {
        QString writeError;
        if (!writeAquaRestoreScript(focusDisabled, &writeError)) {
            failures.append(writeError);
        }
    }

    if (!failures.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Aqua UI lockdown was not fully applied. This stronger macOS mode "
                "requires SIP-off launchd overrides. %1")
                .arg(failures.join(QStringLiteral(" | ")).left(700));
        }
        return false;
    }
    return true;
}

void restoreAquaUiLockdown()
{
    const QString domain = launchdGuiDomain();

    // Heal the system UI. The legacy SIP-off lockdown could `launchctl disable`
    // AND `bootout` the Dock / Spotlight / Mission Control agents — and a booted-out
    // service is DEAD: `enable` + `kickstart` cannot revive it, only a fresh
    // `bootstrap` of its plist reloads it into the domain. A dead Dock also takes
    // down ⌘-Tab (the app switcher) and Mission Control, which is exactly the
    // "Access Desktop doesn't bring back the Dock / app switcher" failure.
    //
    // So for every UI agent FocusOS could have touched (plus anything still recorded
    // in the legacy state file), clear any disable flag and — only when the agent is
    // not currently loaded — bootstrap its plist back and kick it. Agents that are
    // already alive are left untouched, so a healthy launch never restarts the Dock
    // or Finder.
    QStringList candidates = aquaUiLockdownLabels();
    candidates.append(readAquaState());
    candidates.removeDuplicates();

    for (const QString &label : candidates) {
        runLaunchctl({QStringLiteral("enable"), QStringLiteral("%1/%2").arg(domain, label)}, nullptr, 3000);
        if (launchdLabelLoaded(label)) {
            continue;  // already running — don't churn it
        }
        const QString plist = systemAgentPlistPath(label);
        if (!plist.isEmpty()) {
            // Reload the booted-out agent into the GUI domain, then start it.
            runLaunchctl({QStringLiteral("bootstrap"), domain, plist}, nullptr, 4000);
        }
        runLaunchctl({QStringLiteral("kickstart"), QStringLiteral("-k"),
                      QStringLiteral("%1/%2").arg(domain, label)}, nullptr, 4000);
    }
    QFile::remove(aquaStatePath());
}

bool isAccessibilityTrusted()
{
    return AXIsProcessTrusted();
}

bool startInputBlocker(bool allowNavigation, QString *errorMessage)
{
    __block bool installed = false;
    __block bool trusted = false;
    __block QString error;
    performOnMainThreadSync(^{
        trusted = AXIsProcessTrusted();
        if (!trusted) {
            // Surface the system Accessibility prompt AT MOST ONCE per process. A
            // freshly granted permission does NOT flip AXIsProcessTrusted() for the
            // already-running process (TCC only re-reads trust on relaunch), so a
            // naive "prompt whenever untrusted" re-pops the dialog on every session
            // start — engage, routine end, Access-Desktop teardown — because each of
            // those paths calls startInputBlocker again. Gate it behind a one-shot
            // flag so the user is asked exactly once; after they grant + relaunch,
            // this branch is skipped entirely (trusted == true). The event tap is
            // still installed below either way, but stays inert until access is
            // granted and FocusOS is relaunched.
            static std::atomic<bool> promptShown{false};
            if (!promptShown.exchange(true)) {
                NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
                AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
            }
        }
        g_inputAllowNavigation.store(allowNavigation, std::memory_order_relaxed);
        g_inputBlockActive.store(true, std::memory_order_relaxed);
        installed = installEventTapLocked(&error);
    });

    if (!trusted) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "FocusOS needs Accessibility access to block Spotlight, Mission "
                "Control and the screenshot keys. Grant it in System Settings → "
                "Privacy & Security → Accessibility, then relaunch FocusOS.");
        }
        return false;
    }
    if (!installed && errorMessage) {
        *errorMessage = error;
    }
    return installed;
}

void stopInputBlocker()
{
    performOnMainThreadSync(^{
        g_inputBlockActive.store(false, std::memory_order_relaxed);
        removeEventTapLocked();
    });
}

QStringList sweepOtherApplications(const QStringList &keepBundleIdentifiers,
                                   const QStringList &keepDisplayNames,
                                   const QStringList &keepExecutablePaths,
                                   bool dryRun)
{
    const std::set<std::string> keepBundles = normalizedSet(keepBundleIdentifiers);
    const std::set<std::string> keepNames = normalizedSet(keepDisplayNames);
    const std::set<std::string> keepPaths = normalizedSet(keepExecutablePaths);

    QStringList acted;
    NSMutableArray<NSRunningApplication *> *matched = [NSMutableArray array];
    for (NSRunningApplication *application in [[NSWorkspace sharedWorkspace] runningApplications]) {
        if (application.activationPolicy != NSApplicationActivationPolicyRegular) {
            continue;  // background agent / accessory — not a window the user opened
        }
        if (applicationIsKept(application, keepBundles, keepNames, keepPaths)) {
            continue;
        }
        const QString name = fromNSString(application.localizedName);
        if (!name.isEmpty()) {
            acted.append(name);
        }
        if (!dryRun) {
            [matched addObject:application];
            [application terminate];
        }
    }

    if (!dryRun && matched.count > 0) {
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.5];
        while ([deadline timeIntervalSinceNow] > 0) {
            bool allDone = true;
            for (NSRunningApplication *application in matched) {
                if (!application.terminated) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) {
                break;
            }
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
        for (NSRunningApplication *application in matched) {
            if (!application.terminated) {
                [application forceTerminate];
            }
        }
    }

    acted.removeDuplicates();
    acted.sort();
    return acted;
}

QList<qint64> freezeOtherApplications(const QStringList &keepBundleIdentifiers,
                                      const QStringList &keepDisplayNames,
                                      const QStringList &keepExecutablePaths)
{
    const std::set<std::string> keepBundles = normalizedSet(keepBundleIdentifiers);
    const std::set<std::string> keepNames = normalizedSet(keepDisplayNames);
    const std::set<std::string> keepPaths = normalizedSet(keepExecutablePaths);

    QList<qint64> frozen;
    for (NSRunningApplication *application in [[NSWorkspace sharedWorkspace] runningApplications]) {
        if (application.activationPolicy != NSApplicationActivationPolicyRegular) {
            continue;
        }
        if (applicationIsKept(application, keepBundles, keepNames, keepPaths)) {
            continue;
        }
        const pid_t pid = application.processIdentifier;
        if (pid > 1 && ::kill(pid, SIGSTOP) == 0) {
            frozen.append(static_cast<qint64>(pid));
        }
    }
    return frozen;
}

void resumeProcesses(const QList<qint64> &pids)
{
    for (const qint64 pid : pids) {
        ::kill(static_cast<pid_t>(pid), SIGCONT);
    }
}

bool createDisplaySleepAssertion(quint32 *assertionId, QString *errorMessage)
{
    if (!assertionId) {
        return false;
    }

    IOPMAssertionID nativeAssertion = kIOPMNullAssertionID;
    const IOReturn result = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep,
                                                        kIOPMAssertionLevelOn,
                                                        CFSTR("FocusOS routine active"),
                                                        &nativeAssertion);
    if (result != kIOReturnSuccess) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("IOPMAssertionCreateWithName failed: 0x%1")
                .arg(static_cast<uint>(result), 0, 16);
        }
        return false;
    }

    *assertionId = nativeAssertion;
    return true;
}

void releaseDisplaySleepAssertion(quint32 assertionId)
{
    if (assertionId != kIOPMNullAssertionID) {
        IOPMAssertionRelease(assertionId);
    }
}

void startLaunchWatcher(const QStringList &blockedNames,
                        const QStringList &blockedBundleIdentifiers,
                        const QStringList &allowedNames,
                        const QStringList &allowedBundleIdentifiers,
                        const QStringList &allowedExecutablePaths)
{
    {
        std::lock_guard<std::mutex> lock(g_launchWatcherMutex);
        g_lwBlockedNames = normalizedSet(blockedNames);
        g_lwBlockedBundleIds = normalizedSet(blockedBundleIdentifiers);
        g_lwAllowedNames = normalizedSet(allowedNames);
        g_lwAllowedBundleIds = normalizedSet(allowedBundleIdentifiers);
        g_lwAllowedPaths = normalizedSet(allowedExecutablePaths);
    }

    performOnMainThreadSync(^{
        if (g_launchObserver != nil) {
            return;  // policy already refreshed above; observer stays installed
        }
        g_launchObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
            addObserverForName:NSWorkspaceDidLaunchApplicationNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            NSRunningApplication *application = note.userInfo[NSWorkspaceApplicationKey];
            if (!application ||
                application.activationPolicy != NSApplicationActivationPolicyRegular) {
                return;
            }
            if (!launchShouldBlockLocked(application)) {
                return;
            }
            // Reap the disallowed app before it can settle / take focus (the media
            // key launching Apple Music is the canonical case). We deliberately do
            // NOT force FocusOS back to front or re-assert DisableProcessSwitching
            // here: during a routine FocusOS lives in its own fullscreen Space and
            // the user is meant to move freely between it and the routine's allowed
            // app windows — yanking focus would fight that navigation. Killing the
            // launch is the enforcement; the Space the user is on stays put.
            [application terminate];
        }];
    });
}

void stopLaunchWatcher()
{
    performOnMainThreadSync(^{
        if (g_launchObserver != nil) {
            [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:g_launchObserver];
            g_launchObserver = nil;
        }
    });
    std::lock_guard<std::mutex> lock(g_launchWatcherMutex);
    g_lwBlockedNames.clear();
    g_lwBlockedBundleIds.clear();
    g_lwAllowedNames.clear();
    g_lwAllowedBundleIds.clear();
    g_lwAllowedPaths.clear();
}

bool startExecBlocker(const QStringList &blockedNames,
                      const QStringList &blockedBundleIdentifiers,
                      const QStringList &allowedNames,
                      const QStringList &allowedBundleIdentifiers,
                      const QStringList &allowedExecutablePaths,
                      QString *errorMessage)
{
#if FOCUSOS_ENDPOINT_SECURITY_AVAILABLE
    return execBlocker().start(blockedNames,
                               blockedBundleIdentifiers,
                               allowedNames,
                               allowedBundleIdentifiers,
                               allowedExecutablePaths,
                               errorMessage);
#else
    if (errorMessage) {
        *errorMessage = QStringLiteral("Endpoint Security framework is unavailable in this SDK/build. AUTH_EXEC blocking requires macOS EndpointSecurity and the com.apple.developer.endpoint-security.client entitlement.");
    }
    Q_UNUSED(blockedNames);
    Q_UNUSED(blockedBundleIdentifiers);
    Q_UNUSED(allowedNames);
    Q_UNUSED(allowedBundleIdentifiers);
    Q_UNUSED(allowedExecutablePaths);
    return false;
#endif
}

void stopExecBlocker()
{
#if FOCUSOS_ENDPOINT_SECURITY_AVAILABLE
    execBlocker().stop();
#endif
}

QString buildNetworkFilterRuleset(const QStringList &allowedHosts)
{
    // Pure value-type work (QHostInfo::fromName, string building) — safe to run
    // on a QtConcurrent worker thread. No QProcess / QObject here.
    return buildPfRuleset(allowedHosts);
}

bool commitNetworkFilter(const QString &ruleset, QString *errorMessage)
{
    if (ruleset.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Refusing to install an empty pf ruleset");
        }
        return false;
    }

    // Load our ruleset and enable pf in one step, piping the rules in on stdin
    // (`-f -`) so we never have to write a root-owned file from an unprivileged
    // process — runPfctl runs pfctl as root directly or via the scoped `sudo -n`
    // rule. `-E` is reference-counted, so it composes with any pf already running
    // and hands back a token; we don't track the token because dropNetworkFilter()
    // restores Apple's default ruleset and disables pf outright at routine end.
    return runPfctl({QStringLiteral("-E"), QStringLiteral("-f"), QStringLiteral("-")},
                    errorMessage, ruleset.toUtf8());
}

bool applyNetworkFilter(const QStringList &allowedHosts, QString *errorMessage)
{
    return commitNetworkFilter(buildNetworkFilterRuleset(allowedHosts), errorMessage);
}

void dropNetworkFilter()
{
    // Restore Apple's stock ruleset, then disable pf. On a personal/research box
    // pf is normally off, so this returns the machine to its baseline. (If the
    // user runs their own pf config they should re-enable it after the routine.)
    if (QFileInfo::exists(kApplePfConf)) {
        runPfctl({QStringLiteral("-f"), kApplePfConf}, nullptr);
    } else {
        runPfctl({QStringLiteral("-F"), QStringLiteral("all")}, nullptr);
    }
    runPfctl({QStringLiteral("-d")}, nullptr);
}

} // namespace MacBackendNative
