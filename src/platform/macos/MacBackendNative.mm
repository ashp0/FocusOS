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
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <string>

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

const QString kPfRulesetPath = QStringLiteral("/etc/pf.anchors/focusos");
const QString kPfctl = QStringLiteral("/sbin/pfctl");
const QString kApplePfConf = QStringLiteral("/etc/pf.conf");

QString focusosPfRulesetPath()
{
    return kPfRulesetPath;
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
// the unprivileged failure (FocusOS not running as root) into actionable text.
bool runPfctl(const QStringList &arguments, QString *errorMessage)
{
    QProcess pfctl;
    pfctl.setProcessChannelMode(QProcess::MergedChannels);
    pfctl.start(kPfctl, arguments);
    if (!pfctl.waitForStarted(3000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to start /sbin/pfctl");
        }
        return false;
    }
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
        const bool unprivileged =
            output.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive) ||
            output.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive) ||
            output.contains(QStringLiteral("/dev/pf"), Qt::CaseInsensitive);
        if (errorMessage) {
            if (unprivileged) {
                *errorMessage = QStringLiteral(
                    "FocusOS needs root to drive the pf firewall. Launch it with sudo:\n"
                    "  sudo /Applications/FocusOS.app/Contents/MacOS/focusos\n"
                    "Strict mode will not start until the network lock can be installed.");
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

    switch (key) {
    case kKeySpace:                 // Spotlight (⌘-Space), Finder search (⌥⌘-Space)
        return cmd;
    case kKeyTab:                   // app switcher (⌘-Tab / ⌥⌘-Tab)
    case kKeyGrave:                 // window switcher (⌘-`)
        return cmd;
    case kKeyUpArrow:               // Mission Control (⌃-↑)
    case kKeyDownArrow:             // App Exposé (⌃-↓)
    case kKeyLeftArrow:             // move one Space left (⌃-←)
    case kKeyRightArrow:            // move one Space right (⌃-→)
        return ctrl;
    case kKeyF3:                    // Mission Control key
    case kKeyF4:                    // Launchpad key
        return true;
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
            [NSApplication sharedApplication];
            const NSApplicationPresentationOptions options =
                NSApplicationPresentationHideDock |
                NSApplicationPresentationHideMenuBar |
                NSApplicationPresentationDisableAppleMenu |
                NSApplicationPresentationDisableProcessSwitching |
                NSApplicationPresentationDisableForceQuit |
                NSApplicationPresentationDisableSessionTermination |
                NSApplicationPresentationDisableHideApplication;
            [NSApp setPresentationOptions:options];
            [NSApp activateIgnoringOtherApps:YES];
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

void leaveKioskPresentation()
{
    performOnMainThreadSync(^{
        @try {
            [NSApplication sharedApplication];
            [NSApp setPresentationOptions:NSApplicationPresentationDefault];
        } @catch (NSException *) {
        }
    });
}

bool isAccessibilityTrusted()
{
    return AXIsProcessTrusted();
}

bool startInputBlocker(QString *errorMessage)
{
    __block bool installed = false;
    __block bool trusted = false;
    __block QString error;
    performOnMainThreadSync(^{
        trusted = AXIsProcessTrusted();
        if (!trusted) {
            // Surface the system Accessibility prompt (shown once per app identity)
            // so the user has a one-click path to grant access. An event tap created
            // while untrusted exists but stays inert until access is granted AND the
            // app is relaunched — so we still install it, but report not-trusted.
            NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
            AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
        }
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

bool applyNetworkFilter(const QStringList &allowedHosts, QString *errorMessage)
{
    const QString ruleset = buildPfRuleset(allowedHosts);

    // Persist the ruleset where pfctl can read it (and where it survives for the
    // duration of the routine). /etc/pf.anchors is the conventional home for
    // add-on pf rules and already exists on every macOS install.
    QDir().mkpath(QFileInfo(focusosPfRulesetPath()).absolutePath());
    QSaveFile file(focusosPfRulesetPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write the pf ruleset to %1 (run FocusOS as root)")
                .arg(focusosPfRulesetPath());
        }
        return false;
    }
    file.write(ruleset.toUtf8());
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to save the pf ruleset to %1")
                .arg(focusosPfRulesetPath());
        }
        return false;
    }

    // Load our ruleset and enable pf in one step. `-E` is reference-counted, so
    // it composes with any pf already running and hands back a token; we don't
    // track the token because dropNetworkFilter() restores Apple's default
    // ruleset and disables pf outright at routine end.
    return runPfctl({QStringLiteral("-E"),
                     QStringLiteral("-f"), focusosPfRulesetPath()},
                    errorMessage);
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
