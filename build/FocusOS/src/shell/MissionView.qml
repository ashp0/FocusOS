import QtQuick
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property string headerFont
    property string bodyFont
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
                color: routineManager.paused ? "#33d0c068" : "#33c0392b"
                border.width: 1
                border.color: routineManager.paused ? Theme.gold : Theme.crimsonHot
                radius: 2

                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: routineManager.paused ? "▮▮ MISSION PAUSED" : "● MISSION ACTIVE"
                    color: routineManager.paused ? Theme.gold : Theme.crimsonHot
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

        // ────────── Trajectory bar (with ticks + spacecraft marker) ──────────
        Item {
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

        Item { width: parent.width; height: 8 }

        // Action buttons
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Rectangle {
                width: 160
                height: 40
                color: pauseHover.containsMouse ? Theme.steel : "#33141420"
                border.width: 1
                border.color: pauseHover.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    anchors.centerIn: parent
                    text: routineManager.paused ? "▶ RESUME (␣)" : "⏸ PAUSE (␣)"
                    color: pauseHover.containsMouse ? Theme.gold : Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 2
                }

                MouseArea {
                    id: pauseHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.togglePause()
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
