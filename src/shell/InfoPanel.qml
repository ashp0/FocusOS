import QtQuick
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property string headerFont
    property string bodyFont
    property bool graphRevealed: false
    property int graphVisibleDays: 14
    property string selectedSessionId: ""
    property bool sessionsExpanded: false
    property bool todayNotesExpanded: false
    property int todayNotesMinHeight: 260
    property int todayNotesMaxHeight: 540
    property int sessionListMinHeight: 280
    property int sessionListMaxHeight: 560
    signal toggleNotes()
    signal collapseSidebar()

    function formatMinutes(minutes) {
        const value = Math.max(0, Number(minutes || 0))
        if (value >= 60) {
            return Math.floor(value / 60) + "H " + Theme.pad2(value % 60) + "M"
        }
        return value + "M"
    }

    function focusHistory() {
        const days = statsStore.focusHistory
        return days && days.length > 0 ? days : statsStore.dailyStats
    }

    function graphDays() {
        const days = focusHistory()
        if (!days || days.length <= 0) {
            return []
        }
        const visibleCount = Math.min(graphVisibleDays, days.length)
        return days.slice(days.length - visibleCount)
    }

    function weekFocusMinutes() {
        const days = focusHistory()
        let total = 0
        if (!days) return 0
        for (let i = Math.max(0, days.length - 7); i < days.length; ++i) {
            total += Number(days[i].focusMinutes || 0)
        }
        return total
    }

    function sessionHistoryCount() {
        const sessions = notesStore.sessionHistory
        return sessions ? sessions.length : 0
    }

    function sessionListHeight() {
        const count = sessionHistoryCount()
        const rowExtent = 66
        const desired = count > 0 ? count * rowExtent + 8 : sessionListMinHeight
        return Math.min(sessionListMaxHeight, Math.max(sessionListMinHeight, desired))
    }

    Rectangle {
        anchors.fill: parent
        color: "#cc05050c"
        border.width: Theme.panelBorder
        border.color: Theme.goldDim
    }

    // Tick strip down the inside-left edge — a thin "measurement scale" that
    // sells the instrument-panel feel.
    Canvas {
        id: tickStrip
        anchors.left: parent.left
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 2
        width: 8
        opacity: 0.55
        z: 1

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = Theme.goldDim
            ctx.lineWidth = 1
            const spacing = 12
            for (let y = 0; y < height; y += spacing) {
                const long = (Math.floor(y / spacing) % 5 === 0)
                const w = long ? width - 1 : Math.floor(width / 2)
                ctx.beginPath()
                ctx.moveTo(0.5, y + 0.5)
                ctx.lineTo(w, y + 0.5)
                ctx.stroke()
            }
        }
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }

    // Header bar with collapse arrow
    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 70
        color: "#dd0a0a14"

        // Pulsing telemetry LED — sells "live link" to the bridge.
        Rectangle {
            id: telemetryLed
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.top: parent.top
            anchors.topMargin: 16
            width: 8
            height: 8
            radius: 4
            color: Theme.crimsonHot
            border.width: 1
            border.color: Theme.gold

            SequentialAnimation on opacity {
                running: true
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 1100; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 1100; easing.type: Easing.InOutQuad }
            }
        }

        Text {
            anchors.left: telemetryLed.right
            anchors.leftMargin: 10
            anchors.top: parent.top
            anchors.topMargin: 11
            text: "ASTRONAUT LOG"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 15
            font.letterSpacing: 0
        }

        Text {
            id: stardateText
            anchors.left: telemetryLed.right
            anchors.leftMargin: 10
            anchors.top: parent.top
            anchors.topMargin: 34
            color: Theme.textDim
            font.family: root.headerFont
            font.pixelSize: 9
            font.letterSpacing: 0

            function recompute() {
                const now = new Date()
                const start = new Date(now.getFullYear(), 0, 0)
                const dayOfYear = Math.floor((now - start) / 86400000)
                const stardate = (now.getFullYear() - 2000).toString().padStart(2, "0") +
                                 dayOfYear.toString().padStart(3, "0") + "." +
                                 Math.floor(((now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds()) / 86400) * 100)
                                     .toString().padStart(2, "0")
                text = "STARDATE " + stardate + "  ■  LINK NOMINAL"
            }

            Component.onCompleted: recompute()
            Timer { interval: 1000; running: true; repeat: true; onTriggered: stardateText.recompute() }
        }

        Rectangle {
            id: collapseButton
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 28
            color: collapseMouse.containsMouse ? Theme.steel : "transparent"
            border.width: 1
            border.color: collapseMouse.containsMouse ? Theme.gold : Theme.goldDim

            Text {
                anchors.centerIn: parent
                text: "→"
                color: collapseMouse.containsMouse ? Theme.gold : Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 14
            }

            MouseArea {
                id: collapseMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapseSidebar()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.crimson
        }
    }

    // Instrument-panel footer with corner accents and a slow scanning marker.
    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 24
        color: "#dd0a0a14"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.crimson
        }

        // Sweeping scan line — pure visual flavour.
        Rectangle {
            id: scanMarker
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 12
            color: Theme.gold
            opacity: 0.7

            SequentialAnimation on x {
                running: true
                loops: Animation.Infinite
                NumberAnimation {
                    from: 0
                    to: Math.max(0, footer.width - scanMarker.width)
                    duration: 6800
                    easing.type: Easing.InOutQuad
                }
                NumberAnimation {
                    from: Math.max(0, footer.width - scanMarker.width)
                    to: 0
                    duration: 6800
                    easing.type: Easing.InOutQuad
                }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "▲ TELEMETRY STREAM"
            color: Theme.goldDim
            font.family: root.headerFont
            font.pixelSize: 9
            font.letterSpacing: 0
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "FRAME LOCK ◢"
            color: Theme.goldDim
            font.family: root.headerFont
            font.pixelSize: 9
            font.letterSpacing: 0
        }
    }

    // ──────────────── Scrollable single-column body ────────────────
    Flickable {
        id: bodyFlick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.margins: 14
        clip: true
        contentWidth: width
        contentHeight: bodyColumn.implicitHeight

        Column {
            id: bodyColumn
            width: parent.width
            spacing: 12

            // ---- Daily target progress ----
            Rectangle {
                width: parent.width
                height: 102
                color: "#dd0d0d18"
                border.width: 1
                border.color: Theme.goldDim

                Text {
                    id: dailyTargetLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    text: "DAILY TARGET"
                    color: Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 11
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    text: "TARGET " + root.formatMinutes(statsStore.dailyTargetMinutes)
                    color: Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 11
                }

                Text {
                    id: todayValueLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: dailyTargetLabel.bottom
                    anchors.topMargin: 4
                    text: root.formatMinutes(statsStore.todayFocusMinutes) + " logged"
                    color: Theme.textPrimary
                    font.family: root.headerFont
                    font.pixelSize: 18
                }

                Rectangle {
                    id: targetRail
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.top: todayValueLabel.bottom
                    anchors.topMargin: 10
                    height: 12
                    color: Theme.voidColor
                    border.width: 1
                    border.color: Theme.textGhost

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 1
                        width: Math.max(0, Math.min(parent.width - 2, (parent.width - 2) * Math.min(1, statsStore.todayTargetProgress)))
                        color: statsStore.todayTargetProgress >= 1 ? Theme.gold : Theme.crimsonHot

                        Behavior on width {
                            NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
                        }
                    }

                    // Hash marks across the rail at 25% intervals — gauge feel.
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: 4
                            delegate: Item {
                                required property int index
                                width: targetRail.width / 4
                                height: targetRail.height
                                Rectangle {
                                    visible: index > 0
                                    x: 0
                                    width: 1
                                    height: parent.height
                                    color: Theme.voidColor
                                    opacity: 0.7
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    text: {
                        const progress = Math.round(Math.min(1, statsStore.todayTargetProgress) * 100)
                        if (statsStore.todayTargetProgress >= 1) return "■ DAILY TARGET REACHED"
                        if (statsStore.dailyTargetMinutes <= 0) return "(no target set — edit stats.json to set one)"
                        return progress + "%  ■  " + root.formatMinutes(Math.max(0, statsStore.dailyTargetMinutes - statsStore.todayFocusMinutes)) + " left to hit target"
                    }
                    color: statsStore.todayTargetProgress >= 1 ? Theme.gold : Theme.textDim
                    elide: Text.ElideRight
                    font.family: root.bodyFont
                    font.pixelSize: 11
                }
            }

            // ---- Metric trio ----
            Row {
                width: parent.width
                height: 64
                spacing: 8

                Repeater {
                    model: [
                        {"label": "TODAY", "value": root.formatMinutes(statsStore.todayFocusMinutes)},
                        {"label": "STREAK", "value": statsStore.currentStreakDays + "D"},
                        {"label": "TOTAL", "value": root.formatMinutes(statsStore.totalFocusMinutes)}
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        width: (bodyColumn.width - 16) / 3
                        height: 64
                        color: "#dd0d0d18"
                        border.width: 1
                        border.color: Theme.goldDim

                        // Corner reticle cuts — small accent ticks at each corner.
                        Repeater {
                            model: [
                                {"hx": "left", "vy": "top"},
                                {"hx": "right", "vy": "top"},
                                {"hx": "left", "vy": "bottom"},
                                {"hx": "right", "vy": "bottom"}
                            ]
                            delegate: Item {
                                required property var modelData
                                anchors.left: modelData.hx === "left" ? parent.left : undefined
                                anchors.right: modelData.hx === "right" ? parent.right : undefined
                                anchors.top: modelData.vy === "top" ? parent.top : undefined
                                anchors.bottom: modelData.vy === "bottom" ? parent.bottom : undefined
                                width: 6
                                height: 6
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: 0
                                }
                                Rectangle {
                                    width: 6; height: 1
                                    color: Theme.gold
                                    anchors.left: modelData.hx === "left" ? parent.left : undefined
                                    anchors.right: modelData.hx === "right" ? parent.right : undefined
                                    anchors.top: modelData.vy === "top" ? parent.top : undefined
                                    anchors.bottom: modelData.vy === "bottom" ? parent.bottom : undefined
                                }
                                Rectangle {
                                    width: 1; height: 6
                                    color: Theme.gold
                                    anchors.left: modelData.hx === "left" ? parent.left : undefined
                                    anchors.right: modelData.hx === "right" ? parent.right : undefined
                                    anchors.top: modelData.vy === "top" ? parent.top : undefined
                                    anchors.bottom: modelData.vy === "bottom" ? parent.bottom : undefined
                                }
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.top: parent.top
                            anchors.topMargin: 8
                            text: modelData.label
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 10
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 10
                            text: modelData.value
                            color: Theme.textPrimary
                            font.family: root.headerFont
                            font.pixelSize: 18
                        }
                    }
                }
            }

            // ---- Production graph (click to reveal) ----
            Rectangle {
                id: graphCard
                width: parent.width
                height: root.graphRevealed ? 232 : 50
                color: "#dd0d0d18"
                border.width: 1
                border.color: root.graphRevealed ? Theme.gold : Theme.goldDim

                Behavior on height {
                    NumberAnimation { duration: 220; easing.type: Easing.InOutQuad }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: root.graphRevealed ? 16 : 18
                    text: (root.graphRevealed ? "▴ PRODUCTION GRAPH" : "▾ PRODUCTION GRAPH")
                    color: root.graphRevealed ? Theme.gold : Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 12
                }

                Text {
                    visible: root.graphRevealed
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    text: "WEEK " + root.formatMinutes(root.weekFocusMinutes())
                    color: Theme.textGhost
                    font.family: root.bodyFont
                    font.pixelSize: 10
                }

                Canvas {
                    id: productivityGraph
                    visible: root.graphRevealed
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.top: parent.top
                    anchors.topMargin: 40
                    anchors.margins: 14

                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const days = root.graphDays()
                        const left = 36
                        const right = 8
                        const top = 8
                        const bottom = 22
                        const plotWidth = Math.max(1, width - left - right)
                        const plotHeight = Math.max(1, height - top - bottom)
                        let maxMinutes = 60
                        for (let i = 0; i < days.length; ++i) {
                            maxMinutes = Math.max(maxMinutes, Number(days[i].focusMinutes || 0))
                        }
                        maxMinutes = Math.ceil(maxMinutes / 30) * 30

                        ctx.strokeStyle = Theme.textGhost
                        ctx.globalAlpha = 0.6
                        for (let r = 0; r <= 4; ++r) {
                            const y = top + plotHeight * r / 4
                            ctx.beginPath()
                            ctx.moveTo(left, y)
                            ctx.lineTo(left + plotWidth, y)
                            ctx.stroke()
                        }
                        ctx.globalAlpha = 1

                        ctx.fillStyle = Theme.textDim
                        ctx.font = "10px '" + root.bodyFont + "'"
                        ctx.textAlign = "right"
                        ctx.textBaseline = "middle"
                        for (let r = 0; r <= 4; ++r) {
                            ctx.fillText(String(Math.round(maxMinutes * (4 - r) / 4)), left - 6, top + plotHeight * r / 4)
                        }

                        const slot = plotWidth / Math.max(1, days.length)
                        const barWidth = Math.max(4, Math.min(20, slot * 0.55))
                        for (let i = 0; i < days.length; ++i) {
                            const day = days[i]
                            const focus = Number(day.focusMinutes || 0)
                            const completed = Number(day.completedMinutes || 0)
                            const unlocked = Number(day.unlockedMinutes || 0)
                            const x = left + slot * i + (slot - barWidth) / 2
                            const baseY = top + plotHeight
                            const totalH = Math.max(1, plotHeight * focus / maxMinutes)
                            const compH = plotHeight * completed / maxMinutes
                            const unlH = plotHeight * unlocked / maxMinutes

                            ctx.fillStyle = focus > 0 ? Theme.steel : Theme.voidColor
                            ctx.fillRect(x, baseY - totalH, barWidth, totalH)
                            ctx.fillStyle = Theme.gold
                            ctx.fillRect(x, baseY - compH, barWidth, compH)
                            ctx.fillStyle = Theme.crimsonHot
                            ctx.fillRect(x, baseY - compH - unlH, barWidth, unlH)

                            ctx.fillStyle = Theme.textDim
                            ctx.textAlign = "center"
                            ctx.textBaseline = "top"
                            if (days.length <= 16 || i % Math.ceil(days.length / 10) === 0 || i === days.length - 1) {
                                ctx.fillText(String(day.label || ""), x + barWidth / 2, baseY + 6)
                            }
                        }
                    }

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Component.onCompleted: requestPaint()
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.graphRevealed = !root.graphRevealed
                        productivityGraph.requestPaint()
                    }
                }

                Connections {
                    target: statsStore
                    function onStatsChanged() { productivityGraph.requestPaint() }
                }
            }

            // ---- Today's notes (collapsible) ----
            Rectangle {
                id: todayCard
                width: parent.width
                color: "#dd0d0d18"
                border.width: 1
                border.color: Theme.goldDim
                height: root.todayNotesExpanded ? (todayHeader.height + todayContent.height + 20) : 44

                Behavior on height {
                    NumberAnimation { duration: 220; easing.type: Easing.InOutQuad }
                }

                Item {
                    id: todayHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 44

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.todayNotesExpanded ? "▴ " : "▾ ") + "TODAY'S NOTES  ■  " + notesStore.todayNoteCount
                        color: Theme.gold
                        font.family: root.headerFont
                        font.pixelSize: 12
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96
                        height: 24
                        color: writeMouse.containsMouse ? Theme.steel : "transparent"
                        border.width: 1
                        border.color: writeMouse.containsMouse ? Theme.gold : Theme.goldDim

                        Text {
                            anchors.centerIn: parent
                            text: "◈ WRITE"
                            color: writeMouse.containsMouse ? Theme.gold : Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: writeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleNotes()
                        }
                    }

                    MouseArea {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: 116
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.todayNotesExpanded = !root.todayNotesExpanded
                    }
                }

                Item {
                    id: todayContent
                    visible: root.todayNotesExpanded
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: todayHeader.bottom
                    height: Math.min(root.todayNotesMaxHeight, Math.max(root.todayNotesMinHeight, implicitHeight))
                    implicitHeight: todayText.implicitHeight + 16

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.bottomMargin: 8
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.textGhost

                        Flickable {
                            anchors.fill: parent
                            clip: true
                            contentHeight: todayText.implicitHeight + 20

                            Text {
                                id: todayText
                                width: parent.width
                                anchors.margins: 10
                                x: 10
                                y: 10
                                text: notesStore.todayNoteCount > 0
                                      ? notesStore.todayCombinedNotes
                                      : "Today is unwritten. Engage a routine and the mission log begins."
                                color: notesStore.todayNoteCount > 0 ? Theme.textPrimary : Theme.textGhost
                                wrapMode: Text.WordWrap
                                font.family: root.bodyFont
                                font.pixelSize: 11
                                lineHeight: 1.4
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        }
                    }
                }
            }

            // ---- Session history ----
            Rectangle {
                id: sessionsCard
                width: parent.width
                color: "#dd0d0d18"
                border.width: 1
                border.color: Theme.goldDim
                height: root.sessionsExpanded
                        ? sessionsHeader.height + root.sessionListHeight() + (detailPanel.visible ? detailPanel.height + 6 : 0) + 24
                        : 44

                Behavior on height {
                    NumberAnimation { duration: 220; easing.type: Easing.InOutQuad }
                }

                Item {
                    id: sessionsHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 44

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.sessionsExpanded ? "▴ " : "▾ ") + "MISSION LOG  ■  " + notesStore.sessionHistory.length
                        color: Theme.gold
                        font.family: root.headerFont
                        font.pixelSize: 12
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.sessionsExpanded = !root.sessionsExpanded
                    }
                }

                Column {
                    visible: root.sessionsExpanded
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: sessionsHeader.bottom
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 6

                    ListView {
                        id: sessionList
                        width: parent.width
                        height: root.sessionListHeight()
                        clip: true
                        spacing: 6
                        model: notesStore.sessionHistory

                        delegate: Rectangle {
                            required property var modelData
                            width: sessionList.width
                            height: 60
                            color: root.selectedSessionId === modelData.sessionId
                                   ? "#ee1a1024"
                                   : (sessionMouse.containsMouse ? "#dd141420" : "#dd0a0a14")
                            border.width: 1
                            border.color: root.selectedSessionId === modelData.sessionId
                                          ? Theme.gold
                                          : (sessionMouse.containsMouse ? Theme.goldDim : Theme.textGhost)

                            Behavior on color { ColorAnimation { duration: 120 } }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.right: rowMinutes.left
                                anchors.rightMargin: 8
                                anchors.top: parent.top
                                anchors.topMargin: 8
                                elide: Text.ElideRight
                                text: modelData.routineName
                                color: Theme.textPrimary
                                font.family: root.headerFont
                                font.pixelSize: 12
                            }

                            Text {
                                id: rowMinutes
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.top: parent.top
                                anchors.topMargin: 8
                                text: modelData.minutes + "M"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 12
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 8
                                elide: Text.ElideRight
                                text: modelData.dateLabel + " " + modelData.timeLabel + "  ■  " + String(modelData.result).toUpperCase()
                                      + (modelData.hasNote ? "  ■  ✎" : "")
                                color: Theme.textDim
                                font.family: root.bodyFont
                                font.pixelSize: 10
                            }

                            MouseArea {
                                id: sessionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.selectedSessionId === modelData.sessionId) {
                                        root.selectedSessionId = ""
                                    } else {
                                        root.selectedSessionId = modelData.sessionId
                                    }
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        Text {
                            anchors.centerIn: parent
                            visible: sessionList.count === 0
                            text: "NO MISSIONS LOGGED"
                            color: Theme.textGhost
                            font.family: root.headerFont
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        id: detailPanel
                        width: parent.width
                        height: 190
                        visible: root.selectedSessionId.length > 0
                        color: "#ee06060d"
                        border.width: 1
                        border.color: Theme.gold

                        Text {
                            id: detailHeader
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: closeDetailBtn.left
                            anchors.rightMargin: 8
                            anchors.top: parent.top
                            anchors.topMargin: 8
                            elide: Text.ElideRight
                            text: {
                                const data = notesStore.sessionNote(root.selectedSessionId)
                                if (!data || !data.sessionId) return ""
                                return data.routineName + "  ■  " + data.minutes + "M"
                            }
                            color: Theme.gold
                            font.family: root.headerFont
                            font.pixelSize: 12
                        }

                        Rectangle {
                            id: closeDetailBtn
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.top: parent.top
                            anchors.topMargin: 6
                            width: 22
                            height: 20
                            color: closeMouse.containsMouse ? Theme.crimsonHot : Theme.crimson
                            border.width: 1
                            border.color: Theme.gold

                            Text {
                                anchors.centerIn: parent
                                text: "✕"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 10
                            }

                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectedSessionId = ""
                            }
                        }

                        Flickable {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: detailHeader.bottom
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            anchors.topMargin: 6
                            clip: true
                            contentHeight: detailText.implicitHeight

                            Text {
                                id: detailText
                                width: parent.width
                                text: notesStore.sessionNoteText(root.selectedSessionId) || "(no notes captured)"
                                color: Theme.textPrimary
                                wrapMode: Text.WordWrap
                                font.family: root.bodyFont
                                font.pixelSize: 11
                                lineHeight: 1.4
                            }
                        }
                    }
                }
            }

            // ---- Last session card ----
            Rectangle {
                width: parent.width
                height: 84
                color: "#dd0d0d18"
                border.width: 1
                border.color: Theme.goldDim

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    text: "LAST SESSION"
                    color: Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 11
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 30
                    text: statsStore.lastSessionSummary
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    font.family: root.headerFont
                    font.pixelSize: 12
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    text: "DONE " + statsStore.completedSessions + "  ■  UNLOCKED " + statsStore.unlockedSessions + "  ■  INTERRUPTED " + statsStore.interruptedSessions
                    color: Theme.textGhost
                    font.family: root.bodyFont
                    font.pixelSize: 10
                }
            }

            Item { width: parent.width; height: 8 }
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }
}
