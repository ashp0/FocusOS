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
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

// Last-ditch cleanup so a crash doesn't strand the user behind an nftables
// allowlist (which is what bricked the wifi after the engage-time KWin DBus
// abort).
//
// A signal handler may only call async-signal-safe functions — NOT Qt /
// QProcess / malloc, which the old version did (it called backend methods that
// fork QProcess and allocate). If the crash held the allocator lock, that path
// could deadlock or re-crash, leaving the firewall up. So we do the teardown the
// safe way: fork()+execve() pre-resolved binaries. The absolute paths are
// resolved ONCE at install time (below) into static buffers, because resolving
// them at signal time (QStandardPaths / execvp PATH search) is itself unsafe.
//
// FUTURE: when we move to a focusos compositor / supervisor model, this
// belongs in the supervisor process which can't crash with the policy on.
static char g_nftPath[4096] = {0};
static char g_pkillPath[4096] = {0};
// Mutable, static-storage argv tokens (string literals would need an unsafe
// const-cast to pass to execve's char *const argv[]).
static char g_argDelete[] = "delete";
static char g_argTable[] = "table";
static char g_argInet[] = "inet";
static char g_argFocusos[] = "focusos";
static char g_argF[] = "-f";
static char g_argDashDash[] = "--";
static char g_argWho[] = "--who=FocusOS";

static void focusosForkExec(char *const argv[])
{
    const pid_t pid = fork();
    if (pid == 0) {
        execve(argv[0], argv, environ);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0); // waitpid is async-signal-safe
    }
}

static void focusosFatalSignalHandler(int signum)
{
    // Delete our nftables table so a crash never strands the user behind the
    // outbound allowlist.
    if (g_nftPath[0] != '\0') {
        char *argv[] = {g_nftPath, g_argDelete, g_argTable, g_argInet, g_argFocusos, nullptr};
        focusosForkExec(argv);
    }
    // Release our display-sleep inhibitor (the systemd-inhibit helper runs
    // detached and outlives us; without this they pile up across crashes and
    // block idle/sleep). Matches LinuxBackend::releaseDisplaySleepInhibitors.
    if (g_pkillPath[0] != '\0') {
        char *argv[] = {g_pkillPath, g_argF, g_argDashDash, g_argWho, nullptr};
        focusosForkExec(argv);
    }
    // Do not launch plasmashell as a crash fallback in kiosk mode — the external
    // watchdog respawns FocusOS; a desktop shell here would be an escape surface.
    std::signal(signum, SIG_DFL);
    std::raise(signum);
}

static void installCrashCleanupHandlers(PlatformBackend *backend)
{
    Q_UNUSED(backend);
    // Resolve the helper binaries now (safe: we're not in a signal handler yet)
    // and stash absolute paths for the handler to execve directly.
    const auto stash = [](const QString &program, char *dest, size_t destSize) {
        const QByteArray path = QStandardPaths::findExecutable(program).toLocal8Bit();
        if (!path.isEmpty() && static_cast<size_t>(path.size()) < destSize) {
            std::memcpy(dest, path.constData(), static_cast<size_t>(path.size()) + 1);
        }
    };
    stash(QStringLiteral("nft"), g_nftPath, sizeof(g_nftPath));
    stash(QStringLiteral("pkill"), g_pkillPath, sizeof(g_pkillPath));

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

    // The KGlobalAccel registration below is inert unless the global-shortcuts
    // daemon is running, and a bare kwin_wayland session starts no Plasma
    // daemons — so bring it up first. Without this the volume/brightness keys
    // only fire while FocusOS has focus. No-op on macOS.
    backend.ensureGlobalShortcutsDaemon();

    // Claim the volume/brightness media keys session-wide so they work over a
    // focused routine app, not just inside the FocusOS shell. No-op on builds
    // without KF6GlobalAccel (e.g. macOS).
    MediaKeys mediaKeys(&systemStatus);

    QObject::connect(&routineManager, &RoutineManager::activeChanged, &musicEngine, [&routineManager, &musicEngine] {
        // Each routine carries its own engage behavior (stop / low / same). Apply
        // it before flipping the engaged flag so the fade targets the right level.
        // This also runs when the behavior is changed live mid-session, since
        // setActiveRoutineMusicBehavior re-emits activeChanged.
        if (routineManager.active()) {
            musicEngine.setEngageBehavior(routineManager.activeRoutineMusicBehavior());
        }
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

    // Deep-idle sleep. The idle starfield appears after IdleMonitor's first
    // timeout; a couple of minutes further into idleness IdleMonitor flips
    // deepIdle — at which point we put the whole machine to sleep to save power:
    // pause the music, blank the panel, and ask the system to suspend. QML stops
    // the starfield (pure black) off the same deepIdle flag. On the way back out
    // (any input → deepIdle false) we resume the music and wake the panel.
    //
    // The system suspend is best-effort and slightly delayed so the music has a
    // moment to fade and the DPMS-off lands first; if suspend isn't permitted we
    // simply stay in this soft sleep (black, silent, idle) until the user returns.
    QObject::connect(&idleMonitor, &IdleMonitor::deepIdleChanged, &routineManager, [&] {
        if (idleMonitor.deepIdle()) {
            musicEngine.setSleeping(true);
            backend.sleepDisplay();
            // Whole-machine suspend is opt-in (deep_sleep_suspend, default off):
            // on hardware that can't resume from S3 (common on Mac/iMac) it never
            // wakes — the soft sleep above (display off + music off) is the safe
            // default and any input recovers it via the deepIdle=false branch.
            if (routineManager.deepSleepSuspend()) {
                QTimer::singleShot(700, &routineManager, [&backend] { backend.suspendSystem(); });
            }
        } else {
            backend.wakeDisplay();
            musicEngine.setSleeping(false);
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

    // Run the user's editable ~/.focusos/startup.sh once the shell is up, so the
    // few agents they want on the bare session — input remappers like Toshy, tray
    // agents — come up. (We deliberately do NOT replay ~/.config/autostart here:
    // a stray entry there can drag in the whole Plasma desktop on top of FocusOS.)
    // Guarded to run once per login session, not on watchdog respawns. No-op on macOS.
    backend.runSessionStartupItems();

    return app.exec();
}
