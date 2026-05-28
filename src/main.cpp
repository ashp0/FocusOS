#include "core/InspirationStore.h"
#include "core/MusicEngine.h"
#include "core/NotesStore.h"
#include "core/RoutineManager.h"
#include "core/StatsStore.h"
#include "core/SystemStatus.h"
#include "core/TOTPEngine.h"
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

#if defined(Q_OS_LINUX)
#include <csignal>
#include <cstdlib>

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
        // Restore the desktop shell on the way down — otherwise the user is
        // staring at a black screen with no launcher after the abort.
        g_crashCleanupBackend->launchDesktopShell(nullptr);
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

    ShellWindow window(&routineManager,
                       &notesStore,
                       &totpEngine,
                       &musicEngine,
                       &statsStore,
                       &systemStatus,
                       &inspirationStore);
    window.showFocusShell();

    return app.exec();
}
