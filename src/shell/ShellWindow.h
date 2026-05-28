#pragma once

#include <QQuickView>

class InspirationStore;
class NotesStore;
class MusicEngine;
class RoutineManager;
class StatsStore;
class SystemStatus;
class TOTPEngine;

class ShellWindow final : public QQuickView
{
    Q_OBJECT

public:
    ShellWindow(RoutineManager *routineManager,
                NotesStore *notesStore,
                TOTPEngine *totpEngine,
                MusicEngine *musicEngine,
                StatsStore *statsStore,
                SystemStatus *systemStatus,
                InspirationStore *inspirationStore);

    void showFocusShell();

private:
    void showWallpaper();
    void setRootWindowBackground();
    void minimizeFocusShell();

    QQuickView m_wallpaperWindow;
};
