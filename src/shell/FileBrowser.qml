import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

// In-app file browser — the mid-session way to reach a document without a real
// file manager (those get killed by the lockdown, and can launch arbitrary apps).
// It is a real top-level window (same process, no extra QQuickView) so files can
// be dragged out into other apps. It is jailed to the routine's allowed roots
// (only the standard folders the routine opted into, plus its optional access
// folder): navigation can't climb above a root, and opening a file goes through
// RoutineManager.openFileInSession(), which refuses executables / .desktop launchers.
Window {
    id: root

    property string headerFont
    property string bodyFont

    width: 820
    height: 640
    minimumWidth: 520
    minimumHeight: 420
    title: "FocusOS — Files"
    color: Theme.iron
    flags: Qt.Window
    visible: false

    // Navigation state. roots is the list of allowed root folders; currentPath is
    // the folder being shown. entries re-queries whenever currentPath changes.
    property var roots: []
    property string currentPath: ""
    property string statusText: ""
    property var entries: (visible && currentPath.length > 0) ? routineManager.listFolder(currentPath) : []

    function isRoot(path) {
        for (let i = 0; i < roots.length; ++i) {
            if (roots[i].path === path) {
                return true
            }
        }
        return false
    }

    function parentOf(path) {
        const cut = path.lastIndexOf("/")
        return cut > 0 ? path.substring(0, cut) : path
    }

    function rootLabelFor(path) {
        for (let i = 0; i < roots.length; ++i) {
            if (path === roots[i].path || path.indexOf(roots[i].path + "/") === 0) {
                const tail = path === roots[i].path ? "" : path.substring(roots[i].path.length)
                return roots[i].name + tail
            }
        }
        return path
    }

    function openBrowser() {
        roots = routineManager.browseRoots()
        statusText = ""
        currentPath = roots.length > 0 ? roots[0].path : ""
        show()
        raise()
        requestActivate()
    }

    function closeBrowser() {
        statusText = ""
        hide()
    }

    function navigateInto(path) {
        statusText = ""
        currentPath = path
    }

    function goUp() {
        if (!isRoot(currentPath)) {
            navigateInto(parentOf(currentPath))
        }
    }

    function openEntry(entry) {
        if (entry.isDir) {
            navigateInto(entry.path)
        } else {
            const result = routineManager.openFileInSession(entry.path)
            statusText = result.length > 0 ? ("OPENED " + entry.name) : "COULDN'T OPEN THAT FILE"
        }
    }

    onClosing: root.statusText = ""

    Shortcut {
        sequence: "Escape"
        onActivated: root.closeBrowser()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.iron
        border.width: 1
        border.color: Theme.gold

        // ── Header ──
        Rectangle {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 48
            color: Theme.steel

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "📁 FILES"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 16
                font.letterSpacing: 0
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 28
                color: closeMouse.containsMouse ? Theme.crimsonHot : "transparent"
                border.width: 1
                border.color: closeMouse.containsMouse ? Theme.gold : Theme.crimson

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: closeMouse.containsMouse ? Theme.gold : Theme.crimson
                    font.family: root.headerFont
                    font.pixelSize: 13
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeBrowser()
                }
            }
        }

        // ── Roots strip ──
        Flow {
            id: rootsStrip
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 12
            spacing: 8

            Repeater {
                model: root.roots

                delegate: Rectangle {
                    required property var modelData
                    property bool selected: root.currentPath === modelData.path
                                            || root.currentPath.indexOf(modelData.path + "/") === 0
                    width: rootLabel.implicitWidth + 22
                    height: 28
                    color: selected ? Theme.steel : (rootMouse.containsMouse ? "#33141420" : "transparent")
                    border.width: 1
                    border.color: selected ? Theme.gold : (rootMouse.containsMouse ? Theme.goldDim : Theme.textGhost)

                    Text {
                        id: rootLabel
                        anchors.centerIn: parent
                        text: modelData.name
                        color: selected ? Theme.gold : (rootMouse.containsMouse ? Theme.goldDim : Theme.textDim)
                        font.family: root.headerFont
                        font.pixelSize: 11
                        font.letterSpacing: 0
                    }

                    MouseArea {
                        id: rootMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.navigateInto(modelData.path)
                    }
                }
            }
        }

        // ── Path bar (breadcrumb + UP) ──
        Item {
            id: pathBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: rootsStrip.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 12
            height: 30

            Rectangle {
                id: upButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 60
                height: 28
                property bool atRoot: root.isRoot(root.currentPath)
                color: !atRoot && upMouse.containsMouse ? Theme.steel : "transparent"
                border.width: 1
                border.color: atRoot ? Theme.textGhost : (upMouse.containsMouse ? Theme.gold : Theme.goldDim)
                opacity: atRoot ? 0.4 : 1

                Text {
                    anchors.centerIn: parent
                    text: "⬆ UP"
                    color: upButton.atRoot ? Theme.textDim : (upMouse.containsMouse ? Theme.gold : Theme.goldDim)
                    font.family: root.headerFont
                    font.pixelSize: 11
                }

                MouseArea {
                    id: upMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !upButton.atRoot
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.goUp()
                }
            }

            Text {
                anchors.left: upButton.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.rootLabelFor(root.currentPath)
                elide: Text.ElideMiddle
                color: Theme.textDim
                font.family: root.bodyFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }
        }

        // ── Entries ──
        Rectangle {
            id: listFrame
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: pathBar.bottom
            anchors.bottom: statusBar.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            color: "#22000005"
            border.width: 1
            border.color: Theme.textGhost

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.entries
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: entryRow
                    required property var modelData
                    width: ListView.view.width
                    height: 36
                    color: entryHover.hovered ? "#33141420" : "transparent"

                    // Drag a file out into another app — the reason this is a real
                    // window. Folders aren't draggable; clicking either opens the
                    // file (default app) or navigates into the folder.
                    Drag.active: dragHandler.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: { "text/uri-list": "file://" + encodeURI(entryRow.modelData.path) }

                    HoverHandler {
                        id: entryHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    DragHandler {
                        id: dragHandler
                        enabled: !entryRow.modelData.isDir
                        target: null
                    }

                    TapHandler {
                        onTapped: root.openEntry(entryRow.modelData)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: (entryRow.modelData.isDir ? "📁  " : "📄  ") + entryRow.modelData.name
                        elide: Text.ElideRight
                        width: parent.width - 24
                        color: entryRow.modelData.isDir
                               ? (entryHover.hovered ? Theme.gold : Theme.goldDim)
                               : (entryHover.hovered ? Theme.textPrimary : Theme.textDim)
                        font.family: root.bodyFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: list.count === 0
                text: root.roots.length === 0
                      ? "NO FOLDERS ENABLED FOR THIS ROUTINE — ADD ONE IN SETTINGS › ROUTINES"
                      : "EMPTY FOLDER"
                color: Theme.textGhost
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 2
            }
        }

        // ── Status bar ──
        Item {
            id: statusBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            height: 34

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                text: root.statusText.length > 0
                      ? root.statusText
                      : "Folders open in place · files open in their default app · drag a file out to another app"
                elide: Text.ElideRight
                color: root.statusText.length > 0 ? Theme.gold : Theme.textGhost
                font.family: root.bodyFont
                font.pixelSize: 11
                font.letterSpacing: 0
            }
        }
    }
}
