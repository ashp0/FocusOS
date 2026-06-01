import QtQuick

Item {
    id: root
    anchors.fill: parent

    property int sampledElapsedSeconds: 0
    property int sampledTotalSeconds: 0
    property real pulsePhase: 0
    property bool live: routineManager.active && routineManager.overlayProgressEnabled

    property real progressValue: sampledTotalSeconds > 0
                                 ? Math.max(0, Math.min(1, sampledElapsedSeconds / sampledTotalSeconds))
                                 : 0
    property real lateProgress: progressValue <= 0.8 ? 0 : Math.min(1, (progressValue - 0.8) / 0.2)
    property real barOpacity: progressValue <= 0.8
                              ? 0.18 + (0.55 - 0.18) * (progressValue / 0.8)
                              : 0.55 + Math.sin(pulsePhase) * 0.15
    property real barWidth: 3 + lateProgress * 2

    function sampleProgress() {
        sampledElapsedSeconds = routineManager.elapsedSeconds
        sampledTotalSeconds = routineManager.activeRoutineTotalSeconds
    }

    onLiveChanged: {
        if (live) {
            pulsePhase = 0
            sampleProgress()
            progressTimer.restart()
        } else {
            progressTimer.stop()
            pulseTimer.stop()
        }
    }

    Timer {
        id: progressTimer
        interval: 1000
        running: root.live
        repeat: true
        onTriggered: root.sampleProgress()
    }

    Timer {
        id: pulseTimer
        interval: 100
        running: root.live && root.progressValue >= 0.8
        repeat: true
        onTriggered: root.pulsePhase += Math.PI / 10
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.barWidth
        height: parent.height * root.progressValue
        color: "#E8A020"
        opacity: root.live ? root.barOpacity : 0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        width: parent.width * root.progressValue
        height: root.barWidth
        color: "#E8A020"
        opacity: root.live ? root.barOpacity : 0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.barWidth
        height: parent.height * root.progressValue
        color: "#E8A020"
        opacity: root.live ? root.barOpacity : 0
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: parent.width * root.progressValue
        height: root.barWidth
        color: "#E8A020"
        opacity: root.live ? root.barOpacity : 0
    }

    // Manual-pause reminder (Task 4). This window is always-on-top and stays
    // visible over the routine apps even when the FocusOS shell is minimized, so
    // a manual pause can't be silently forgotten. Click-through (the overlay is
    // input-transparent) — the user resumes from the FocusOS shell.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 18
        width: pausePill.implicitWidth + 56
        height: 50
        visible: routineManager.pauseMode === 2
        color: "#dd1a0608"
        border.width: 2
        border.color: "#C0392B"

        SequentialAnimation on opacity {
            running: routineManager.pauseMode === 2
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.5; duration: 700; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 0.5; to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
        }

        Text {
            id: pausePill
            anchors.centerIn: parent
            text: "⏸  TIMER MANUALLY PAUSED  —  RESUME IN FOCUSOS"
            color: "#F5F0E8"
            font.pixelSize: 14
            font.letterSpacing: 2
        }
    }
}
