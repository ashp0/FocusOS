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

    // The border is exactly two layers, both drawn ONCE around the perimeter (no
    // stacking, no per-corner duplicates — the four-corner fill used to read as a
    // doubled border on the Linux/Wayland overlay):
    //   1. a thin, slightly-transparent yellow border all the way around — the
    //      always-present track that shows the indicator is live; and
    //   2. a thicker, more-opaque yellow border that fills in CLOCKWISE from the
    //      top-left, its length proportional to elapsed time.
    // In open-ended momentum there's no total, so layer 2 is hidden and the thin
    // border just breathes; while paused both go quiet and the sweep below takes
    // over.
    property real trackWidth: 3
    property real trackOpacity: 0.3

    // Mode-resolved look for the thin perimeter, shared by all four edges:
    //  • work mode  → breathing opacity + cycling yellow shades
    //  • paused     → flashing (indeterminate "unpause me" pulse)
    //  • countdown  → the steady faint track
    property color trackColor: root.workMode ? root.workColor : "#E8A020"
    property real trackDrawOpacity: !root.showBorder
                                    ? 0
                                    : (root.workMode ? root.workOpacity
                                       : (root.pausedMode ? root.pausedFlash : root.trackOpacity))

    // The thicker progress border is a single clockwise trace, so its length is
    // distributed around the perimeter (top → right → bottom → left) rather than
    // grown from all four corners at once. Only shown for a real countdown.
    property bool showProgressFill: root.live && !root.pausedMode && !root.workMode
                                    && root.progressValue > 0
    readonly property real perimeter: 2 * (width + height)
    readonly property real filledLength: root.progressValue * perimeter
    function segment(consumedBefore, edgeLength) {
        return Math.max(0, Math.min(edgeLength, root.filledLength - consumedBefore))
    }

    // ── Layer 1: the thin border, all four edges ──
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

    // ── Layer 2: the thicker progress border, traced clockwise ──
    // Top edge, left → right.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.segment(0, parent.width)
        height: root.barWidth
        color: "#E8A020"
        opacity: root.showProgressFill ? root.barOpacity : 0
    }
    // Right edge, top → bottom.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.barWidth
        height: root.segment(parent.width, parent.height)
        color: "#E8A020"
        opacity: root.showProgressFill ? root.barOpacity : 0
    }
    // Bottom edge, right → left.
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.segment(parent.width + parent.height, parent.width)
        height: root.barWidth
        color: "#E8A020"
        opacity: root.showProgressFill ? root.barOpacity : 0
    }
    // Left edge, bottom → top.
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.barWidth
        height: root.segment(2 * parent.width + parent.height, parent.height)
        color: "#E8A020"
        opacity: root.showProgressFill ? root.barOpacity : 0
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
