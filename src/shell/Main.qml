import QtQuick
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root
    width: 1440
    height: 900
    focus: true

    property bool sidebarCollapsed: true
    property bool helpOpen: false

    Keys.onEscapePressed: function(event) {
        if (routineManager.networkLockPromptVisible) {
            routineManager.abortPendingRoutineStart()
            event.accepted = true
            root.forceActiveFocus()
            return
        }
        if (unlockModal.modalOpen) {
            unlockModal.closeModal()
            event.accepted = true
            root.forceActiveFocus()
            return
        }
        if (helpOpen) {
            helpOpen = false
            event.accepted = true
            return
        }
        if (!root.sidebarCollapsed && info.activeTab === 1) {
            root.sidebarCollapsed = true
            event.accepted = true
            root.forceActiveFocus()
            return
        }
    }

    Keys.onPressed: function(event) {
        // Cmd+\ — toggle sidebar
        if ((event.modifiers & Qt.MetaModifier) && event.key === Qt.Key_Backslash) {
            root.sidebarCollapsed = !root.sidebarCollapsed
            event.accepted = true
            return
        }
        // Cmd+N — open notes tab
        if ((event.modifiers & Qt.MetaModifier) && event.key === Qt.Key_N) {
            root.sidebarCollapsed = false
            info.activeTab = 1
            event.accepted = true
            return
        }
        // ? — help overlay
        if (event.key === Qt.Key_Question || (event.key === Qt.Key_Slash && (event.modifiers & Qt.ShiftModifier))) {
            root.helpOpen = !root.helpOpen
            event.accepted = true
            return
        }
        // Space — pause / resume active routine
        if (event.key === Qt.Key_Space && routineManager.active && !unlockModal.modalOpen) {
            routineManager.togglePause()
            event.accepted = true
            return
        }
        // Media volume keys
        if (event.key === Qt.Key_VolumeUp) {
            systemStatus.adjustSystemVolume(5)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_VolumeDown) {
            systemStatus.adjustSystemVolume(-5)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_VolumeMute) {
            systemStatus.toggleMute()
            event.accepted = true
            return
        }
        // Media brightness keys (handled globally by MediaKeys when another app
        // is focused; this covers the case where FocusOS itself is on top).
        if (event.key === Qt.Key_MonBrightnessUp) {
            systemStatus.adjustBrightness(5)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_MonBrightnessDown) {
            systemStatus.adjustBrightness(-5)
            event.accepted = true
            return
        }
    }

    FontLoader {
        id: shareTech
        source: "qrc:/qt/qml/FocusOS/assets/fonts/ShareTechMono-Regular.ttf"
    }

    FontLoader {
        id: plexMono
        source: "qrc:/qt/qml/FocusOS/assets/fonts/IBMPlexMono-Regular.ttf"
    }

    FontLoader {
        id: plexMonoSemi
        source: "qrc:/qt/qml/FocusOS/assets/fonts/IBMPlexMono-SemiBold.ttf"
    }

    property string headerFont: shareTech.status === FontLoader.Ready ? shareTech.name : "Share Tech Mono"
    property string bodyFont: plexMono.status === FontLoader.Ready ? plexMono.name : "IBM Plex Mono"
    property string bodyBoldFont: plexMonoSemi.status === FontLoader.Ready ? plexMonoSemi.name : root.bodyFont

    function formatMinutes(minutes) {
        const value = Math.max(0, Number(minutes || 0))
        if (value >= 60) {
            return Math.floor(value / 60) + "H " + Theme.pad2(value % 60) + "M"
        }
        return value + "M"
    }

    // Background ambient layer — space backdrop with starfield + inspiration media.
    AmbientLayer {
        anchors.fill: parent
        z: 0
    }

    Rectangle {
        id: completionSignal
        anchors.fill: parent
        z: 4
        color: "transparent"
        border.width: routineManager.sessionPromptVisible ? 2 : 0
        border.color: Theme.gold
        opacity: routineManager.sessionPromptVisible ? 0.35 : 0

        Behavior on opacity {
            NumberAnimation { duration: 1800; easing.type: Easing.InOutQuad }
        }
    }

    Item {
        anchors.fill: parent
        z: 10

        property int sidebarWidth: root.sidebarCollapsed ? 0 : Math.max(320, Math.floor(root.width * 0.34))

        ActivitiesPanel {
            id: activities
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width - parent.sidebarWidth - (root.sidebarCollapsed ? 0 : Theme.divider)
            headerFont: root.headerFont
            bodyFont: root.bodyFont
            bodyBoldFont: root.bodyBoldFont
            sidebarCollapsed: root.sidebarCollapsed
            onUnlockRequested: unlockModal.openModal()
            // The routine editor used to be its own dialog. We collapsed it
            // into the ROUTINES tab of the Settings modal so there's only
            // one admin surface.
            onEditRoutinesRequested: {
                unlockModal.openModal()
                unlockModal.activeTab = 0
            }
            onShowSidebar: root.sidebarCollapsed = false

            Behavior on width {
                NumberAnimation { duration: 240; easing.type: Easing.InOutQuad }
            }
        }

        Rectangle {
            id: divider
            anchors.left: activities.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: root.sidebarCollapsed ? 0 : Theme.divider
            color: Theme.crimson
            visible: !root.sidebarCollapsed
        }

        InfoPanel {
            id: info
            anchors.left: divider.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            headerFont: root.headerFont
            bodyFont: root.bodyFont
            visible: !root.sidebarCollapsed
            onToggleNotes: {
                root.sidebarCollapsed = false
                info.activeTab = 1
            }
            onCollapseSidebar: root.sidebarCollapsed = true
        }
    }

    // Thin overlay of stars on top of panels — keeps the "space" feel even over chrome.
    AmbientLayer {
        anchors.fill: parent
        z: 12
        showBackground: false
        showMedia: false
        showStars: true
        showDust: false
        starOpacityScale: 0.35
    }

    UnlockModal {
        id: unlockModal
        anchors.fill: parent
        headerFont: root.headerFont
        bodyFont: root.bodyFont
        z: 30
    }

    Item {
        id: sessionPrompt
        anchors.fill: parent
        z: 35
        visible: opacity > 0
        opacity: routineManager.sessionPromptVisible ? 1 : 0

        function finish(quitApps) {
            statsStore.recordLastSessionReflection(reflectionField.text)
            notesStore.recordSessionReflection(reflectionField.text)
            if (quitApps) {
                routineManager.quitFinishedSession()
            } else {
                routineManager.continueFinishedSession()
            }
            reflectionField.text = ""
            root.forceActiveFocus()
        }

        Behavior on opacity {
            NumberAnimation { duration: 260; easing.type: Easing.InOutQuad }
        }

        Rectangle {
            anchors.fill: parent
            color: "#dd050508"
        }

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            id: sessionCard
            width: Math.min(640, parent.width - 64)
            height: 392
            anchors.centerIn: parent
            color: Theme.iron
            border.width: 1
            border.color: Theme.gold

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 48
                color: Theme.steel

                Text {
                    anchors.centerIn: parent
                    text: "MISSION COMPLETE"
                    color: Theme.gold
                    font.family: root.headerFont
                    font.pixelSize: 16
                    font.letterSpacing: 0
                }
            }

            Text {
                id: sessionName
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.top: parent.top
                anchors.topMargin: 76
                text: routineManager.finishedSessionName
                color: Theme.textPrimary
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: root.headerFont
                font.pixelSize: 24
                font.letterSpacing: 0
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: sessionName.bottom
                anchors.topMargin: 10
                text: root.formatMinutes(routineManager.finishedSessionMinutes) + " RECORDED"
                color: Theme.goldDim
                horizontalAlignment: Text.AlignHCenter
                font.family: root.bodyFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            Rectangle {
                id: reflectionBox
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: sessionName.bottom
                anchors.topMargin: 52
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                height: 120
                color: Theme.voidColor
                border.width: 1
                border.color: reflectionField.activeFocus ? Theme.gold : Theme.goldDim

                Text {
                    visible: reflectionField.text.length === 0
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 13
                    text: "WHAT DID YOU ACCOMPLISH? WHAT IS NEXT?"
                    color: Theme.textGhost
                    font.family: root.bodyFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                TextEdit {
                    id: reflectionField
                    anchors.fill: parent
                    anchors.margins: 14
                    color: Theme.textPrimary
                    selectedTextColor: Theme.voidColor
                    selectionColor: Theme.gold
                    wrapMode: TextEdit.WordWrap
                    font.family: root.bodyFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 26
                spacing: 14

                Repeater {
                    model: [
                        {"label": "CONTINUE", "quit": false},
                        {"label": "QUIT", "quit": true}
                    ]

                    delegate: Rectangle {
                        required property var modelData

                        width: 150
                        height: 38
                        color: promptMouse.containsMouse
                               ? (modelData.quit ? Theme.crimsonHot : Theme.steel)
                               : (modelData.quit ? Theme.crimson : Theme.iron)
                        border.width: 1
                        border.color: promptMouse.containsMouse ? Theme.gold : Theme.goldDim

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: Theme.gold
                            font.family: root.headerFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                        }

                        MouseArea {
                            id: promptMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sessionPrompt.finish(modelData.quit)
                        }
                    }
                }
            }
        }

        Connections {
            target: routineManager
            function onSessionPromptChanged() {
                if (routineManager.sessionPromptVisible) {
                    reflectionField.text = ""
                    reflectionField.forceActiveFocus()
                }
            }
        }
    }

    Item {
        id: completionBurst
        anchors.fill: parent
        z: 34
        visible: running || opacity > 0
        opacity: running ? 1 : 0
        property bool running: false
        property real age: 0
        property var particles: []

        function ignite() {
            const next = []
            const cx = width * 0.5
            const cy = height * 0.5
            const colors = ["#E8A020", "#C0392B", "#F5F0E8"]
            for (let i = 0; i < 52; ++i) {
                const angle = Math.random() * Math.PI * 2
                const speed = 220 + Math.random() * 560
                next.push({
                    x: cx,
                    y: cy,
                    vx: Math.cos(angle) * speed,
                    vy: Math.sin(angle) * speed - 40,
                    width: 2 + Math.random() * 3,
                    length: 12 + Math.random() * 26,
                    color: colors[Math.floor(Math.random() * colors.length)],
                    rotation: angle + Math.PI / 2 + (Math.random() - 0.5) * 0.8,
                    spin: (Math.random() - 0.5) * 5.5,
                    life: 0.8 + Math.random() * 0.8
                })
            }
            particles = next
            age = 0
            running = true
            burstTimer.restart()
            burstCanvas.requestPaint()
        }

        Behavior on opacity {
            NumberAnimation { duration: 600; easing.type: Easing.OutQuad }
        }

        Canvas {
            id: burstCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (!completionBurst.running && completionBurst.age <= 0) {
                    return
                }

                const t = completionBurst.age
                const cx = width * 0.5
                const cy = height * 0.5
                const flash = Math.max(0, 1 - t * 1.5)
                const halo = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.max(width, height) * 0.45)
                halo.addColorStop(0, "rgba(232,160,32," + (0.34 * flash) + ")")
                halo.addColorStop(0.35, "rgba(192,57,43," + (0.18 * flash) + ")")
                halo.addColorStop(1, "rgba(5,5,8,0)")
                ctx.fillStyle = halo
                ctx.fillRect(0, 0, width, height)

                ctx.save()
                ctx.translate(cx, cy - 58 - t * 90)
                ctx.rotate(-0.08)
                ctx.fillStyle = "rgba(232,220,200," + Math.max(0, 0.9 - t) + ")"
                ctx.beginPath()
                ctx.moveTo(0, -34)
                ctx.lineTo(13, 20)
                ctx.lineTo(-13, 20)
                ctx.closePath()
                ctx.fill()
                ctx.fillStyle = "rgba(192,57,43," + Math.max(0, 0.85 - t) + ")"
                ctx.fillRect(-15, 10, 30, 12)
                ctx.restore()

                for (let i = 0; i < completionBurst.particles.length; ++i) {
                    const p = completionBurst.particles[i]
                    const lifeT = Math.min(1, t / p.life)
                    const alpha = Math.max(0, 1 - lifeT)
                    const x = p.x + p.vx * t
                    const y = p.y + p.vy * t + 260 * t * t
                    ctx.save()
                    ctx.translate(x, y)
                    ctx.rotate(p.rotation + p.spin * t)
                    ctx.globalAlpha = alpha
                    ctx.fillStyle = p.color
                    ctx.fillRect(-p.width / 2, -p.length / 2, p.width, p.length * (1 + lifeT * 0.35))
                    ctx.globalAlpha = alpha * 0.28
                    ctx.fillRect(-p.width, -p.length / 2, p.width * 2, p.length * 1.6)
                    ctx.restore()
                }
                ctx.globalAlpha = 1
            }
        }

        Timer {
            id: burstTimer
            interval: 16
            repeat: true
            onTriggered: {
                completionBurst.age += 0.016
                if (completionBurst.age > 1.7) {
                    completionBurst.running = false
                    stop()
                }
                burstCanvas.requestPaint()
            }
        }

        Connections {
            target: routineManager
            function onSessionPromptChanged() {
                if (routineManager.sessionPromptVisible && routineManager.finishedSessionResult === "completed") {
                    completionBurst.ignite()
                }
            }
        }
    }

    Item {
        id: statusToast
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 94
        z: 38
        height: 42
        visible: opacity > 0
        opacity: 0
        property string message: ""

        function show(messageText) {
            message = String(messageText || "")
            if (message.length === 0) {
                return
            }
            opacity = 1
            toastTimer.restart()
        }

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 48, toastText.implicitWidth + 36)
            height: parent.height
            color: "#ee0f0f14"
            border.width: 1
            border.color: Theme.gold

            Text {
                id: toastText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: statusToast.message
                color: Theme.gold
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: root.bodyFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }
        }

        Timer {
            id: toastTimer
            interval: 3600
            onTriggered: statusToast.opacity = 0
        }

        Connections {
            target: routineManager
            function onStatusMessageChanged(message) {
                statusToast.show(message)
            }
        }
    }

    Item {
        id: networkLockPrompt
        anchors.fill: parent
        z: 37
        visible: opacity > 0
        opacity: routineManager.networkLockPromptVisible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
        }

        Rectangle {
            anchors.fill: parent
            color: "#e8050508"
        }

        // hoverEnabled so this overlay grabs hover while it's up — otherwise the
        // ENGAGE button underneath keeps the hover state it had at click time and
        // stays highlighted/scaled after the routine is aborted.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
        }

        Rectangle {
            width: Math.min(620, parent.width - 64)
            height: 286
            anchors.centerIn: parent
            color: Theme.iron
            border.width: 1
            border.color: Theme.crimsonHot

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 48
                color: Theme.crimson

                Text {
                    anchors.centerIn: parent
                    text: "NETWORK LOCK FAILED"
                    color: Theme.gold
                    font.family: root.headerFont
                    font.pixelSize: 16
                    font.letterSpacing: 0
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 76
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                text: routineManager.networkLockRoutineName
                color: Theme.textPrimary
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.family: root.headerFont
                font.pixelSize: 20
                font.letterSpacing: 0
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 118
                anchors.leftMargin: 34
                anchors.rightMargin: 34
                text: "Network restrictions could not be applied. Strict mode will not start a routine until nftables is configured correctly."
                color: Theme.goldDim
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.family: root.bodyFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 174
                anchors.leftMargin: 34
                anchors.rightMargin: 34
                text: routineManager.networkLockError
                color: Theme.textDim
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.family: root.bodyFont
                font.pixelSize: 11
                font.letterSpacing: 0
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 26
                spacing: 14

                Rectangle {
                    width: 156
                    height: 38
                    color: promptButtonMouse.containsMouse ? Theme.steel : Theme.iron
                    border.width: 1
                    border.color: promptButtonMouse.containsMouse ? Theme.gold : Theme.goldDim

                    Text {
                        anchors.centerIn: parent
                        text: "ABORT"
                        color: Theme.gold
                        font.family: root.headerFont
                        font.pixelSize: 12
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: promptButtonMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            routineManager.abortPendingRoutineStart()
                            root.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    // Help overlay (press ?)
    Item {
        id: helpOverlay
        anchors.fill: parent
        z: 36
        visible: opacity > 0
        opacity: root.helpOpen ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }

        Rectangle {
            anchors.fill: parent
            color: "#ee050508"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.helpOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 64)
            height: 360
            color: "#ee0a0a14"
            border.width: 1
            border.color: Theme.gold

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 24
                horizontalAlignment: Text.AlignHCenter
                text: "ASTRONAUT'S MANUAL"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 18
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 70
                anchors.leftMargin: 36
                anchors.rightMargin: 36
                spacing: 14

                Repeater {
                    model: [
                        {"key": "⌘ + \\", "desc": "TOGGLE SIDEBAR"},
                        {"key": "⌘ + N", "desc": "OPEN MISSION LOG NOTES"},
                        {"key": "SPACE", "desc": "PAUSE / RESUME ACTIVE ROUTINE"},
                        {"key": "?", "desc": "TOGGLE THIS MANUAL"},
                        {"key": "ESC", "desc": "CLOSE OVERLAY / DRAWER"}
                    ]

                    delegate: Item {
                        required property var modelData
                        width: parent.width
                        height: 22

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.key
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 13
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.desc
                            color: Theme.textPrimary
                            font.family: root.bodyFont
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 18
                horizontalAlignment: Text.AlignHCenter
                text: "BUILD YOUR YEAR. ENGAGE A ROUTINE.  ■  press ESC to close"
                color: Theme.textGhost
                font.family: root.bodyFont
                font.pixelSize: 11
            }
        }
    }

    // The routine countdown border now lives in a global, always-on-top
    // overlay window (ProgressOverlay.qml, wired in ShellWindow) so it stays
    // visible across every space and on top of full-screen apps.

    Canvas {
        id: scanlines
        anchors.fill: parent
        z: 40
        opacity: 0.35
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "rgba(160, 180, 220, 0.018)"
            for (let y = 0; y < height; y += 4) {
                ctx.fillRect(0, y, width, 2)
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
