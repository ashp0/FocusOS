#pragma once

#include <QList>
#include <QObject>

class SystemStatus;
class PlatformBackend;
class QAction;

// MediaKeys claims the laptop/keyboard media keys — volume up/down/mute and
// brightness up/down — for the whole FocusOS session.
//
// Why this exists: FocusOS runs as a bare `kwin_wayland` shell with no
// plasmashell / plasma-pa / powerdevil. Those Plasma daemons are what normally
// own the XF86Audio* / XF86MonBrightness* keys, so in the FocusOS session the
// keys do nothing while a routine app (Brave, an IDE, …) is focused. A Wayland
// client only receives key events when it is focused, so handling them inside
// the QML shell (see Main.qml) only works while FocusOS itself is on top.
//
// The fix is a *global* shortcut registered with the compositor via KGlobalAccel
// (KWin grabs the key and dispatches it regardless of which window is focused).
// The handlers route to SystemStatus, which already knows how to drive PipeWire
// volume and backlight brightness.
//
// After a system sleep/resume the compositor can silently drop those global-key
// grabs (and kglobalacceld may have been restarted), leaving the keys dead until
// the next login. So MediaKeys also listens for logind's PrepareForSleep and
// re-claims every shortcut on resume — see reclaimShortcuts().
//
// When the build has no KF6GlobalAccel (e.g. the macOS build, or a Linux box
// without the dev package), this degrades to a no-op and the in-shell handler
// in Main.qml remains the only path — exactly the previous behaviour.
class MediaKeys final : public QObject
{
    Q_OBJECT

public:
    explicit MediaKeys(SystemStatus *systemStatus,
                       PlatformBackend *backend = nullptr,
                       QObject *parent = nullptr);

    // True when global shortcuts were actually registered with the compositor.
    bool active() const;

public slots:
    // Re-establish every global-key grab with the compositor. Idempotent — safe
    // to call repeatedly. Used on resume from sleep, where KWin can drop the
    // grabs out from under us.
    void reclaimShortcuts();

private slots:
    // logind PrepareForSleep(bool): true just before suspend, false right after
    // resume. We re-claim the media keys on the resume edge.
    void onPrepareForSleep(bool aboutToSleep);

private:
#if defined(FOCUSOS_HAS_KGLOBALACCEL)
    // A registered media key: the QAction KGlobalAccel persists, plus the Qt key
    // we bound it to so reclaimShortcuts() can re-apply the same default.
    struct Binding {
        QAction *action = nullptr;
        int qtKey = 0;
    };
    QAction *registerKey(const QString &id, const QString &displayName, int qtKey);
    void applyShortcut(const Binding &binding);
    QList<Binding> m_bindings;
#endif

    SystemStatus *m_systemStatus = nullptr;
    PlatformBackend *m_backend = nullptr;
    bool m_active = false;
};
