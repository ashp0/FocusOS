import QtQuick

Item {
    id: root
    anchors.fill: parent

    property int sampledElapsedSeconds: 0
    property int sampledTotalSeconds: 0
    property real pulsePhase: 0
    property bool live: routineManager.active && routineManager.overlayProgressEnabled

    // The overlay window is mapped whenever a routine is active and either the
    // progress border is enabled OR the timer is (manually) paused (so the paused
    // reminder still reaches over the apps). The border modes below key off these.
    property bool showBorder: routineManager.active &&
                              (routineManager.overlayProgressEnabled || routineManager.paused)
    // Indefinite "Continue" work mode: no countdown, just a breathing border that
    // signals active momentum (pulsing transparency + cycling yellow shades).
    property bool workMode: routineManager.active && routineManager.openEnded && !routineManager.paused
    // Paused (idle or manual): the border becomes an indeterminate loading sweep
    // to prompt the user to unpause. A manual pause (pauseMode 2) is the headline
    // case but an idle-click pause shows it too.
    property bool pausedMode: routineManager.active && routineManager.paused

    // ── Work-mode breathing (Task: indefinite session) ──
    property real workOpacity: 0.5
    property color workColor: "#E8A020"
    SequentialAnimation {
        running: root.workMode
        loops: Animation.Infinite
        ParallelAnimation {
            NumberAnimation { target: root; property: "workOpacity"; from: 0.30; to: 0.72; duration: 2000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "workColor"; from: "#E8A020"; to: "#F4D03F"; duration: 2000 }
        }
        ParallelAnimation {
            NumberAnimation { target: root; property: "workOpacity"; from: 0.72; to: 0.30; duration: 2000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "workColor"; from: "#F4D03F"; to: "#C8821A"; duration: 2000 }
        }
        ParallelAnimation {
            NumberAnimation { target: root; property: "workOpacity"; from: 0.30; to: 0.72; duration: 2000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "workColor"; from: "#C8821A"; to: "#E8A020"; duration: 2000 }
        }
    }

    // ── Paused flashing (Task: manual pause indeterminate state) ──
    property real pausedFlash: 0.5
    SequentialAnimation {
        running: root.pausedMode
        loops: Animation.Infinite
        NumberAnimation { target: root; property: "pausedFlash"; from: 0.14; to: 0.78; duration: 620; easing.type: Easing.InOutQuad }
        NumberAnimation { target: root; property: "pausedFlash"; from: 0.78; to: 0.14; duration: 620; easing.type: Easing.InOutQuad }
    }

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

    // Mode-resolved perimeter look, shared by all four edge tracks:
    //  • work mode  → breathing opacity + cycling yellow shades
    //  • paused     → flashing (indeterminate "unpause me" pulse)
    //  • countdown  → the steady faint track
    property color trackColor: root.workMode ? root.workColor : "#E8A020"
    property real trackDrawOpacity: !root.showBorder
                                    ? 0
                                    : (root.workMode ? root.workOpacity
                                       : (root.pausedMode ? root.pausedFlash : root.trackOpacity))

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth
        color: root.trackColor
        opacity: root.trackDrawOpacity
    }
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth
        color: root.trackColor
        opacity: root.trackDrawOpacity
    }
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: root.trackWidth
        color: root.trackColor
        opacity: root.trackDrawOpacity
    }
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.trackWidth
        color: root.trackColor
        opacity: root.trackDrawOpacity
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.barWidth
        height: parent.height * root.progressValue
        color: root.trackColor
        opacity: (root.live && !root.pausedMode) ? root.barOpacity : 0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        width: parent.width * root.progressValue
        height: root.barWidth
        color: root.trackColor
        opacity: (root.live && !root.pausedMode) ? root.barOpacity : 0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.barWidth
        height: parent.height * root.progressValue
        color: root.trackColor
        opacity: (root.live && !root.pausedMode) ? root.barOpacity : 0
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: parent.width * root.progressValue
        height: root.barWidth
        color: root.trackColor
        opacity: (root.live && !root.pausedMode) ? root.barOpacity : 0
    }

    // Indeterminate "loading" sweep while paused. A bright segment tracks back
    // and forth along the top and bottom edges — the unmistakable "this is paused,
    // unpause to continue" cue (paired with the flashing perimeter above). The
    // top and bottom segments travel in opposition so the motion reads clearly.
    Item {
        id: sweepTop
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth + 1
        visible: root.pausedMode
        clip: true

        Rectangle {
            id: sweepTopSeg
            width: Math.max(140, sweepTop.width * 0.22)
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: "#F4D03F" }
                GradientStop { position: 1.0; color: "transparent" }
            }
            SequentialAnimation on x {
                running: root.pausedMode
                loops: Animation.Infinite
                NumberAnimation { from: -sweepTopSeg.width; to: sweepTop.width; duration: 1500; easing.type: Easing.InOutSine }
                NumberAnimation { from: sweepTop.width; to: -sweepTopSeg.width; duration: 1500; easing.type: Easing.InOutSine }
            }
        }
    }

    Item {
        id: sweepBottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.trackWidth + 1
        visible: root.pausedMode
        clip: true

        Rectangle {
            id: sweepBottomSeg
            width: Math.max(140, sweepBottom.width * 0.22)
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: "#F4D03F" }
                GradientStop { position: 1.0; color: "transparent" }
            }
            SequentialAnimation on x {
                running: root.pausedMode
                loops: Animation.Infinite
                NumberAnimation { from: sweepBottom.width; to: -sweepBottomSeg.width; duration: 1500; easing.type: Easing.InOutSine }
                NumberAnimation { from: -sweepBottomSeg.width; to: sweepBottom.width; duration: 1500; easing.type: Easing.InOutSine }
            }
        }
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
