import QtQuick

// Idle / screensaver state: completely pitch black with only a starfield.
// No other UI. Shown on top of everything when IdleMonitor reports idle; the
// first interaction wakes the shell (and is swallowed so it doesn't click
// through to whatever is underneath).
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // The starfield runs during the first idle stage, then stops once the session
    // goes into deep sleep (panel blanked, music paused, machine suspending) — the
    // screen is fully black and nothing animates, so the app burns no CPU/GPU
    // while the display is off. It returns on the first input that wakes us.
    StarField {
        anchors.fill: parent
        visible: !idleMonitor.deepIdle
        // The screensaver must animate even when the bare-session shell doesn't
        // report itself as the active application (a routine isn't engaged here,
        // so FocusOS owns the screen anyway). Without this the field froze on the
        // iMac. It still stops at deep idle via `visible` above.
        ignoreApplicationActive: true
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onPositionChanged: idleMonitor.wake()
        onPressed: function(mouse) {
            idleMonitor.wake()
            mouse.accepted = true
        }
        onWheel: function(wheel) {
            idleMonitor.wake()
            wheel.accepted = true
        }
    }
}
