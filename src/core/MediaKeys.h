#pragma once

#include <QObject>

class SystemStatus;
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
// When the build has no KF6GlobalAccel (e.g. the macOS build, or a Linux box
// without the dev package), this degrades to a no-op and the in-shell handler
// in Main.qml remains the only path — exactly the previous behaviour.
class MediaKeys final : public QObject
{
    Q_OBJECT

public:
    explicit MediaKeys(SystemStatus *systemStatus, QObject *parent = nullptr);

    // True when global shortcuts were actually registered with the compositor.
    bool active() const;

private:
#if defined(FOCUSOS_HAS_KGLOBALACCEL)
    QAction *registerKey(const QString &id, const QString &displayName, int qtKey);
#endif

    SystemStatus *m_systemStatus = nullptr;
    bool m_active = false;
};
