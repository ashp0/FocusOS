#include "shell/ShellWindow.h"

#include "core/InspirationStore.h"
#include "core/MusicEngine.h"
#include "core/NotesStore.h"
#include "core/RoutineManager.h"
#include "core/StatsStore.h"
#include "core/SystemStatus.h"
#include "core/TOTPEngine.h"
#include "core/Updater.h"
#include "core/IdleMonitor.h"

#include <QGuiApplication>
#include <QProcess>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include "platform/macos/MacBackendNative.h"
#endif

ShellWindow::ShellWindow(RoutineManager *routineManager,
                         NotesStore *notesStore,
                         TOTPEngine *totpEngine,
                         MusicEngine *musicEngine,
                         StatsStore *statsStore,
                         SystemStatus *systemStatus,
                         InspirationStore *inspirationStore,
                         Updater *updater,
                         IdleMonitor *idleMonitor)
{
    setColor(QColor(QStringLiteral("#050508")));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_routineManager = routineManager;

    rootContext()->setContextProperty(QStringLiteral("routineManager"), routineManager);
    rootContext()->setContextProperty(QStringLiteral("notesStore"), notesStore);
    rootContext()->setContextProperty(QStringLiteral("totpEngine"), totpEngine);
    rootContext()->setContextProperty(QStringLiteral("musicEngine"), musicEngine);
    rootContext()->setContextProperty(QStringLiteral("statsStore"), statsStore);
    rootContext()->setContextProperty(QStringLiteral("systemStatus"), systemStatus);
    rootContext()->setContextProperty(QStringLiteral("inspirationStore"), inspirationStore);
    rootContext()->setContextProperty(QStringLiteral("updater"), updater);
    rootContext()->setContextProperty(QStringLiteral("idleMonitor"), idleMonitor);

    loadFromModule(QStringLiteral("FocusOS"), QStringLiteral("Main"));

    // Global progress overlay: a transparent, click-through, always-on-top
    // Tool window that paints the routine countdown border on top of every
    // other window, even after the FocusOS shell minimizes during a routine.
    // Tool + no-focus keeps it out of the WM list / Mission Control.
    {
        QSurfaceFormat overlayFormat = m_progressOverlayWindow.format();
        overlayFormat.setAlphaBufferSize(8);
        m_progressOverlayWindow.setFormat(overlayFormat);
    }
    m_progressOverlayWindow.setColor(QColor(Qt::transparent));
    m_progressOverlayWindow.setResizeMode(QQuickView::SizeRootObjectToView);
    // Distinctive caption so the locked-down KWin window rule (kwinrulesrc) can
    // match *only* this window and force it to keep-above. Qt's
    // WindowStaysOnTopHint and QWindow::raise() are no-ops on Wayland, so the
    // server-side rule is the only thing that actually keeps the countdown
    // border on top of the routine apps. Must be set before the window maps.
    m_progressOverlayWindow.setTitle(QStringLiteral("FocusOS Focus Progress Overlay"));
    m_progressOverlayWindow.setFlags(Qt::Tool |
                                     Qt::FramelessWindowHint |
                                     Qt::WindowStaysOnTopHint |
                                     Qt::WindowTransparentForInput |
                                     Qt::WindowDoesNotAcceptFocus);
    m_progressOverlayWindow.rootContext()->setContextProperty(QStringLiteral("routineManager"), routineManager);
    m_progressOverlayWindow.loadFromModule(QStringLiteral("FocusOS"), QStringLiteral("ProgressOverlay"));

    connect(routineManager, &RoutineManager::activeChanged, this, &ShellWindow::updateProgressOverlay);
    connect(routineManager, &RoutineManager::overlayProgressEnabledChanged, this, &ShellWindow::updateProgressOverlay);
    // A manual pause must surface its banner over the routine apps even when the
    // progress overlay is otherwise disabled — keep the overlay window mapped.
    connect(routineManager, &RoutineManager::pauseModeChanged, this, &ShellWindow::updateProgressOverlay);
    updateProgressOverlay();

#if defined(Q_OS_LINUX)
    connect(routineManager, &RoutineManager::activeChanged, this, [this, routineManager] {
        if (routineManager->openEnded()) {
            // Open-ended continuation IS the FocusOS ambient screen — keep the
            // shell in front rather than minimizing behind the routine apps.
            showFocusShell();
        } else if (routineManager->active() && routineManager->activeRoutineHasLaunchTargets()) {
            QTimer::singleShot(350, this, &ShellWindow::minimizeFocusShell);
        } else if (!routineManager->active() && !routineManager->sessionPromptVisible()) {
            showFocusShell();
        }
    });
    connect(routineManager, &RoutineManager::sessionPromptChanged, this, [this, routineManager] {
        if (routineManager->sessionPromptVisible()) {
            showFocusShell();
        }
    });
    connect(routineManager, &RoutineManager::desktopShellChanged, this, [this, routineManager] {
        if (routineManager->desktopShellRunning()) {
            QTimer::singleShot(250, this, &ShellWindow::minimizeFocusShell);
        }
    });
    connect(routineManager, &RoutineManager::desktopAccessRequested, this, [this] {
        QTimer::singleShot(250, this, &ShellWindow::minimizeFocusShell);
    });
    connect(routineManager, &RoutineManager::accessChanged, this, [this, routineManager] {
        if (!routineManager->accessGranted() && !routineManager->active()) {
            showFocusShell();
        }
    });
    connect(this, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        // Only fight a WM-initiated restore when the shell is supposed to own
        // the screen. While m_shellShouldHide is set we are deliberately
        // minimized behind the routine's apps — re-showing here would yank the
        // user back into FocusOS and bury the app they just switched to. (It
        // would also defeat minimizeFocusShell itself: dropping the
        // stays-on-top flag flips the window through a transient windowed state
        // whose visibilityChanged would otherwise re-raise us before the
        // minimize lands.)
        if (!m_shellShouldHide && visibility != QWindow::Minimized &&
            visibility != QWindow::FullScreen && isVisible()) {
            QTimer::singleShot(0, this, &ShellWindow::showFocusShell);
        }
    });
