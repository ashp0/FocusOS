#pragma once

#include <QQuickView>

class InspirationStore;
class NotesStore;
class MusicEngine;
class RoutineManager;
class StatsStore;
class SystemStatus;
class TOTPEngine;
class Updater;

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
                InspirationStore *inspirationStore,
                Updater *updater);

    void showFocusShell();

private:
    void showWallpaper();
    void setRootWindowBackground();
    void minimizeFocusShell();
    void updateProgressOverlay();

    QQuickView m_wallpaperWindow;
    QQuickView m_progressOverlayWindow;
    RoutineManager *m_routineManager = nullptr;
};
