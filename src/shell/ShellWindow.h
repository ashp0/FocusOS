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
    // True while the shell is intentionally minimized (a routine's apps, the
    // desktop shell, or temporary access are in front). The visibilityChanged
    // guard must not drag the shell back to fullscreen while this holds, or the
    // user can never reach the windows we just got out of the way for.
    bool m_shellShouldHide = false;
};
