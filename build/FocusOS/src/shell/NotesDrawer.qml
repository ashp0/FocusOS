import QtQuick
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property bool open: false
    property int rootWidth: 0
    property string headerFont
    property string bodyFont
    property int activeView: 0   // 0 = current draft, 1 = today's combined

    x: open ? rootWidth - width : rootWidth

    Behavior on x {
        NumberAnimation {
            duration: Theme.transitionMs
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0.2, 0.0, 0.8, 1.0, 1.0, 1.0]
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#ee0a0a14"
        border.width: 1
        border.color: Theme.goldDim
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 66
        color: "#ee101020"

        Text {
            anchors.left: parent.left
            anchors.right: closeButton.left
            anchors.leftMargin: 18
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.activeView === 0
                  ? (notesStore.draftRoutineName.length > 0
                     ? "LOG: " + notesStore.draftRoutineName
                     : "MISSION LOG")
                  : "ALL OF TODAY"
            color: Theme.gold
            elide: Text.ElideRight
            font.family: root.headerFont
            font.pixelSize: 15
            font.letterSpacing: 0
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 30
            color: closeMouse.containsMouse ? Theme.crimsonHot : Theme.crimson
            border.width: 1
            border.color: closeMouse.containsMouse ? Theme.gold : Theme.goldDim

            Text {
                anchors.centerIn: parent
                text: "✕"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 12
            }

            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.open = false
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: Theme.crimson
        }
    }

    // View toggle — divider deliberately outside the Row so the Row's positioner
    // doesn't fight with an anchored child.
    Item {
        id: viewToggle
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: 32

        Row {
            id: viewToggleRow
            anchors.fill: parent

            Repeater {
                model: [
                    {"label": "DRAFT", "idx": 0},
                    {"label": "TODAY (" + notesStore.todayNoteCount + ")", "idx": 1}
                ]
                delegate: Rectangle {
                    required property var modelData
                    width: viewToggleRow.width / 2
                    height: viewToggleRow.height
                    color: root.activeView === modelData.idx ? "#ee15151f" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.activeView === modelData.idx ? Theme.gold : Theme.textDim
                        font.family: root.headerFont
                        font.pixelSize: 11
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: root.activeView === modelData.idx ? 2 : 0
                        color: Theme.gold
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeView = modelData.idx
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.goldDim
        }
    }

    // Active routine draft editor
    TextArea {
        id: notesArea
        visible: root.activeView === 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: viewToggle.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 14
        text: notesStore.text
        color: Theme.textPrimary
        selectedTextColor: Theme.voidColor
        selectionColor: Theme.gold
        placeholderText: notesStore.draftRoutineName.length > 0
                         ? "Capture this mission's discoveries..."
                         : "Engage a routine first to start a mission log."
        placeholderTextColor: Theme.textGhost
        wrapMode: TextArea.Wrap
        font.family: root.bodyFont
        font.pixelSize: 14
        font.letterSpacing: 0
        background: Rectangle {
            color: Theme.voidColor
            border.width: 1
            border.color: notesArea.activeFocus ? Theme.goldDim : Theme.textGhost
        }

        onTextChanged: {
            if (notesStore.text !== text) {
                notesStore.text = text
            }
        }
    }

    // Today's combined notes — read-only
    Flickable {
        visible: root.activeView === 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: viewToggle.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 14
        clip: true
        contentHeight: combinedText.implicitHeight + 16

        Rectangle {
            width: parent.width
            height: combinedText.implicitHeight + 16
            color: Theme.voidColor
            border.width: 1
            border.color: Theme.textGhost

            Text {
                id: combinedText
                anchors.fill: parent
                anchors.margins: 12
                text: notesStore.todayNoteCount > 0
                      ? notesStore.todayCombinedNotes
                      : "Today is unwritten."
                color: notesStore.todayNoteCount > 0 ? Theme.textPrimary : Theme.textGhost
                wrapMode: Text.WordWrap
                font.family: root.bodyFont
                font.pixelSize: 12
                lineHeight: 1.4
            }
        }
    }

    Connections {
        target: notesStore
        function onTextChanged() {
            if (notesArea.text !== notesStore.text) {
                notesArea.text = notesStore.text
            }
        }
    }
}
