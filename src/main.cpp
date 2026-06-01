#include "core/InspirationStore.h"
#include "core/IdleMonitor.h"
#include "core/MediaKeys.h"
#include "core/MusicEngine.h"
#include "core/NotesStore.h"
#include "core/RoutineManager.h"
#include "core/StatsStore.h"
#include "core/SystemStatus.h"
#include "core/TOTPEngine.h"
#include "core/Updater.h"
#include "blocker/BlockerHost.h"
#include "blocker/BlockerPolicy.h"
#include "platform/PlatformBackend.h"
#include "shell/ShellWindow.h"

#if defined(Q_OS_MACOS)
#include "platform/macos/MacBackend.h"
#elif defined(Q_OS_LINUX)
#include "platform/linux/LinuxBackend.h"
#endif

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>

#include <csignal>
#include <cstdlib>
#include <unistd.h>

// Last-ditch cleanup so a crash doesn't strand the user behind an nftables
// allowlist (which is what bricked the wifi after the engage-time KWin DBus
// abort). We only know the backend at runtime, so the handler keeps a static
// pointer set by main() once the backend is up.
//
// FUTURE: when we move to a focusos compositor / supervisor model, this
// belongs in the supervisor process which can't crash with the policy on.
static PlatformBackend *g_crashCleanupBackend = nullptr;

static void focusosFatalSignalHandler(int signum)
{
    if (g_crashCleanupBackend) {
        g_crashCleanupBackend->dropNetworkPolicy();
        // Release the display-sleep inhibitor too — otherwise the respawn
        // watchdog relaunches FocusOS while the orphaned systemd-inhibit/sleep
        // helper keeps holding the lock, and they pile up across crashes,
        // blocking idle/sleep indefinitely.
        g_crashCleanupBackend->releaseDisplaySleepInhibitors();
        // Do not launch plasmashell as a crash fallback in kiosk mode. The
        // external watchdog is responsible for respawning FocusOS; opening a
        // desktop shell here would create an escape surface during a lock.
    }
    std::signal(signum, SIG_DFL);
    std::raise(signum);
}

static void installCrashCleanupHandlers(PlatformBackend *backend)
{
    g_crashCleanupBackend = backend;
    std::signal(SIGSEGV, focusosFatalSignalHandler);
    std::signal(SIGABRT, focusosFatalSignalHandler);
    std::signal(SIGBUS, focusosFatalSignalHandler);
    std::signal(SIGFPE, focusosFatalSignalHandler);
    std::signal(SIGTERM, focusosFatalSignalHandler);
    std::signal(SIGINT, focusosFatalSignalHandler);
}
#endif

