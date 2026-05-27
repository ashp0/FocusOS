import QtQuick
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root
    width: 1440
    height: 900
    focus: true

    property bool sidebarCollapsed: false
    property bool helpOpen: false

    Keys.onEscapePressed: function(event) {
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
        if (notesDrawer.open) {
            notesDrawer.open = false
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
        // Cmd+N — toggle notes drawer
        if ((event.modifiers & Qt.MetaModifier) && event.key === Qt.Key_N) {
            notesDrawer.open = !notesDrawer.open
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
        if (event.key === Qt.Key_Space && routineManager.active && !notesDrawer.open && !unlockModal.modalOpen) {
            routineManager.togglePause()
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
            onToggleNotes: notesDrawer.open = !notesDrawer.open
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
        starOpacityScale: 0.35
    }

    NotesDrawer {
        id: notesDrawer
        width: Math.min(360, Math.max(300, root.width * 0.26))
        height: root.height
        headerFont: root.headerFont
        bodyFont: root.bodyFont
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        rootWidth: root.width
        z: 20
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
