import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property bool open: false
    property string headerFont
    property string bodyFont
    property var routineDrafts: []
    property int selectedIndex: -1
    property bool advancedOpen: false
    property string saveStatus: ""
    property string urlDraft: ""

    visible: opacity > 0
    opacity: open ? 1 : 0
    focus: open
    Keys.onEscapePressed: function(event) {
        closeEditor()
        event.accepted = true
    }

    function toArray(value) {
        const values = []
        if (!value) return values
        for (let i = 0; i < value.length; ++i) {
            const text = String(value[i]).trim()
            if (text.length > 0) values.push(text)
        }
        return values
    }

    function normalizeRoutine(routine) {
        return {
            "id": String(routine.id || ""),
            "name": String(routine.name || ""),
            "description": String(routine.description || ""),
            "apps": toArray(routine.apps),
            "allowed_urls": toArray(routine.allowed_urls),
            "time_limit_minutes": Math.max(1, Number(routine.time_limit_minutes || 60)),
            "min_time_minutes": Math.max(0, Number(routine.min_time_minutes || 0)),
            "network_lock": routine.network_lock === undefined ? true : Boolean(routine.network_lock),
            "break_frequency_minutes": Math.max(0, Number(routine.break_frequency_minutes || 0)),
            "break_duration_minutes": Math.max(0, Number(routine.break_duration_minutes || 0))
        }
    }

    function cloneDrafts() {
        const drafts = []
        for (let i = 0; i < routineDrafts.length; ++i) {
            drafts.push(normalizeRoutine(routineDrafts[i]))
        }
        return drafts
    }

    function loadRoutineDrafts(preferredId, preferredName) {
        const source = routineManager.routinesForEditing()
        const drafts = []
        for (let i = 0; i < source.length; ++i) {
            drafts.push(normalizeRoutine(source[i]))
        }
        routineDrafts = drafts
        selectedIndex = drafts.length > 0 ? 0 : -1
        if (preferredId && preferredId.length > 0) {
            for (let j = 0; j < drafts.length; ++j) {
                if (drafts[j].id === preferredId) {
                    selectedIndex = j
                    break
                }
            }
        } else if (preferredName && preferredName.length > 0) {
            for (let k = 0; k < drafts.length; ++k) {
                if (drafts[k].name === preferredName) {
                    selectedIndex = k
                    break
                }
            }
        }
    }

    function openEditor() {
        if (!routineManager.accessGranted) return
        saveStatus = ""
        urlDraft = ""
        loadRoutineDrafts("", "")
        open = true
        forceActiveFocus()
    }

    function closeEditor() {
        open = false
        saveStatus = ""
        urlDraft = ""
    }

    function selectedRoutine() {
        if (selectedIndex < 0 || selectedIndex >= routineDrafts.length) return null
        return routineDrafts[selectedIndex]
    }

    function currentValue(key, fallback) {
        const routine = selectedRoutine()
        if (!routine || routine[key] === undefined) return fallback
        return routine[key]
    }

    function currentArray(key) {
        const routine = selectedRoutine()
        return routine ? toArray(routine[key]) : []
    }

    function replaceRoutine(index, routine) {
        if (index < 0 || index >= routineDrafts.length) return
        const drafts = cloneDrafts()
        drafts[index] = normalizeRoutine(routine)
        routineDrafts = drafts
        selectedIndex = Math.min(index, routineDrafts.length - 1)
        saveStatus = "UNSAVED"
    }

    function updateSelectedField(key, value) {
        const routine = selectedRoutine()
        if (!routine) return
        routine[key] = value
        replaceRoutine(selectedIndex, routine)
    }

    function addRoutine() {
        const drafts = cloneDrafts()
        const number = drafts.length + 1
        drafts.push({
            "id": "",
            "name": "NEW ROUTINE " + number,
            "description": "",
            "apps": [],
            "allowed_urls": [],
            "time_limit_minutes": 60,
            "min_time_minutes": 0,
            "network_lock": true,
            "break_frequency_minutes": 0,
            "break_duration_minutes": 0
        })
        routineDrafts = drafts
        selectedIndex = drafts.length - 1
        saveStatus = "UNSAVED"
    }

    function duplicateRoutine(index) {
        if (index < 0 || index >= routineDrafts.length) return
        const drafts = cloneDrafts()
        const copy = normalizeRoutine(drafts[index])
        copy.id = ""
        copy.name = copy.name + " COPY"
        drafts.splice(index + 1, 0, copy)
        routineDrafts = drafts
        selectedIndex = index + 1
        saveStatus = "UNSAVED"
    }

    function deleteRoutine(index) {
        if (index < 0 || index >= routineDrafts.length) return
        const drafts = cloneDrafts()
        drafts.splice(index, 1)
        routineDrafts = drafts
        selectedIndex = drafts.length === 0 ? -1 : Math.min(index, drafts.length - 1)
        saveStatus = "UNSAVED"
    }

    function addApp(path) {
        const trimmed = String(path || "").trim()
        if (trimmed.length === 0) return
        const routine = selectedRoutine()
        if (!routine) return
        const apps = toArray(routine.apps)
        apps.push(trimmed)
        routine.apps = apps
        replaceRoutine(selectedIndex, routine)
    }

    function removeApp(appIndex) {
        const routine = selectedRoutine()
        if (!routine) return
        const apps = toArray(routine.apps)
        apps.splice(appIndex, 1)
        routine.apps = apps
        replaceRoutine(selectedIndex, routine)
    }

    function addUrl(url) {
        const trimmed = String(url || "").trim()
        if (trimmed.length === 0) return
        const routine = selectedRoutine()
        if (!routine) return
        const urls = toArray(routine.allowed_urls)
        urls.push(trimmed)
        routine.allowed_urls = urls
        replaceRoutine(selectedIndex, routine)
        urlDraft = ""
    }

    function removeUrl(urlIndex) {
        const routine = selectedRoutine()
        if (!routine) return
        const urls = toArray(routine.allowed_urls)
        urls.splice(urlIndex, 1)
        routine.allowed_urls = urls
        replaceRoutine(selectedIndex, routine)
    }

    function saveRoutines() {
        const preferredId = selectedRoutine() ? String(selectedRoutine().id || "") : ""
        const preferredName = selectedRoutine() ? String(selectedRoutine().name || "") : ""
        const ok = routineManager.saveRoutines(cloneDrafts())
        saveStatus = ok ? "SAVED" : "SAVE FAILED"
        if (ok) {
            loadRoutineDrafts(preferredId, preferredName)
        }
    }

    Behavior on opacity {
        NumberAnimation { duration: Theme.transitionMs; easing.type: Easing.InOutQuad }
    }

    Connections {
        target: routineManager
        function onAccessChanged() {
            if (!routineManager.accessGranted && root.open) {
                root.closeEditor()
            }
        }
    }

    component EditorButton: Button {
        id: button
        property bool danger: false
        property bool compact: false

        focusPolicy: Qt.TabFocus
        implicitHeight: compact ? 28 : 36
        implicitWidth: compact ? 76 : 140
        font.family: root.headerFont
        font.pixelSize: compact ? 10 : 12

        contentItem: Text {
            text: button.text
            color: button.enabled ? Theme.gold : Theme.textGhost
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.family: button.font.family
            font.pixelSize: button.font.pixelSize
            font.letterSpacing: 0
        }
        background: Rectangle {
            color: !button.enabled
                   ? Theme.steel
                   : (button.down || button.hovered
                      ? (button.danger ? Theme.crimsonHot : Theme.steel)
                      : (button.danger ? Theme.crimson : Theme.iron))
            border.width: 1
            border.color: button.activeFocus || button.hovered ? Theme.gold : Theme.goldDim
        }
    }

    component EditorField: TextField {
        id: field
        color: Theme.textPrimary
        selectedTextColor: Theme.voidColor
        selectionColor: Theme.gold
        placeholderTextColor: Theme.textGhost
        font.family: root.bodyFont
        font.pixelSize: 13
        font.letterSpacing: 0
        background: Rectangle {
            color: Theme.steel
            border.width: 1
            border.color: field.activeFocus ? Theme.gold : Theme.goldDim
        }
    }

    component EditorSpin: SpinBox {
        id: spin
        editable: true
        focusPolicy: Qt.TabFocus
        font.family: root.bodyFont
        font.pixelSize: 12
        contentItem: TextInput {
            text: spin.textFromValue(spin.value, spin.locale)
            color: Theme.textPrimary
            selectedTextColor: Theme.voidColor
            selectionColor: Theme.gold
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            validator: spin.validator
            inputMethodHints: Qt.ImhDigitsOnly
            font: spin.font
        }
        up.indicator: Text {
            x: spin.width - width - 8
            y: 7
            text: "+"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 12
        }
        down.indicator: Text {
            x: 8
            y: 7
            text: "-"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 12
        }
        background: Rectangle {
            color: Theme.steel
            border.width: 1
            border.color: spin.activeFocus ? Theme.gold : Theme.goldDim
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#dd050508"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.open
        onClicked: function(mouse) { mouse.accepted = true }
    }

    Rectangle {
        id: dialog
        width: Math.min(1180, parent.width - 56)
        height: Math.min(780, parent.height - 56)
        anchors.centerIn: parent
        color: Theme.iron
        border.width: 1
        border.color: Theme.gold

        Rectangle {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 54
            color: Theme.crimson

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: "ROUTINE EDITOR"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 16
                font.letterSpacing: 0
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 188
                anchors.verticalCenter: parent.verticalCenter
                text: root.saveStatus
                color: root.saveStatus === "SAVE FAILED" ? Theme.crimsonHot : Theme.goldDim
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            EditorButton {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 30
                text: "X"
                danger: true
                compact: true
                onClicked: root.closeEditor()
            }
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: footer.top
            anchors.margins: 16
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: "#ee0a0a14"
                border.width: 1
                border.color: Theme.goldDim

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    EditorButton {
                        Layout.fillWidth: true
                        text: "NEW ROUTINE"
                        danger: false
                        onClicked: root.addRoutine()
                    }

                    ListView {
                        id: routineList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: root.routineDrafts

                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property var modelData

                            width: routineList.width
                            height: 66
                            color: root.selectedIndex === row.index
                                   ? "#ee1a1024"
                                   : (rowMouse.containsMouse ? "#dd141420" : Theme.iron)
                            border.width: 1
                            border.color: root.selectedIndex === row.index
                                          ? Theme.gold
                                          : (rowMouse.containsMouse ? Theme.goldDim : Theme.textGhost)

                            Text {
                                anchors.left: parent.left
                                anchors.right: rowActions.left
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                anchors.top: parent.top
                                anchors.topMargin: 10
                                text: String(row.modelData.name || "UNTITLED")
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                font.family: root.headerFont
                                font.pixelSize: 13
                                font.letterSpacing: 0
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.right: rowActions.left
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 10
                                text: Number(row.modelData.time_limit_minutes || 60) + " MIN"
                                color: Theme.textGhost
                                elide: Text.ElideRight
                                font.family: root.bodyFont
                                font.pixelSize: 10
                                font.letterSpacing: 0
                            }

                            Row {
                                id: rowActions
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                EditorButton {
                                    width: 38
                                    height: 28
                                    text: "DUP"
                                    compact: true
                                    danger: false
                                    onClicked: root.duplicateRoutine(row.index)
                                }

                                EditorButton {
                                    width: 28
                                    height: 28
                                    text: "X"
                                    compact: true
                                    danger: true
                                    onClicked: root.deleteRoutine(row.index)
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.left: parent.left
                                anchors.right: rowActions.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectedIndex = row.index
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#ee06060d"
                border.width: 1
                border.color: Theme.goldDim

                Flickable {
                    id: editorFlick
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    contentWidth: width
                    contentHeight: editorColumn.implicitHeight

                    ColumnLayout {
                        id: editorColumn
                        width: editorFlick.width
                        spacing: 14
                        enabled: root.selectedIndex >= 0
                        opacity: enabled ? 1 : 0.35

                        Text {
                            visible: root.selectedIndex < 0
                            Layout.fillWidth: true
                            text: "NO ROUTINE SELECTED"
                            color: Theme.textGhost
                            horizontalAlignment: Text.AlignHCenter
                            font.family: root.headerFont
                            font.pixelSize: 14
                            font.letterSpacing: 0
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "MISSION PROFILE"
                            color: Theme.gold
                            font.family: root.headerFont
                            font.pixelSize: 14
                            font.letterSpacing: 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.preferredWidth: 92
                                text: "NAME"
                                color: Theme.goldDim
                                font.family: root.headerFont
                                font.pixelSize: 12
                            }

                            EditorField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: root.currentValue("name", "")
                                placeholderText: "ROUTINE NAME"
                                onTextEdited: root.updateSelectedField("name", text)
                            }

                            Text {
                                text: "DURATION"
                                color: Theme.goldDim
                                font.family: root.headerFont
                                font.pixelSize: 12
                            }

                            EditorSpin {
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 36
                                from: 1
                                to: 1440
                                value: Number(root.currentValue("time_limit_minutes", 60))
                                onValueModified: root.updateSelectedField("time_limit_minutes", value)
                            }
                        }

                        CheckBox {
                            id: networkCheck
                            Layout.fillWidth: true
                            checked: Boolean(root.currentValue("network_lock", true))
                            text: "NETWORK LOCK"
                            font.family: root.headerFont
                            font.pixelSize: 12
                            focusPolicy: Qt.TabFocus
                            onToggled: root.updateSelectedField("network_lock", checked)
                            contentItem: Text {
                                text: networkCheck.text
                                color: networkCheck.checked ? Theme.gold : Theme.textDim
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: networkCheck.indicator.width + 8
                                font: networkCheck.font
                            }
                            indicator: Rectangle {
                                implicitWidth: 42
                                implicitHeight: 22
                                y: (networkCheck.height - height) / 2
                                color: networkCheck.checked ? Theme.crimsonHot : Theme.steel
                                border.width: 1
                                border.color: networkCheck.activeFocus ? Theme.gold : Theme.goldDim
                                Rectangle {
                                    width: 16
                                    height: 16
                                    x: networkCheck.checked ? parent.width - width - 3 : 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Theme.gold
                                    Behavior on x { NumberAnimation { duration: 120 } }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "ALLOWED APPS"
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 12
                        }

                        Repeater {
                            model: root.currentArray("apps")
                            delegate: Rectangle {
                                required property int index
                                required property string modelData

                                Layout.fillWidth: true
                                height: 42
                                color: Theme.iron
                                border.width: 1
                                border.color: Theme.textGhost

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: removeAppButton.left
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: routineManager.applicationDisplayName(modelData)
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    font.family: root.bodyFont
                                    font.pixelSize: 13
                                }

                                EditorButton {
                                    id: removeAppButton
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 30
                                    height: 28
                                    text: "X"
                                    compact: true
                                    danger: true
                                    onClicked: root.removeApp(index)
                                }
                            }
                        }

                        EditorButton {
                            Layout.preferredWidth: 150
                            text: "ADD APP"
                            danger: false
                            onClicked: root.addApp(routineManager.pickApplication())
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "ALLOWED URLS"
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 12
                        }

                        Repeater {
                            model: root.currentArray("allowed_urls")
                            delegate: Rectangle {
                                required property int index
                                required property string modelData

                                Layout.fillWidth: true
                                height: 38
                                color: Theme.iron
                                border.width: 1
                                border.color: Theme.textGhost

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: removeUrlButton.left
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    font.family: root.bodyFont
                                    font.pixelSize: 12
                                }

                                EditorButton {
                                    id: removeUrlButton
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 30
                                    height: 26
                                    text: "X"
                                    compact: true
                                    danger: true
                                    onClicked: root.removeUrl(index)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            EditorField {
                                id: urlField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: root.urlDraft
                                placeholderText: "example.com or https://example.com/path"
                                onTextEdited: root.urlDraft = text
                                onAccepted: root.addUrl(text)
                            }

                            EditorButton {
                                Layout.preferredWidth: 86
                                text: "ADD"
                                danger: false
                                onClicked: root.addUrl(urlField.text)
                            }
                        }

                        EditorButton {
                            Layout.preferredWidth: 180
                            text: root.advancedOpen ? "HIDE ADVANCED" : "ADVANCED"
                            danger: false
                            onClicked: root.advancedOpen = !root.advancedOpen
                        }

                        Rectangle {
                            visible: root.advancedOpen
                            Layout.fillWidth: true
                            height: visible ? 72 : 0
                            color: Theme.iron
                            border.width: 1
                            border.color: Theme.goldDim

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                Text {
                                    text: "BREAK EVERY"
                                    color: Theme.goldDim
                                    font.family: root.headerFont
                                    font.pixelSize: 12
                                }

                                EditorSpin {
                                    Layout.preferredWidth: 108
                                    Layout.preferredHeight: 34
                                    from: 0
                                    to: 1440
                                    value: Number(root.currentValue("break_frequency_minutes", 0))
                                    onValueModified: root.updateSelectedField("break_frequency_minutes", value)
                                }

                                Text {
                                    text: "MIN FOR"
                                    color: Theme.goldDim
                                    font.family: root.headerFont
                                    font.pixelSize: 12
                                }

                                EditorSpin {
                                    Layout.preferredWidth: 108
                                    Layout.preferredHeight: 34
                                    from: 0
                                    to: 240
                                    value: Number(root.currentValue("break_duration_minutes", 0))
                                    onValueModified: root.updateSelectedField("break_duration_minutes", value)
                                }

                                Text {
                                    text: "MIN"
                                    color: Theme.goldDim
                                    font.family: root.headerFont
                                    font.pixelSize: 12
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "NOTES"
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 12
                        }

                        TextArea {
                            id: notesField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 132
                            text: root.currentValue("description", "")
                            placeholderText: "Mission objective, constraints, or launch checklist."
                            placeholderTextColor: Theme.textGhost
                            color: Theme.textPrimary
                            selectedTextColor: Theme.voidColor
                            selectionColor: Theme.gold
                            wrapMode: TextArea.Wrap
                            font.family: root.bodyFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                            onTextChanged: {
                                if (activeFocus) {
                                    root.updateSelectedField("description", text)
                                }
                            }
                            background: Rectangle {
                                color: Theme.steel
                                border.width: 1
                                border.color: notesField.activeFocus ? Theme.gold : Theme.goldDim
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 62
            color: "#ee0a0a14"
            border.width: 1
            border.color: Theme.textGhost

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: root.routineDrafts.length + " ROUTINES"
                    color: Theme.textDim
                    font.family: root.headerFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                EditorButton {
                    Layout.preferredWidth: 126
                    text: "CANCEL"
                    danger: false
                    onClicked: root.closeEditor()
                }

                EditorButton {
                    Layout.preferredWidth: 156
                    text: "SAVE ROUTINES"
                    danger: true
                    onClicked: root.saveRoutines()
                }
            }
        }
    }
}