int main(int argc, char *argv[])
{
    // Native-host mode: the browser spawns this binary as the blocker
    // extension's native-messaging host. Detected via the explicit flag the
    // install wrapper passes, or the chrome-extension:// origin Chrome/Brave
    // always appends. Runs headless — branch out before any GUI / instance lock.
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--native-host")
            || arg.startsWith(QLatin1String("chrome-extension://"))) {
            return focusos::runBlockerHost();
        }
        // Ops/diagnostic hook: write the signed blocker rules from the CLI.
        //   focusos --write-policy <0|1> [host ...]
        // (Normal operation drives this from RoutineManager via the backends.)
        if (arg == QLatin1String("--write-policy")) {
            const bool active = (i + 1 < argc)
                && QString::fromLocal8Bit(argv[i + 1]) != QLatin1String("0");
            QStringList hosts;
            for (int j = i + 2; j < argc; ++j) {
                hosts << QString::fromLocal8Bit(argv[j]);
            }
            BlockerPolicy::write(active, hosts);
            return 0;
        }
    }

    QCoreApplication::setOrganizationName(QStringLiteral("FocusOS"));
    QCoreApplication::setApplicationName(QStringLiteral("FocusOS"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication app(argc, argv);

    const QString focusDataDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.focusos");
    QDir().mkpath(focusDataDir);
    QLockFile instanceLock(focusDataDir + QStringLiteral("/focusos.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
        return 0;
    }

#if defined(Q_OS_MACOS)
    MacBackend backend;
#elif defined(Q_OS_LINUX)
    LinuxBackend backend;
    installCrashCleanupHandlers(&backend);
    // Startup sweep: if a previous instance crashed (or was killed) before it
    // could release its display-sleep inhibitor, the respawn watchdog has just
    // relaunched us — clear any orphaned FocusOS inhibitor before this session
    // starts so the locks don't accumulate.
    backend.releaseDisplaySleepInhibitors();
#else
#error "FocusOS currently supports macOS and Linux backends only."
#endif

    RoutineManager routineManager(&backend);
    NotesStore notesStore;
    TOTPEngine totpEngine;
    MusicEngine musicEngine;
    StatsStore statsStore;
    SystemStatus systemStatus;
    InspirationStore inspirationStore;
    Updater updater;
    IdleMonitor idleMonitor;

    // Claim the volume/brightness media keys session-wide so they work over a
    // focused routine app, not just inside the FocusOS shell. No-op on builds
    // without KF6GlobalAccel (e.g. macOS).
    MediaKeys mediaKeys(&systemStatus);

    QObject::connect(&routineManager, &RoutineManager::activeChanged, &musicEngine, [&routineManager, &musicEngine] {
        musicEngine.setRoutineEngaged(routineManager.active());
    });
    QObject::connect(&routineManager,
                     &RoutineManager::routineSessionFinished,
                     &statsStore,
                     &StatsStore::recordRoutineSession);
    QObject::connect(&routineManager,
                     &RoutineManager::routineSessionProgress,
                     &statsStore,
                     &StatsStore::recordRoutineSessionProgress);
    QObject::connect(&routineManager,
                     &RoutineManager::routineSessionFinished,
                     &notesStore,
                     &NotesStore::onRoutineSessionFinished);
    QObject::connect(&routineManager, &RoutineManager::activeChanged, &notesStore, [&routineManager, &notesStore] {
        if (routineManager.active()) {
            notesStore.onRoutineEngaged(routineManager.activeRoutineId(), routineManager.activeRoutineName());
        }
    });
    // Any user input re-arms the unlock panel's 30-minute inactivity auto-lock.
    QObject::connect(&idleMonitor, &IdleMonitor::activity,
                     &routineManager, &RoutineManager::notifyActivity);
    // A keyboard press / window-focus change auto-resumes an idle pause (Task 4).
    QObject::connect(&idleMonitor, &IdleMonitor::resumeHint,
                     &routineManager, &RoutineManager::onResumeHint);
    // Idle detection (starfield screensaver + display-sleep-on-idle) only runs on
    // the home screen — never during an engaged routine, where a focused, idle
    // user is working rather than away. Auto-resume is unaffected (resumeHint is
    // event-driven, not tied to the idle timer).
    QObject::connect(&routineManager, &RoutineManager::activeChanged, &idleMonitor, [&] {
        idleMonitor.setSuppressed(routineManager.active());
    });
    idleMonitor.setSuppressed(routineManager.active()); // a resumed session may already be active

    // Display-sleep-on-idle. The idle starfield appears after IdleMonitor's
    // timeout (and only while FocusOS itself is focused); a short while later we
    // also blank the physical panel to save power. The next input wakes both.
    // Suppressed while a routine is actively holding the display awake so we
    // don't fight its keep-awake inhibitor.
    QTimer displaySleepTimer;
    displaySleepTimer.setSingleShot(true);
    displaySleepTimer.setInterval(2 * 60 * 1000); // 2 min after the idle screen
    QObject::connect(&displaySleepTimer, &QTimer::timeout, &routineManager, [&] {
        if (idleMonitor.idle() &&
            !(routineManager.active() && routineManager.displayStaysAwake())) {
            backend.sleepDisplay();
        }
    });
    QObject::connect(&idleMonitor, &IdleMonitor::idleChanged, &displaySleepTimer, [&] {
        if (idleMonitor.idle()) {
            displaySleepTimer.start();
        } else {
            displaySleepTimer.stop();
        }
    });

#if defined(Q_OS_LINUX)
    // Power-key → screen lock (Task 6). logind is configured (90-focusos-logind
    // .conf) so HandlePowerKey=lock: instead of powering the machine off, the
    // key makes logind emit the session's "Lock" signal. Wire that straight to
    // RoutineManager so it blanks the screen (and "Unlock" to restore). This is
    // best-effort: if the session object can't be resolved nothing is wired and
    // the in-app LOCK SCREEN button still works.
    {
        QDBusConnection systemBus = QDBusConnection::systemBus();
        QDBusInterface logindManager(QStringLiteral("org.freedesktop.login1"),
                                     QStringLiteral("/org/freedesktop/login1"),
                                     QStringLiteral("org.freedesktop.login1.Manager"),
                                     systemBus);
        QString sessionPath;
        QDBusReply<QDBusObjectPath> byPid =
            logindManager.call(QStringLiteral("GetSessionByPID"), static_cast<quint32>(::getpid()));
        if (byPid.isValid()) {
            sessionPath = byPid.value().path();
        } else {
            const QByteArray sessionId = qgetenv("XDG_SESSION_ID");
            if (!sessionId.isEmpty()) {
                QDBusReply<QDBusObjectPath> bySid =
                    logindManager.call(QStringLiteral("GetSession"), QString::fromLocal8Bit(sessionId));
                if (bySid.isValid()) {
                    sessionPath = bySid.value().path();
                }
            }
        }
        if (!sessionPath.isEmpty()) {
            systemBus.connect(QStringLiteral("org.freedesktop.login1"), sessionPath,
                              QStringLiteral("org.freedesktop.login1.Session"),
                              QStringLiteral("Lock"), &routineManager, SLOT(lockScreen()));
            systemBus.connect(QStringLiteral("org.freedesktop.login1"), sessionPath,
                              QStringLiteral("org.freedesktop.login1.Session"),
                              QStringLiteral("Unlock"), &routineManager, SLOT(unlockScreen()));
        }
    }
#endif

    ShellWindow window(&routineManager,
                       &notesStore,
                       &totpEngine,
                       &musicEngine,
                       &statsStore,
                       &systemStatus,
                       &inspirationStore,
                       &updater,
                       &idleMonitor);
    window.showFocusShell();

    return app.exec();
}
