import QtQuick
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property string headerFont
    property string bodyFont
    // Open-ended continuation (Task 5): the timer has expired and the user
    // chose to keep going. No countdown — just ambient forward motion.
    property bool openEnded: routineManager.openEnded
    signal endRequested()

    function pad(value) {
        return value < 10 ? "0" + value : "" + value
    }

    function formatSecondsClock(seconds) {
        const value = Math.max(0, Number(seconds || 0))
        const hours = Math.floor(value / 3600)
        const minutes = Math.floor((value % 3600) / 60)
        const secs = value % 60
        return pad(hours) + ":" + pad(minutes) + ":" + pad(secs)
    }

    function progress() {
        if (routineManager.activeRoutineTotalSeconds <= 0) {
            return 0
        }
        return 1 - routineManager.remainingSeconds / routineManager.activeRoutineTotalSeconds
    }

    function progressPercent() {
        return Math.round(progress() * 100)
    }

    function breakStatusText() {
        const frequency = Number(routineManager.activeRoutineBreakFrequencyMinutes || 0)
        const duration = Number(routineManager.activeRoutineBreakDurationMinutes || 0)
        if (frequency <= 0 || duration <= 0) {
            return ""
        }
        const cycleSeconds = frequency * 60
        const elapsed = Math.max(0, Number(routineManager.elapsedSeconds || 0))
        const sinceBreak = elapsed % cycleSeconds
        const remaining = Math.max(0, cycleSeconds - sinceBreak)
        if (sinceBreak < duration * 60 && elapsed >= cycleSeconds) {
            return "REST WINDOW  ■  " + duration + "M AUTHORIZED"
        }
        return "NEXT REST CHECK  ■  " + root.formatSecondsClock(remaining)
    }

    // ────────── Faint constellation background ──────────
    Canvas {
        id: constellation
        anchors.fill: parent
        opacity: 0.55

        property var points: []

        function seed() {
            const count = 14
            const next = []
            for (let i = 0; i < count; ++i) {
                next.push({
                    x: 0.05 + Math.random() * 0.9,
                    y: 0.1 + Math.random() * 0.8,
                    r: 1.2 + Math.random() * 2,
                    alpha: 0.18 + Math.random() * 0.35
                })
            }
            points = next
            requestPaint()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            // Sparse connecting lines for constellation effect
            ctx.strokeStyle = "rgba(180,180,210,0.06)"
            ctx.lineWidth = 1
            for (let i = 0; i < points.length - 1; ++i) {
                if (Math.random() < 0.5) continue
                const a = points[i]
                const b = points[i + 1]
                ctx.beginPath()
                ctx.moveTo(a.x * width, a.y * height)
                ctx.lineTo(b.x * width, b.y * height)
                ctx.stroke()
            }
            // Stars
            for (let i = 0; i < points.length; ++i) {
                const p = points[i]
                ctx.fillStyle = "rgba(232, 220, 200, " + p.alpha + ")"
                ctx.beginPath()
                ctx.arc(p.x * width, p.y * height, p.r, 0, Math.PI * 2)
                ctx.fill()
                // diffraction spike
                ctx.strokeStyle = "rgba(232, 220, 200, " + (p.alpha * 0.35) + ")"
                const reach = p.r * 4.5
                ctx.beginPath()
                ctx.moveTo(p.x * width - reach, p.y * height)
                ctx.lineTo(p.x * width + reach, p.y * height)
                ctx.moveTo(p.x * width, p.y * height - reach)
                ctx.lineTo(p.x * width, p.y * height + reach)
                ctx.stroke()
            }
        }

        onWidthChanged: seed()
        onHeightChanged: seed()
        Component.onCompleted: seed()
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -10
        anchors.leftMargin: 40
        anchors.rightMargin: 40
        spacing: 14

        // Status pill
        Item {
            width: parent.width
            height: 22

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: statusText.implicitWidth + 28
                height: 22
                color: root.openEnded ? "#33e8a020" : (routineManager.paused ? "#33d0c068" : "#33c0392b")
                border.width: 1
                border.color: root.openEnded ? Theme.gold : (routineManager.paused ? Theme.gold : Theme.crimsonHot)
                radius: 2

                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: root.openEnded
                          ? "∞ MOMENTUM SUSTAINED"
                          : (routineManager.paused ? "▮▮ MISSION PAUSED" : "● MISSION ACTIVE")
                    color: root.openEnded ? Theme.gold : (routineManager.paused ? Theme.gold : Theme.crimsonHot)
                    font.family: root.headerFont
                    font.pixelSize: 11
                    font.letterSpacing: 0
                }
            }
        }

        // Mission name — large, centered
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: routineManager.activeRoutineName
            color: Theme.textPrimary
            elide: Text.ElideRight
            font.family: root.headerFont
            font.pixelSize: 44
            font.letterSpacing: 2
        }

        // Description
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: routineManager.activeRoutineDescription.length > 0
                  ? routineManager.activeRoutineDescription
                  : ""
            visible: text.length > 0
            color: Theme.goldDim
            wrapMode: Text.WordWrap
            font.family: root.bodyFont
            font.pixelSize: 14
            font.letterSpacing: 0
            lineHeight: 1.4
        }

        Item { width: parent.width; height: 16 }

        // Big T-MINUS countdown
        Item {
            visible: !root.openEnded
            width: parent.width
            height: countdownText.implicitHeight + tminusLabel.implicitHeight + 8

            Text {
                id: tminusLabel
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                text: routineManager.paused ? "PAUSED" : "T − MINUS"
                color: Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 13
                font.letterSpacing: 6
            }

            Text {
                id: countdownText
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: tminusLabel.bottom
                anchors.topMargin: 6
                text: root.formatSecondsClock(routineManager.remainingSeconds)
                color: routineManager.paused ? Theme.goldDim : Theme.gold
                font.family: root.headerFont
                font.pixelSize: 78
                font.letterSpacing: 2

                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }

        Item { width: parent.width; height: 8 }

        // ────────── Open-ended momentum (no countdown) ──────────
        // Ambient forward-motion: a drifting band of light implies indefinite
        // progress without showing any time. Visible only in continuation mode.
        Item {
            visible: root.openEnded
            width: parent.width
            height: 96

            Text {
                id: momentumLabel
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                text: "OPEN-ENDED FLIGHT"
                color: Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 13
                font.letterSpacing: 6
            }

            // Drifting flow band — a soft highlight that sweeps left→right
            // forever, the visual stand-in for "still moving forward".
            Rectangle {
                id: flowTrack
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.top: momentumLabel.bottom
                anchors.topMargin: 22
                height: 2
                color: Theme.textGhost
                clip: true

                Rectangle {
                    id: flowComet
                    width: Math.max(60, flowTrack.width * 0.18)
                    height: parent.height
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: Theme.gold }
                        GradientStop { position: 1.0; color: "transparent" }
                    }

                    SequentialAnimation on x {
                        running: root.openEnded
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: -flowComet.width
                            to: flowTrack.width
                            duration: 2600
                            easing.type: Easing.InOutSine
                        }
                    }
                }
            }

            // Travelling chevron beneath the band for a second motion cue.
            Text {
                anchors.top: flowTrack.bottom
                anchors.topMargin: 16
                text: "▶ ▶ ▶"
                color: Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 16
                font.letterSpacing: 8
                x: 20

                SequentialAnimation on opacity {
                    running: root.openEnded
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 1400; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 0.8; duration: 1400; easing.type: Easing.InOutQuad }
                }
            }
        }

        // ────────── Trajectory bar (with ticks + spacecraft marker) ──────────
        Item {
            visible: !root.openEnded
            width: parent.width
            height: 46

            // Base trajectory line
            Rectangle {
                id: trajectoryLine
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 2
                color: Theme.textGhost
            }

            // Filled section
            Rectangle {
                anchors.left: trajectoryLine.left
                anchors.verticalCenter: trajectoryLine.verticalCenter
                width: trajectoryLine.width * root.progress()
                height: 2
                color: routineManager.paused ? Theme.goldDim : Theme.gold
            }

            // Tick marks (5 segments)
            Repeater {
                model: 5
                delegate: Item {
                    required property int index
                    width: 1
                    height: 14
                    x: trajectoryLine.x + trajectoryLine.width * (index / 4) - 1
                    anchors.verticalCenter: trajectoryLine.verticalCenter

                    Rectangle {
                        anchors.fill: parent
                        color: (root.progress() * 4) >= parent.index ? Theme.gold : Theme.textGhost
                        opacity: 0.7
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.bottom
                        anchors.topMargin: 4
                        text: (parent.index * 25) + "%"
                        color: (root.progress() * 4) >= parent.index ? Theme.goldDim : Theme.textGhost
                        font.family: root.bodyFont
                        font.pixelSize: 9
                    }
                }
            }

            // Spacecraft marker (chevron) that travels along the line
            Item {
                id: spacecraft
                width: 14
                height: 14
                x: trajectoryLine.x + trajectoryLine.width * root.progress() - width / 2
                anchors.verticalCenter: trajectoryLine.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: "▶"
                    color: routineManager.paused ? Theme.goldDim : Theme.gold
                    font.pixelSize: 14
                }

                // Subtle bloom behind it
                Rectangle {
                    anchors.centerIn: parent
                    width: routineManager.paused ? 14 : 22
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.gold
                    opacity: routineManager.paused ? 0.25 : 0.5

                    SequentialAnimation on opacity {
                        running: !routineManager.paused
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.15; duration: 1600; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 0.55; duration: 1600; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }

        // Elapsed + percent
        Item {
            visible: !root.openEnded
            width: parent.width
            height: 36

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "ELAPSED " + root.formatSecondsClock(routineManager.elapsedSeconds)
                color: Theme.textDim
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 2
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                text: root.progressPercent() + "% COMPLETE"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 2
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "TOTAL " + root.formatSecondsClock(routineManager.activeRoutineTotalSeconds)
                color: Theme.textDim
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 2
            }
        }

        Rectangle {
            visible: !root.openEnded && root.breakStatusText().length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, breakText.implicitWidth + 34)
            height: 28
            color: "#221510"
            border.width: 1
            border.color: Theme.goldDim

            Text {
                id: breakText
                anchors.centerIn: parent
                text: root.breakStatusText()
                color: Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 11
                font.letterSpacing: 0
            }
        }

        Item { width: parent.width; height: 8 }

        // Action buttons
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Rectangle {
                visible: !root.openEnded
                width: 196
                height: 40
                color: pauseHover.containsMouse ? Theme.steel : "#33141420"
                border.width: 1
                // Manual pause border is crimson so it reads as "stay paused".
                border.color: routineManager.pauseMode === 2
                              ? Theme.crimsonHot
                              : (pauseHover.containsMouse ? Theme.gold : Theme.goldDim)

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: routineManager.pauseMode === 0
                              ? "⏸ PAUSE"
                              : (routineManager.pauseMode === 2 ? "▶ RESUME · MANUAL" : "▶ RESUME · IDLE")
                        color: routineManager.pauseMode === 2
                               ? Theme.crimsonHot
                               : (pauseHover.containsMouse ? Theme.gold : Theme.goldDim)
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 2
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: routineManager.pauseMode === 0
                        text: "DBL-CLICK = MANUAL"
                        color: Theme.goldDim
                        opacity: 0.7
                        font.family: root.bodyFont
                        font.pixelSize: 8
                        font.letterSpacing: 1
                    }
                }

                MouseArea {
                    id: pauseHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.togglePause()
                    onDoubleClicked: routineManager.manualPause()
                }
            }

            Rectangle {
                visible: routineManager.activeRoutineHasLaunchTargets
                width: 150
                height: 40
                color: relaunchHover.containsMouse ? Theme.steel : "#33141420"
                border.width: 1
                border.color: relaunchHover.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    anchors.centerIn: parent
                    text: "↻ RELAUNCH"
                    color: relaunchHover.containsMouse ? Theme.gold : Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }

                MouseArea {
                    id: relaunchHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.relaunchActiveRoutine()
                }
            }

            // Quick-open a single known document without navigating folders — the
            // fast path that doesn't make you double-click through the browser.
            // The file manager is killed during a routine, so this native picker
            // (executables / .desktop refused) is the only direct way to reach a
            // reference file the user didn't pre-load. Folder browsing lives in the
            // "📁 FILES" button in the bottom bar.
            Rectangle {
                width: 168
                height: 40
                color: openDocHover.containsMouse ? Theme.steel : "#33141420"
                border.width: 1
                border.color: openDocHover.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    anchors.centerIn: parent
                    text: "📄 OPEN DOC"
                    color: openDocHover.containsMouse ? Theme.gold : Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }

                MouseArea {
                    id: openDocHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.openDocumentInSession()
                }
            }

            Rectangle {
                width: 160
                height: 40
                color: endHover.containsMouse ? Theme.crimsonHot : "#33141420"
                border.width: 1
                border.color: endHover.containsMouse ? Theme.gold : Theme.crimson

                Text {
                    anchors.centerIn: parent
                    text: "⏹ END EARLY"
                    color: endHover.containsMouse ? Theme.gold : Theme.crimson
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 2
                }

                MouseArea {
                    id: endHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.endRequested()
                }
            }
        }
    }
}
