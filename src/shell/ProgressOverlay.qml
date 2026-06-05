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
    // Bright progress fill: clearly visible from the start (floor 0.55), ramping to
    // near-solid, then pulsing in the final 20%. The old 0.18 floor on a 3px line
    // was effectively invisible at the screen edge — this is the "I actually see it"
    // version the global overlay is supposed to be.
    property real barOpacity: progressValue <= 0.8
                              ? 0.55 + (0.9 - 0.55) * (progressValue / 0.8)
                              : 0.9 + Math.sin(pulsePhase) * 0.1
    property real barWidth: 6 + lateProgress * 4

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

    // Always-visible faint perimeter track. Without it the only thing drawn is the
    // progress fill below, whose length is proportional to elapsed time — so the
    // border was a near-invisible sliver early in a routine, and entirely invisible
    // for open-ended momentum (which has no total, hence progressValue === 0). The
    // track makes the global indicator visible the instant a routine is live, on
    // every Space; the bright fill grows over it as the countdown advances.
    property real trackWidth: 5
    property real trackOpacity: 0.34

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth
        color: "#E8A020"
        opacity: root.live ? root.trackOpacity : 0
    }
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth
        color: "#E8A020"
        opacity: root.live ? root.trackOpacity : 0
    }
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: root.trackWidth
        color: "#E8A020"
        opacity: root.live ? root.trackOpacity : 0
    }
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.trackWidth
        color: "#E8A020"
        opacity: root.live ? root.trackOpacity : 0
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
