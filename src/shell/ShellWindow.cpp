#include "shell/ShellWindow.h"

#include "core/InspirationStore.h"
#include "core/MusicEngine.h"
#include "core/NotesStore.h"
#include "core/RoutineManager.h"
#include "core/StatsStore.h"
#include "core/SystemStatus.h"
#include "core/TOTPEngine.h"

#include <QGuiApplication>
#include <QProcess>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

ShellWindow::ShellWindow(RoutineManager *routineManager,
                         NotesStore *notesStore,
                         TOTPEngine *totpEngine,
                         MusicEngine *musicEngine,
                         StatsStore *statsStore,
                         SystemStatus *systemStatus,
                         InspirationStore *inspirationStore)
{
    setColor(QColor(QStringLiteral("#050508")));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_wallpaperWindow.setColor(QColor(QStringLiteral("#0A0A0F")));
    m_wallpaperWindow.setResizeMode(QQuickView::SizeRootObjectToView);
    // Tool + bottom + no-focus keeps the wallpaper proxy out of the WM's
    // window list / Mission Control on KDE & GNOME — without Qt::Tool it
    // shows up as a phantom black "second FocusOS" entry.
    m_wallpaperWindow.setFlags(Qt::Tool |
                               Qt::FramelessWindowHint |
                               Qt::WindowStaysOnBottomHint |
                               Qt::WindowDoesNotAcceptFocus);

    rootContext()->setContextProperty(QStringLiteral("routineManager"), routineManager);
    rootContext()->setContextProperty(QStringLiteral("notesStore"), notesStore);
    rootContext()->setContextProperty(QStringLiteral("totpEngine"), totpEngine);
    rootContext()->setContextProperty(QStringLiteral("musicEngine"), musicEngine);
    rootContext()->setContextProperty(QStringLiteral("statsStore"), statsStore);
    rootContext()->setContextProperty(QStringLiteral("systemStatus"), systemStatus);
    rootContext()->setContextProperty(QStringLiteral("inspirationStore"), inspirationStore);

    loadFromModule(QStringLiteral("FocusOS"), QStringLiteral("Main"));

#if defined(Q_OS_LINUX)
    connect(routineManager, &RoutineManager::activeChanged, this, [this, routineManager] {
        if (routineManager->active() && routineManager->activeRoutineHasLaunchTargets()) {
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
        if (visibility != QWindow::Minimized && visibility != QWindow::FullScreen && isVisible()) {
            QTimer::singleShot(0, this, &ShellWindow::showFocusShell);
        }
    });
#endif
}

void ShellWindow::showFocusShell()
{
#if defined(Q_OS_LINUX)
    // When the FocusOS shell is fullscreen it already covers the screen, so
    // the wallpaper proxy is just dead weight in the WM. Hide it; we only
    // need it when FocusOS minimizes during a routine.
    if (m_wallpaperWindow.isVisible()) {
        m_wallpaperWindow.hide();
    }
#endif
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        setScreen(screen);
        setGeometry(screen->geometry());
    }

    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    showFullScreen();
    raise();
    requestActivate();
}

void ShellWindow::showWallpaper()
{
#if defined(Q_OS_LINUX)
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        m_wallpaperWindow.setScreen(screen);
        m_wallpaperWindow.setGeometry(screen->geometry());
    }

    if (!m_wallpaperWindow.isVisible()) {
        m_wallpaperWindow.showFullScreen();
    }
    m_wallpaperWindow.lower();
#endif
}

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

void ShellWindow::minimizeFocusShell()
{
#if defined(Q_OS_LINUX)
    showWallpaper();
    setRootWindowBackground();
    setFlags(Qt::Window | Qt::FramelessWindowHint);
    showMinimized();
    m_wallpaperWindow.lower();
#endif
}
