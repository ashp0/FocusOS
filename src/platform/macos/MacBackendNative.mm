#include "platform/macos/MacBackendNative.h"

#include "blocker/BlockerPolicy.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <NetworkExtension/NetworkExtension.h>

#if defined(FOCUSOS_HAS_ENDPOINT_SECURITY) && __has_include(<EndpointSecurity/EndpointSecurity.h>)
#define FOCUSOS_ENDPOINT_SECURITY_AVAILABLE 1
#import <EndpointSecurity/EndpointSecurity.h>
#else
#define FOCUSOS_ENDPOINT_SECURITY_AVAILABLE 0
#endif

#include <QFileInfo>

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

bool waitForNetworkExtensionStep(void (^starter)(void (^completion)(NSError *error)),
                                 NSError **outError,
                                 NSTimeInterval timeoutSeconds = 15.0)
{
    __block BOOL done = NO;
    __block NSError *stepError = nil;
    starter(^(NSError *error) {
        stepError = error;
        done = YES;
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeoutSeconds];
    while (!done && [deadline timeIntervalSinceNow] > 0) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
    }

    if (!done) {
        stepError = [NSError errorWithDomain:@"FocusOS.NetworkExtension"
                                        code:1
                                    userInfo:@{
                                        NSLocalizedDescriptionKey: @"Timed out waiting for Network Extension preferences"
                                    }];
    }

    if (outError) {
        *outError = stepError;
    }
    return stepError == nil;
}

QString networkExtensionErrorMessage(NSString *operation, NSError *error)
{
    const QString detail = error ? fromNSString([error localizedDescription])
                                 : QStringLiteral("unknown error");
    return QStringLiteral("%1 failed: %2. FocusOS must be signed with the Network Extension content-filter entitlement and include a com.focusos.shell.NetworkFilter provider.")
        .arg(fromNSString(operation), detail);
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
    NEFilterManager *manager = [NEFilterManager sharedManager];
    NSError *error = nil;
    if (!waitForNetworkExtensionStep(^(void (^completion)(NSError *)) {
            [manager loadFromPreferencesWithCompletionHandler:completion];
        }, &error)) {
        if (errorMessage) {
            *errorMessage = networkExtensionErrorMessage(@"Network filter preference load", error);
        }
        return false;
    }

    NEFilterProviderConfiguration *configuration = [[NEFilterProviderConfiguration alloc] init];
    configuration.filterSockets = YES;
    configuration.filterPackets = NO;
    NSString *mainBundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];
    if (mainBundleIdentifier.length == 0) {
        mainBundleIdentifier = @"com.focusos.shell";
    }
    configuration.filterDataProviderBundleIdentifier =
        [mainBundleIdentifier stringByAppendingString:@".NetworkFilter"];
    configuration.organization = @"FocusOS";
    configuration.serverAddress = @"FocusOS local content filter";
    configuration.vendorConfiguration = @{
        @"PolicyPath": toNSString(BlockerPolicy::policyFilePath()),
        @"AllowedHosts": toNSArray(allowedHosts),
        @"DefaultAction": @"drop-unlisted"
    };

    manager.localizedDescription = @"FocusOS Network Lock";
    manager.providerConfiguration = configuration;
    if (@available(macOS 10.15, *)) {
        manager.grade = NEFilterManagerGradeFirewall;
    }
    manager.enabled = YES;

    if (!waitForNetworkExtensionStep(^(void (^completion)(NSError *)) {
            [manager saveToPreferencesWithCompletionHandler:completion];
        }, &error)) {
        if (errorMessage) {
            *errorMessage = networkExtensionErrorMessage(@"Network filter preference save", error);
        }
        return false;
    }

    return true;
}

void dropNetworkFilter()
{
    NEFilterManager *manager = [NEFilterManager sharedManager];
    NSError *error = nil;
    if (!waitForNetworkExtensionStep(^(void (^completion)(NSError *)) {
            [manager loadFromPreferencesWithCompletionHandler:completion];
        }, &error, 5.0)) {
        return;
    }

    manager.enabled = NO;
    waitForNetworkExtensionStep(^(void (^completion)(NSError *)) {
        [manager saveToPreferencesWithCompletionHandler:completion];
    }, &error, 5.0);
}

} // namespace MacBackendNative
