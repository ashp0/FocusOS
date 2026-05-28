import QtQuick
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property string headerFont
    property string bodyFont
    property string bodyBoldFont
    property bool sidebarCollapsed: false
    property bool otherRoutinesExpanded: false
    signal unlockRequested()
    signal editRoutinesRequested()
    signal showSidebar()

    function formatSecondsClock(seconds) {
        const value = Math.max(0, Number(seconds || 0))
        const hours = Math.floor(value / 3600)
        const minutes = Math.floor((value % 3600) / 60)
        const secs = value % 60
        return Theme.pad2(hours) + ":" + Theme.pad2(minutes) + ":" + Theme.pad2(secs)
    }

    component SystemStepper: Row {
        id: stepper
        property string label: ""
        property int value: -1
        property bool controlAvailable: false
        signal decrement()
        signal increment()

        height: 28
        spacing: 2
        opacity: controlAvailable ? 1 : 0.42

        Rectangle {
            width: 24
            height: stepper.height
            color: minusMouse.containsMouse && stepper.controlAvailable ? Theme.steel : "transparent"
            border.width: 1
            border.color: minusMouse.containsMouse && stepper.controlAvailable ? Theme.goldDim : Theme.textGhost

            Text {
                anchors.centerIn: parent
                text: "-"
                color: stepper.controlAvailable ? Theme.goldDim : Theme.textGhost
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            MouseArea {
                id: minusMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: stepper.controlAvailable
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: stepper.decrement()
            }
        }

        Rectangle {
            width: 76
            height: stepper.height
            color: "transparent"
            border.width: 1
            border.color: Theme.textGhost

            Text {
                anchors.centerIn: parent
                text: stepper.label + " " + (stepper.controlAvailable ? (stepper.value + "%") : "--")
                color: stepper.controlAvailable ? Theme.textDim : Theme.textGhost
                elide: Text.ElideRight
                font.family: root.headerFont
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        Rectangle {
            width: 24
            height: stepper.height
            color: plusMouse.containsMouse && stepper.controlAvailable ? Theme.steel : "transparent"
            border.width: 1
            border.color: plusMouse.containsMouse && stepper.controlAvailable ? Theme.goldDim : Theme.textGhost

            Text {
                anchors.centerIn: parent
                text: "+"
                color: stepper.controlAvailable ? Theme.goldDim : Theme.textGhost
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            MouseArea {
                id: plusMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: stepper.controlAvailable
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: stepper.increment()
            }
        }
    }

    component SystemSlider: Item {
        id: slider
        property string label: ""
        property int value: 0
        property bool controlAvailable: false
        signal valueEdited(int value)

        width: 132
        height: 28
        visible: controlAvailable

        function valueFromX(position) {
            return Math.max(0, Math.min(100, Math.round(position / Math.max(1, rail.width) * 100)))
        }

        Text {
            id: sliderLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            text: slider.label
            color: Theme.textDim
            elide: Text.ElideRight
            font.family: root.headerFont
            font.pixelSize: 10
            font.letterSpacing: 0
        }

        Rectangle {
            id: rail
            anchors.left: sliderLabel.right
            anchors.right: valueLabel.left
            anchors.leftMargin: 6
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: 2
            color: Theme.textGhost
        }

        Rectangle {
            anchors.left: rail.left
            anchors.verticalCenter: rail.verticalCenter
            width: Math.max(0, Math.min(rail.width, rail.width * slider.value / 100))
            height: 2
            color: Theme.goldDim
        }

        Rectangle {
            width: 8
            height: 16
            x: rail.x + Math.max(0, Math.min(rail.width - width, rail.width * slider.value / 100 - width / 2))
            anchors.verticalCenter: rail.verticalCenter
            color: sliderMouse.containsMouse ? Theme.gold : Theme.goldDim
            border.width: 1
            border.color: Theme.voidColor
        }

        Text {
            id: valueLabel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            text: slider.value + "%"
            color: Theme.textDim
            horizontalAlignment: Text.AlignRight
            font.family: root.headerFont
            font.pixelSize: 10
            font.letterSpacing: 0
        }

        MouseArea {
            id: sliderMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: function(mouse) {
                slider.valueEdited(slider.valueFromX(mouse.x - rail.x))
            }
            onPositionChanged: function(mouse) {
                if (pressed) {
                    slider.valueEdited(slider.valueFromX(mouse.x - rail.x))
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#cc080812"
        border.width: Theme.panelBorder
        border.color: Theme.goldDim
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 86
        color: "#dd0a0a14"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: "ФОКУС//OS"
            color: Theme.crimsonHot
            font.family: root.headerFont
            font.pixelSize: 30
            font.letterSpacing: 0
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            text: "MISSION CONTROL"
            color: Theme.goldDim
            font.family: root.bodyFont
            font.pixelSize: 11
            font.letterSpacing: 0
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Text {
                id: clock
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 22
                font.letterSpacing: 0

                function updateClock() {
                    const date = new Date()
                    text = Theme.pad2(date.getHours()) + ":" + Theme.pad2(date.getMinutes()) + ":" + Theme.pad2(date.getSeconds())
                }

                Component.onCompleted: updateClock()

                Timer {
                    interval: 1000
                    running: true
                    repeat: true
                    onTriggered: clock.updateClock()
                }
            }

            // Show-sidebar arrow, only visible when sidebar is hidden
            Rectangle {
                visible: root.sidebarCollapsed
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 28
                color: showSidebarMouse.containsMouse ? Theme.steel : "transparent"
                border.width: 1
                border.color: showSidebarMouse.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: showSidebarMouse.containsMouse ? Theme.gold : Theme.goldDim
                    font.family: root.headerFont
                    font.pixelSize: 14
                }

                MouseArea {
                    id: showSidebarMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.showSidebar()
                }
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

    // Edit-mode banner — only when access is granted (and idle)
    Rectangle {
        id: editBanner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: (routineManager.editMode && !routineManager.active) ? 36 : 0
        visible: height > 0
        color: "#cc15151f"
        border.width: 1
        border.color: Theme.gold

        Behavior on height { NumberAnimation { duration: 180 } }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "✎ EDIT MODE  ■  click ✎ on a routine to edit its mission"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 11
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: 78
            height: 22
            color: exitEditMouse.containsMouse ? Theme.steel : "transparent"
            border.width: 1
            border.color: Theme.goldDim

            Text {
                anchors.centerIn: parent
                text: "DONE"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 10
            }
            MouseArea {
                id: exitEditMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: routineManager.editMode = false
            }
        }
    }

    // ────────── MISSION VIEW (engaged state) ──────────
    MissionView {
        id: missionView
        visible: routineManager.active
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: editBanner.bottom
        anchors.bottom: othersTray.top
        headerFont: root.headerFont
        bodyFont: root.bodyFont
        onEndRequested: routineManager.endActiveRoutine()
    }

    // Collapsed "Other routines" tray, visible only when engaged
    Item {
        id: othersTray
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: accessPanel.top
        height: routineManager.active ? (root.otherRoutinesExpanded ? 168 : 36) : 0
        visible: height > 0

        Behavior on height {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }

        Rectangle {
            anchors.fill: parent
            color: "#cc080810"
            opacity: 0.85

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.textGhost
            }
        }

        Rectangle {
            id: othersToggle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 36
            color: "transparent"

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                text: (root.otherRoutinesExpanded ? "▾ " : "▸ ") + "OTHER ROUTINES  ■  " + Math.max(0, routineManager.routineCount - 1) + " SEALED"
                color: othersToggleMouse.containsMouse ? Theme.goldDim : Theme.textDim
                font.family: root.headerFont
                font.pixelSize: 11
                font.letterSpacing: 2
            }

            MouseArea {
                id: othersToggleMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.otherRoutinesExpanded = !root.otherRoutinesExpanded
            }
        }

        // Dimmed, scrollable list of other routines
        ListView {
            id: othersList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: othersToggle.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 4
            anchors.bottomMargin: 10
            orientation: ListView.Horizontal
            clip: true
            spacing: 10
            visible: root.otherRoutinesExpanded
            opacity: 0.5
            model: routineManager

            delegate: Item {
                id: otherDelegate
                required property string routineId
                required property string name
                required property string timeLabel
                required property bool isActive

                width: 200
                height: othersList.height
                visible: !isActive

                Rectangle {
                    anchors.fill: parent
                    color: "#dd0d0d18"
                    border.width: 1
                    border.color: Theme.textGhost

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        elide: Text.ElideRight
                        text: otherDelegate.name
                        color: Theme.textDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        text: "⊠ SEALED"
                        color: Theme.textGhost
                        font.family: root.headerFont
                        font.pixelSize: 11
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        text: otherDelegate.timeLabel
                        color: Theme.textGhost
                        font.family: root.headerFont
                        font.pixelSize: 11
                    }
                }
            }
        }
    }

    // ────────── IDLE STATE — full routine list ──────────
    ListView {
        id: routineList
        visible: !routineManager.active
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: editBanner.bottom
        anchors.bottom: accessPanel.top
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: 18
        anchors.bottomMargin: 12
        clip: true
        spacing: 18
        model: routineManager

        delegate: Item {
            id: delegateRoot
            required property string routineId
            required property string name
            required property string description
            required property string timeLabel
            required property bool isActive
            required property string buttonLabel
            required property bool buttonEnabled

            property bool editing: false
            property string editText: description

            width: routineList.width
            height: card.implicitHeight

            Rectangle {
                id: glow
                anchors.fill: card
                anchors.margins: -3
                visible: delegateRoot.isActive
                color: "transparent"
                border.width: 1
                border.color: Theme.crimsonHot
                opacity: 0.4

                SequentialAnimation on opacity {
                    running: delegateRoot.isActive
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 2800; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 0.18; duration: 2800; easing.type: Easing.InOutQuad }
                }
            }

            Rectangle {
                id: card
                anchors.fill: parent
                implicitHeight: layoutColumn.implicitHeight + 32
                color: delegateRoot.isActive ? "#dd1a1024" : (hoverArea.containsMouse ? "#dd141420" : "#dd0d0d18")
                border.width: 1
                border.color: delegateRoot.isActive ? Theme.crimsonHot : (hoverArea.containsMouse ? Theme.gold : Theme.goldDim)

                Behavior on color {
                    ColorAnimation { duration: Theme.transitionMs }
                }

                Rectangle {
                    id: leftAccent
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 4
                    color: delegateRoot.isActive ? Theme.crimsonHot : Theme.crimson
                    opacity: delegateRoot.isActive ? 1 : 0.45
                }

                Column {
                    id: layoutColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    anchors.topMargin: 16
                    spacing: 12

                    Item {
                        width: parent.width
                        height: 36

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: badge.left
                            anchors.rightMargin: 14
                            elide: Text.ElideRight
                            text: delegateRoot.name
                            color: Theme.textPrimary
                            font.family: root.headerFont
                            font.pixelSize: 22
                            font.letterSpacing: 0
                        }

                        Rectangle {
                            id: badge
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 116
                            height: 28
                            color: Theme.voidColor
                            border.width: 1
                            border.color: delegateRoot.isActive ? Theme.gold : Theme.goldDim

                            Text {
                                anchors.centerIn: parent
                                text: delegateRoot.timeLabel
                                color: delegateRoot.isActive ? Theme.gold : Theme.goldDim
                                font.family: root.headerFont
                                font.pixelSize: 14
                                font.letterSpacing: 0
                            }
                        }
                    }

                    // Description — editable only when edit mode is on
                    Item {
                        width: parent.width
                        height: delegateRoot.editing ? Math.max(96, descEdit.contentHeight + 24) : Math.max(40, descDisplay.implicitHeight + 12)

                        Rectangle {
                            anchors.fill: parent
                            color: delegateRoot.editing ? "#ee0a0a12" : "transparent"
                            border.width: delegateRoot.editing ? 1 : 0
                            border.color: Theme.gold
                        }

                        Text {
                            id: descDisplay
                            visible: !delegateRoot.editing
                            anchors.left: parent.left
                            anchors.right: pencil.visible ? pencil.left : parent.right
                            anchors.rightMargin: pencil.visible ? 12 : 0
                            anchors.top: parent.top
                            anchors.topMargin: 4
                            wrapMode: Text.WordWrap
                            text: delegateRoot.description && delegateRoot.description.length > 0
                                  ? delegateRoot.description
                                  : (routineManager.editMode ? "( click ✎ to write the mission goal )" : "")
                            color: delegateRoot.description && delegateRoot.description.length > 0
                                   ? Theme.textPrimary
                                   : Theme.textDim
                            font.family: root.bodyFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                            lineHeight: 1.35
                        }

                        Rectangle {
                            id: pencil
                            visible: !delegateRoot.editing && routineManager.editMode
                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 28
                            height: 28
                            color: pencilMouse.containsMouse ? Theme.steel : "transparent"
                            border.width: 1
                            border.color: pencilMouse.containsMouse ? Theme.gold : Theme.goldDim

                            Text {
                                anchors.centerIn: parent
                                text: "✎"
                                color: pencilMouse.containsMouse ? Theme.gold : Theme.goldDim
                                font.family: root.headerFont
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: pencilMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    delegateRoot.editText = delegateRoot.description
                                    delegateRoot.editing = true
                                    descEdit.forceActiveFocus()
                                }
                            }
                        }

                        TextEdit {
                            id: descEdit
                            visible: delegateRoot.editing
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            text: delegateRoot.editText
                            color: Theme.textPrimary
                            selectedTextColor: Theme.voidColor
                            selectionColor: Theme.gold
                            wrapMode: TextEdit.WordWrap
                            font.family: root.bodyFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                            onTextChanged: delegateRoot.editText = text
                        }

                        Row {
                            visible: delegateRoot.editing
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            spacing: 6

                            Rectangle {
                                width: 78
                                height: 24
                                color: cancelMouse.containsMouse ? Theme.steel : Theme.iron
                                border.width: 1
                                border.color: Theme.goldDim
                                Text {
                                    anchors.centerIn: parent
                                    text: "CANCEL"
                                    color: Theme.goldDim
                                    font.family: root.headerFont
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    id: cancelMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: delegateRoot.editing = false
                                }
                            }
                            Rectangle {
                                width: 78
                                height: 24
                                color: saveMouse.containsMouse ? Theme.crimsonHot : Theme.crimson
                                border.width: 1
                                border.color: Theme.gold
                                Text {
                                    anchors.centerIn: parent
                                    text: "SAVE"
                                    color: Theme.gold
                                    font.family: root.headerFont
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    id: saveMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        routineManager.updateRoutineDescription(delegateRoot.routineId, delegateRoot.editText)
                                        delegateRoot.editing = false
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 42

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "══ ■ ROUTINE LOCKPOINT ■ ══"
                            color: delegateRoot.isActive ? Theme.goldDim : Theme.textGhost
                            font.family: root.headerFont
                            font.pixelSize: 11
                            font.letterSpacing: 0
                        }

                        Rectangle {
                            id: engageButton
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 152
                            height: 38
                            transformOrigin: Item.Center
                            scale: delegateRoot.buttonEnabled
                                   ? (buttonMouse.containsMouse ? 1.03 : idlePulseScale)
                                   : 1
                            property real idlePulseScale: 1
                            color: delegateRoot.buttonEnabled
                                   ? (buttonMouse.containsMouse ? Theme.crimsonHot : Theme.crimson)
                                   : Theme.steel
                            border.width: 1
                            border.color: delegateRoot.buttonEnabled && buttonMouse.containsMouse ? Theme.gold : Theme.goldDim

                            SequentialAnimation on idlePulseScale {
                                running: delegateRoot.buttonEnabled && !buttonMouse.containsMouse
                                loops: Animation.Infinite
                                NumberAnimation { to: 1.018; duration: 2400; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 2400; easing.type: Easing.InOutQuad }
                            }

                            Behavior on scale {
                                NumberAnimation { duration: 160; easing.type: Easing.InOutQuad }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: delegateRoot.buttonLabel
                                color: delegateRoot.buttonEnabled ? Theme.gold : Theme.textGhost
                                font.family: root.headerFont
                                font.pixelSize: 14
                                font.letterSpacing: 0
                            }

                            MouseArea {
                                id: buttonMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: delegateRoot.buttonEnabled
                                cursorShape: Qt.PointingHandCursor
                                onClicked: routineManager.engage(delegateRoot.routineId)
                            }
                        }
                    }
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    // Bottom controls — dimmed so routines own the foreground.
    Item {
        id: accessPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 72

        Rectangle {
            anchors.fill: parent
            color: "#cc080810"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.textGhost
            }
        }

        // Group has lower opacity so it recedes into the background.
        Item {
            anchors.fill: parent
            opacity: bottomHover.containsMouse ? 0.85 : 0.32

            Behavior on opacity {
                NumberAnimation { duration: 220; easing.type: Easing.InOutQuad }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Rectangle {
                    id: musicButton
                    width: 78
                    height: 28
                    color: musicMouse.containsMouse ? Theme.steel : "transparent"
                    border.width: 1
                    border.color: musicMouse.containsMouse ? Theme.goldDim : Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: !musicEngine.available ? "♪ —" : (musicEngine.enabled ? "♪ ON" : "♪ OFF")
                        color: !musicEngine.available ? Theme.textGhost : (musicMouse.containsMouse ? Theme.goldDim : Theme.textDim)
                        font.family: root.headerFont
                        font.pixelSize: 11
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: musicMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: musicEngine.available
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: musicEngine.enabled = !musicEngine.enabled
                    }
                }

                Item {
                    id: volumeSlider
                    width: 96
                    height: 28
                    enabled: musicEngine.available
                    opacity: enabled ? 1 : 0.4

                    function valueFromX(position) {
                        return Math.max(0, Math.min(100, Math.round(position / rail.width * 100)))
                    }

                    Rectangle {
                        id: rail
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 2
                        color: Theme.textGhost
                    }

                    Rectangle {
                        id: thumb
                        width: 8
                        height: 14
                        x: Math.max(0, Math.min(rail.width - width, (musicEngine.volume / 100) * rail.width - width / 2))
                        anchors.verticalCenter: rail.verticalCenter
                        color: Theme.goldDim
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: volumeSlider.enabled
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            musicEngine.volume = volumeSlider.valueFromX(mouse.x)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                musicEngine.volume = volumeSlider.valueFromX(mouse.x)
                            }
                        }
                    }
                }

                SystemSlider {
                    label: "VOL"
                    value: systemStatus.volumePercent
                    controlAvailable: systemStatus.volumeAvailable
                    onValueEdited: function(nextValue) {
                        systemStatus.setSystemVolume(nextValue)
                    }
                }

                SystemSlider {
                    label: "BRI"
                    value: systemStatus.brightnessPercent
                    controlAvailable: systemStatus.brightnessAvailable
                    onValueEdited: function(nextValue) {
                        systemStatus.setBrightness(nextValue)
                    }
                }
            }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 22
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                Rectangle {
                    visible: systemStatus.batteryPercent >= 0
                    width: 104
                    height: 28
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: systemStatus.batteryLabel
                        color: systemStatus.batteryCharging ? Theme.gold : Theme.textDim
                        elide: Text.ElideRight
                        font.family: root.headerFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }
                }

                Rectangle {
                    visible: routineManager.accessGranted
                    width: 128
                    height: 28
                    color: editRoutinesMouse.containsMouse ? Theme.steel : "transparent"
                    border.width: 1
                    border.color: editRoutinesMouse.containsMouse ? Theme.goldDim : Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: "EDIT ROUTINES"
                        color: editRoutinesMouse.containsMouse ? Theme.goldDim : Theme.textDim
                        elide: Text.ElideRight
                        font.family: root.headerFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: editRoutinesMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.editRoutinesRequested()
                    }
                }

                // Re-open admin menu during Other Access if it was dismissed.
                Rectangle {
                    visible: routineManager.accessGranted
                    width: 126
                    height: 28
                    color: adminMouse.containsMouse ? Theme.steel : "transparent"
                    border.width: 1
                    border.color: adminMouse.containsMouse ? Theme.goldDim : Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: "▣ OTHER MENU"
                        color: adminMouse.containsMouse ? Theme.goldDim : Theme.textDim
                        elide: Text.ElideRight
                        font.family: root.headerFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: adminMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.unlockRequested()
                    }
                }

                // Access Desktop button — Linux only, when access is open
                Rectangle {
                    visible: routineManager.accessGranted && routineManager.desktopShellSupported
                    width: 138
                    height: 28
                    color: desktopMouse.containsMouse ? Theme.steel : "transparent"
                    border.width: 1
                    border.color: desktopMouse.containsMouse ? Theme.goldDim : Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: "ACCESS DESKTOP"
                        color: desktopMouse.containsMouse ? Theme.goldDim : Theme.textDim
                        elide: Text.ElideRight
                        font.family: root.headerFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: desktopMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: routineManager.launchDesktopShell()
                    }
                }

                // Close access early
                Rectangle {
                    visible: routineManager.accessGranted
                    width: 124
                    height: 28
                    color: closeAccessMouse.containsMouse ? Theme.crimsonHot : "transparent"
                    border.width: 1
                    border.color: closeAccessMouse.containsMouse ? Theme.gold : Theme.crimson

                    Text {
                        anchors.centerIn: parent
                        text: "⏏ END ACCESS"
                        color: closeAccessMouse.containsMouse ? Theme.gold : Theme.crimson
                        font.family: root.headerFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: closeAccessMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: routineManager.closeOtherAccess()
                    }
                }

                // Open access (when none is granted)
                Rectangle {
                    visible: !routineManager.accessGranted
                    width: 150
                    height: 28
                    color: otherMouse.containsMouse ? Theme.steel : "transparent"
                    border.width: 1
                    border.color: otherMouse.containsMouse ? Theme.goldDim : Theme.textGhost

                    Text {
                        anchors.centerIn: parent
                        text: "▣ OTHER ACCESS"
                        color: otherMouse.containsMouse ? Theme.goldDim : Theme.textDim
                        elide: Text.ElideRight
                        font.family: root.headerFont
                        font.pixelSize: 11
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: otherMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.unlockRequested()
                    }
                }
            }
        }

        MouseArea {
            id: bottomHover
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            propagateComposedEvents: true
        }
    }
}