#endif

#if defined(Q_OS_MACOS)
    // "Access Desktop" (TOTP-gated admin access): the backend has already lifted
    // the kiosk presentation + blockers, but the shell window is still the
    // fullscreen, above-the-menu-bar kiosk cover from coverScreenIncludingNotch.
    // Step it down into an ordinary window so the rest of the system is visible.
    connect(routineManager, &RoutineManager::desktopAccessRequested, this, [this] {
        QTimer::singleShot(200, this, &ShellWindow::enterDesktopAccessWindow);
    });
    // Access ended (END ACCESS): reclaim the fullscreen kiosk cover — whether we
    // land back on the home screen or on a still-running routine's MissionView
    // (in both cases the shell owns the screen again on macOS).
    connect(routineManager, &RoutineManager::accessChanged, this, [this, routineManager] {
        if (!routineManager->accessGranted()) {
            showFocusShell();
        }
    });
#endif
}

void ShellWindow::showFocusShell()
{
    m_shellShouldHide = false;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        setScreen(screen);
        setGeometry(screen->geometry());
    }

    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#if defined(Q_OS_MACOS)
    // Native showFullScreen() drops the window into the macOS "safe area" below
    // the notch, leaving a black bar across the top. Instead cover the whole
    // screen as a borderless floating window (the menu bar is hidden by the kiosk
    // presentation) and lift it over the notch strip so the starfield reaches the
    // top edge seamlessly around the notch.
    show();
    raise();
    requestActivate();
    MacBackendNative::coverScreenIncludingNotch(reinterpret_cast<void *>(winId()));
#else
    showFullScreen();
    raise();
    requestActivate();
#endif
    updateProgressOverlay();
}

#if defined(Q_OS_MACOS)
void ShellWindow::enterDesktopAccessWindow()
{
    // Intentionally stepping aside — keep any restore handler from dragging the
    // shell back to fullscreen while the authorized desktop window is open.
    m_shellShouldHide = true;

    // Ordinary decorated window: titled, movable, resizable, NOT stays-on-top —
    // a "regular, standard macOS window" the user can push aside to see the rest
    // of the system, instead of the fullscreen kiosk cover.
    setFlags(Qt::Window);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        const int w = qRound(avail.width() * 0.6);
        const int h = qRound(avail.height() * 0.7);
        setGeometry(avail.x() + (avail.width() - w) / 2,
                    avail.y() + (avail.height() - h) / 2,
                    w, h);
    }
    showNormal();
    raise();
    requestActivate();

    // Drop the above-the-menu-bar window level set by coverScreenIncludingNotch so
    // the shell sits among normal windows rather than floating over everything.
    MacBackendNative::restoreStandardWindowLevel(reinterpret_cast<void *>(winId()));
}
#endif

void ShellWindow::setRootWindowBackground()
{
#if defined(Q_OS_LINUX)
    const QString xsetroot = QStandardPaths::findExecutable(QStringLiteral("xsetroot"));
    if (xsetroot.isEmpty()) {
        return;
    }

    QProcess process;
    process.start(xsetroot, {QStringLiteral("-solid"), QStringLiteral("#0A0A0F")});
    if (!process.waitForFinished(250)) {
        process.kill();
        process.waitForFinished(50);
    }
#endif
}

void ShellWindow::updateProgressOverlay()
{
    const bool shouldShow = m_routineManager &&
                            m_routineManager->active() &&
                            (m_routineManager->overlayProgressEnabled() ||
                             m_routineManager->pauseMode() == 2);

    if (shouldShow) {
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            m_progressOverlayWindow.setScreen(screen);
            m_progressOverlayWindow.setGeometry(screen->geometry());
        }
        if (!m_progressOverlayWindow.isVisible()) {
#if defined(Q_OS_MACOS)
            m_progressOverlayWindow.show();
#else
            m_progressOverlayWindow.showFullScreen();
#endif
        }
        m_progressOverlayWindow.raise();
#if defined(Q_OS_MACOS)
        MacBackendNative::coverScreenIncludingNotch(
            reinterpret_cast<void *>(m_progressOverlayWindow.winId()));
#endif
    } else if (m_progressOverlayWindow.isVisible()) {
        m_progressOverlayWindow.hide();
    }
}

void ShellWindow::minimizeFocusShell()
{
    m_shellShouldHide = true;
#if defined(Q_OS_LINUX)
    // Get fully out of the way: the routine's apps own the screen now.
    //
    // We used to raise a fullscreen black "wallpaper" proxy here as a backdrop.
    // But on KWin/Wayland a Qt::Tool window flagged WindowStaysOnBottomHint is
    // NOT reliably kept below normal windows — it ended up *covering* the
    // launched app (you'd hear Brave but see only black) and leaked a phantom
    // entry into Alt+Tab. The launched apps fill the screen themselves, and
    // plasmashell is killed during a routine, so there's nothing to back-fill.
    setRootWindowBackground();
    setFlags(Qt::Window | Qt::FramelessWindowHint);
    showMinimized();
    lower();
    QTimer::singleShot(120, this, &ShellWindow::updateProgressOverlay);
#endif
}
