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

    StarField {
        anchors.fill: parent
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
