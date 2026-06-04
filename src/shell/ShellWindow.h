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
class IdleMonitor;

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
                Updater *updater,
                IdleMonitor *idleMonitor);

    void showFocusShell();

private:
    void setRootWindowBackground();
    void minimizeFocusShell();
    void updateProgressOverlay();
#if defined(Q_OS_MACOS)
    // Active routine: FocusOS becomes a real native-fullscreen window in its own
    // Space so the routine's app windows stay reachable in the desktop Space.
    void enterRoutineFullScreen();
    // Home screen / completion prompt: the frameless above-everything kiosk cover.
    // Split out of showFocusShell() so it can be deferred behind the (animated)
    // native-fullscreen exit.
    void applyFramelessCover();
    // "Access Desktop" on macOS: step the shell out of its fullscreen routine /
    // kiosk cover into an ordinary, movable/resizable window so the rest of the
    // system (Dock, menu bar, other apps) is visible and usable. showFocusShell()
    // / enterRoutineFullScreen() reclaim the locked layout when access ends.
    void enterDesktopAccessWindow();
    void configureDesktopAccessWindow();
#endif

    QQuickView m_progressOverlayWindow;
    RoutineManager *m_routineManager = nullptr;
    // True while the shell is intentionally minimized (a routine's apps, the
    // desktop shell, or temporary access are in front). The visibilityChanged
    // guard must not drag the shell back to fullscreen while this holds, or the
    // user can never reach the windows we just got out of the way for.
    bool m_shellShouldHide = false;
};
