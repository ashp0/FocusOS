#include "shell/ShellWindow.h"

#include "core/InspirationStore.h"
#include "core/MusicEngine.h"
#include "core/NotesStore.h"
#include "core/RoutineManager.h"
#include "core/StatsStore.h"
#include "core/SystemStatus.h"
#include "core/TOTPEngine.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>

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

    rootContext()->setContextProperty(QStringLiteral("routineManager"), routineManager);
    rootContext()->setContextProperty(QStringLiteral("notesStore"), notesStore);
    rootContext()->setContextProperty(QStringLiteral("totpEngine"), totpEngine);
    rootContext()->setContextProperty(QStringLiteral("musicEngine"), musicEngine);
    rootContext()->setContextProperty(QStringLiteral("statsStore"), statsStore);
    rootContext()->setContextProperty(QStringLiteral("systemStatus"), systemStatus);
    rootContext()->setContextProperty(QStringLiteral("inspirationStore"), inspirationStore);

    loadFromModule(QStringLiteral("FocusOS"), QStringLiteral("Main"));
}

void ShellWindow::showFocusShell()
{
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        setScreen(screen);
        setGeometry(screen->geometry());
    }

    showFullScreen();
    raise();
    requestActivate();
}
