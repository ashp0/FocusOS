import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property bool modalOpen: false
    property bool adminUnlocked: false
    property int activeTab: 0
    property string headerFont
    property string bodyFont
    property string errorText: ""
    property string saveStatus: ""
    property string resetConfirmation: ""
    property string deviceInfoText: ""
    property bool showEnrollmentQr: false
    property var routineDrafts: []

    visible: opacity > 0
    opacity: modalOpen ? 1 : 0

    function toArray(value) {
        const values = []
        if (!value) {
            return values
        }
        for (let i = 0; i < value.length; ++i) {
            const text = String(value[i]).trim()
            if (text.length > 0) {
                values.push(text)
            }
        }
        return values
    }

    function groupSecret(secret) {
        const compact = String(secret || "").replace(/\s+/g, "")
        const groups = []
        for (let i = 0; i < compact.length; i += 4) {
            groups.push(compact.slice(i, i + 4))
        }
        return groups.join(" ")
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

    function loadRoutineDrafts() {
        const source = routineManager.routinesForEditing()
        const drafts = []
        for (let i = 0; i < source.length; ++i) {
            drafts.push(normalizeRoutine(source[i]))
        }
        routineDrafts = drafts
    }

    function openModal() {
        errorText = ""
        saveStatus = ""
        resetConfirmation = ""
        showEnrollmentQr = false
        updateDeviceInfo()
        activeTab = 0
        codeField.text = ""
        adminUnlocked = routineManager.accessGranted
        if (adminUnlocked) {
            loadRoutineDrafts()
        }
        modalOpen = true
        if (!adminUnlocked) {
            codeField.forceActiveFocus()
        }
    }

    function closeModal() {
        modalOpen = false
        codeField.text = ""
        errorText = ""
        saveStatus = ""
        resetConfirmation = ""
        showEnrollmentQr = false
    }

    function allowedUrlsText(routine) {
        return toArray(routine.allowed_urls).join(", ")
    }

    function urlsFromText(text) {
        const values = []
        const parts = String(text).split(",")
        for (let i = 0; i < parts.length; ++i) {
            const trimmed = parts[i].trim()
            if (trimmed.length > 0) {
                values.push(trimmed)
            }
        }
        return values
    }

    function updateRoutineField(routineIndex, key, value) {
        if (routineIndex < 0 || routineIndex >= routineDrafts.length) {
            return
        }
        routineDrafts[routineIndex][key] = value
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
    }

    function removeRoutine(routineIndex) {
        const drafts = cloneDrafts()
        drafts.splice(routineIndex, 1)
        routineDrafts = drafts
    }

    function addApp(routineIndex, path) {
        if (!path || routineIndex < 0 || routineIndex >= routineDrafts.length) {
            return
        }
        const drafts = cloneDrafts()
        drafts[routineIndex].apps.push(path)
        routineDrafts = drafts
    }

    function removeApp(routineIndex, appIndex) {
        const drafts = cloneDrafts()
        drafts[routineIndex].apps.splice(appIndex, 1)
        routineDrafts = drafts
    }

    function updateApp(routineIndex, appIndex, path) {
        if (routineIndex < 0 || routineIndex >= routineDrafts.length) {
            return
        }
        routineDrafts[routineIndex].apps[appIndex] = path
    }

    function saveRoutines() {
        saveStatus = routineManager.saveRoutines(cloneDrafts()) ? "SAVED" : "SAVE FAILED"
        loadRoutineDrafts()
    }

    function behaviorIndex(behavior) {
        if (behavior === "low") {
            return 1
        }
        if (behavior === "same") {
            return 2
        }
        return 0
    }

    function behaviorValue(index) {
        if (index === 1) {
            return "low"
        }
        if (index === 2) {
            return "same"
        }
        return "stop"
    }

    function updateDeviceInfo() {
        const date = new Date()
        const days = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
        const months = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
        deviceInfoText = days[date.getDay()] + " " +
                         months[date.getMonth()] + " " +
                         date.getDate() + " " +
                         date.getFullYear() + "  ■  " +
                         Theme.pad2(date.getHours()) + ":" +
                         Theme.pad2(date.getMinutes()) + "  ■  " +
                         systemStatus.batteryLabel
    }

    Component.onCompleted: updateDeviceInfo()

    Timer {
        interval: 30000
        running: root.modalOpen && !root.adminUnlocked
        repeat: true
        onTriggered: root.updateDeviceInfo()
    }

    Connections {
        target: systemStatus
        function onStatusChanged() {
            root.updateDeviceInfo()
        }
    }

    Behavior on opacity {
        NumberAnimation { duration: Theme.transitionMs; easing.type: Easing.InOutQuad }
    }

    Connections {
        target: routineManager
        function onAccessChanged() {
            if (!routineManager.accessGranted && root.adminUnlocked) {
                root.adminUnlocked = false
                codeField.text = ""
                root.errorText = ""
                root.saveStatus = ""
                root.resetConfirmation = ""
                root.showEnrollmentQr = false
                if (root.modalOpen) {
                    codeField.forceActiveFocus()
                }
            }
        }
    }

    component AdminTextField: TextField {
        id: field
        color: Theme.textPrimary
        selectedTextColor: Theme.voidColor
        selectionColor: Theme.gold
        placeholderTextColor: Theme.textGhost
        font.family: root.bodyFont
        font.pixelSize: 12
        font.letterSpacing: 0
        background: Rectangle {
            color: Theme.steel
            border.width: 1
            border.color: field.activeFocus ? Theme.gold : Theme.goldDim
        }
    }

    component AdminTextArea: TextArea {
        id: area
        color: Theme.textPrimary
        selectedTextColor: Theme.voidColor
        selectionColor: Theme.gold
        placeholderTextColor: Theme.textGhost
        wrapMode: TextArea.Wrap
        font.family: root.bodyFont
        font.pixelSize: 12
        font.letterSpacing: 0
        background: Rectangle {
            color: Theme.steel
            border.width: 1
            border.color: area.activeFocus ? Theme.gold : Theme.goldDim
        }
    }

    component AdminButton: Rectangle {
        id: button
        property string label: ""
        property bool actionEnabled: true
        property bool danger: true
        signal clicked()

        implicitWidth: 132
        implicitHeight: 34
        color: actionEnabled ? (buttonMouse.containsMouse ? Theme.crimsonHot : Theme.crimson) : Theme.steel
        border.width: 1
        border.color: actionEnabled && buttonMouse.containsMouse ? Theme.gold : Theme.goldDim
        opacity: actionEnabled ? 1 : 0.45

        Text {
            anchors.centerIn: parent
            text: button.label
            color: button.actionEnabled ? Theme.gold : Theme.textGhost
            font.family: root.headerFont
            font.pixelSize: 12
            font.letterSpacing: 0
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: button.actionEnabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: button.clicked()
        }
    }

    component AdminSpinner: Item {
        id: spin
        property int value: 0
        property int from: 0
        property int to: 1440
        property int stepSize: 1
        signal valueModified(int value)

        implicitWidth: 150
        implicitHeight: 34

        function clamp(nextValue) {
            return Math.max(spin.from, Math.min(spin.to, Math.round(Number(nextValue))))
        }

        function setSpinValue(nextValue) {
            const clamped = clamp(nextValue)
            if (spin.value !== clamped) {
                spin.value = clamped
                spin.valueModified(clamped)
            } else {
                spinField.text = String(spin.value)
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            AdminButton {
                Layout.preferredWidth: 34
                Layout.fillHeight: true
                label: "-"
                danger: false
                onClicked: spin.setSpinValue(spin.value - spin.stepSize)
            }

            AdminTextField {
                id: spinField
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: String(spin.value)
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: spin.from; top: spin.to }
                onEditingFinished: spin.setSpinValue(text)
            }

            AdminButton {
                Layout.preferredWidth: 34
                Layout.fillHeight: true
                label: "+"
                danger: false
                onClicked: spin.setSpinValue(spin.value + spin.stepSize)
            }
        }

        onValueChanged: spinField.text = String(spin.value)
    }

    Rectangle {
        anchors.fill: parent
        color: "#cc050508"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.modalOpen
        onClicked: function(mouse) {
            mouse.accepted = true
        }
    }

    Rectangle {
        id: modal
        width: root.adminUnlocked ? Math.min(1040, parent.width - 72) : Math.min(520, parent.width - 48)
        height: root.adminUnlocked ? Math.min(760, parent.height - 72) : (totpEngine.firstLaunch ? Math.min(680, parent.height - 48) : 322)
        anchors.centerIn: parent
        color: Theme.iron
        border.width: 1
        border.color: Theme.crimsonHot

        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
                mouse.accepted = true
            }
        }

        Rectangle {
            id: modalHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 52
            color: Theme.crimson

            Text {
                anchors.centerIn: parent
                text: root.adminUnlocked ? "◈ OTHER ADMINISTRATION" : "◈ OTHER ACCESS AUTHORIZATION"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 16
                font.letterSpacing: 0
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 30
                color: closeMouse.containsMouse ? Theme.crimsonHot : Theme.crimson
                border.width: 1
                border.color: closeMouse.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: Theme.gold
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeModal()
                }
            }
        }

        Item {
            id: lockedPanel
            visible: !root.adminUnlocked
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: modalHeader.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 22

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16

                Text {
                    visible: totpEngine.firstLaunch
                    Layout.fillWidth: true
                    text: "══ ■ FIRST-LAUNCH PAIRING ■ ══"
                    color: Theme.goldDim
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }

                Image {
                    visible: totpEngine.firstLaunch
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 256
                    Layout.preferredHeight: 256
                    fillMode: Image.PreserveAspectFit
                    source: totpEngine.qrCodeDataUrl
                }

                Rectangle {
                    visible: totpEngine.firstLaunch
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    color: Theme.voidColor
                    border.width: 1
                    border.color: Theme.goldDim

                    Text {
                        anchors.fill: parent
                        anchors.margins: 8
                        text: root.groupSecret(totpEngine.secret)
                        color: Theme.textDim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WrapAnywhere
                        font.family: root.bodyFont
                        font.pixelSize: 12
                        font.letterSpacing: 0
                    }
                }

                Text {
                    visible: totpEngine.firstLaunch
                    Layout.fillWidth: true
                    text: "SCAN QR IN AN AUTHENTICATOR APP, THEN ENTER ITS CURRENT 6-DIGIT CODE"
                    color: Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.family: root.bodyFont
                    font.pixelSize: 11
                    font.letterSpacing: 0
                }

                Text {
                    visible: totpEngine.firstLaunch
                    Layout.fillWidth: true
                    text: "IPHONE PASSWORDS: CODES > + > SETUP KEY. USE THE KEY ABOVE IF CAMERA SCAN FAILS."
                    color: Theme.textGhost
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.family: root.bodyFont
                    font.pixelSize: 10
                    font.letterSpacing: 0
                }

                Text {
                    Layout.fillWidth: true
                    text: root.deviceInfoText
                    color: Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.headerFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                TextField {
                    id: codeField
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 210
                    Layout.preferredHeight: 52
                    maximumLength: 6
                    horizontalAlignment: TextInput.AlignHCenter
                    inputMethodHints: Qt.ImhDigitsOnly
                    color: Theme.textPrimary
                    selectedTextColor: Theme.voidColor
                    selectionColor: Theme.gold
                    placeholderText: "000000"
                    placeholderTextColor: Theme.textGhost
                    font.family: root.headerFont
                    font.pixelSize: 24
                    font.letterSpacing: 0
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                    background: Rectangle {
                        color: Theme.steel
                        border.width: 1
                        border.color: root.errorText.length > 0 ? Theme.crimsonHot : (codeField.activeFocus ? Theme.gold : Theme.goldDim)
                    }

                    Keys.onEscapePressed: function(event) {
                        root.closeModal()
                        event.accepted = true
                    }

                    onTextChanged: {
                        root.errorText = ""
                        if (text.length === 6) {
                            if (totpEngine.validate(text)) {
                                totpEngine.completeFirstLaunchEnrollment()
                                routineManager.unlockOtherAccess()
                                root.adminUnlocked = true
                                root.loadRoutineDrafts()
                                root.activeTab = 0
                                text = ""
                            } else {
                                root.errorText = "AUTHORIZATION FAILED"
                                text = ""
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.errorText
                    color: Theme.crimsonHot
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }
            }
        }

        ColumnLayout {
            id: adminPanel
            visible: root.adminUnlocked
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: modalHeader.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 18
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                spacing: 8

                Repeater {
                    model: ["ROUTINES", "ACCESS SETTINGS", "MUSIC"]
                    delegate: Rectangle {
                        required property int index
                        required property string modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: root.activeTab === index ? Theme.steel : Theme.iron
                        border.width: 1
                        border.color: root.activeTab === index ? Theme.gold : Theme.goldDim

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: root.activeTab === index ? Theme.gold : Theme.textDim
                            font.family: root.headerFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.activeTab = index
                        }
                    }
                }
            }

            // Quick toggle: show edit pencils on the main screen
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: routineManager.editMode ? "#cc1a1a0a" : Theme.iron
                border.width: 1
                border.color: routineManager.editMode ? Theme.gold : Theme.goldDim

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "✎ SHOW EDIT BUTTONS ON ROUTINES"
                    color: routineManager.editMode ? Theme.gold : Theme.textDim
                    font.family: root.headerFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 64
                    height: 26
                    color: routineManager.editMode ? Theme.crimsonHot : Theme.steel
                    border.width: 1
                    border.color: routineManager.editMode ? Theme.gold : Theme.goldDim

                    Text {
                        anchors.centerIn: parent
                        text: routineManager.editMode ? "ON" : "OFF"
                        color: Theme.gold
                        font.family: root.headerFont
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: routineManager.editMode = !routineManager.editMode
                    }
                }

                MouseArea {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: 92
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.editMode = !routineManager.editMode
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    visible: root.activeTab === 0
                    anchors.fill: parent
                    spacing: 12

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: routineColumn.implicitHeight

                        Column {
                            id: routineColumn
                            width: parent.width
                            spacing: 12

                            Repeater {
                                model: root.routineDrafts

                                delegate: Rectangle {
                                    id: routineCard
                                    required property int index
                                    required property var modelData
                                    property bool confirmDelete: false

                                    width: routineColumn.width
                                    height: routineLayout.implicitHeight + 24
                                    color: Theme.iron
                                    border.width: 1
                                    border.color: Theme.goldDim

                                    ColumnLayout {
                                        id: routineLayout
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: "NAME"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminTextField {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 34
                                                text: String(routineCard.modelData.name || "")
                                                placeholderText: "ROUTINE NAME"
                                                onTextChanged: root.updateRoutineField(routineCard.index, "name", text)
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                Layout.preferredWidth: 96
                                                text: "OBJECTIVE"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminTextArea {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 72
                                                text: String(routineCard.modelData.description || "")
                                                placeholderText: "WHAT SHOULD THIS ROUTINE PRODUCE?"
                                                onTextChanged: root.updateRoutineField(routineCard.index, "description", text)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: "ALLOWED APPS"
                                            color: Theme.goldDim
                                            font.family: root.headerFont
                                            font.pixelSize: 12
                                            font.letterSpacing: 0
                                        }

                                        Repeater {
                                            model: root.toArray(routineCard.modelData.apps).length

                                            delegate: RowLayout {
                                                required property int index

                                                width: routineLayout.width
                                                spacing: 8

                                                AdminTextField {
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 34
                                                    text: root.toArray(routineCard.modelData.apps)[index]
                                                    placeholderText: "/usr/share/applications/firefox.desktop or /usr/bin/code"
                                                    onTextChanged: root.updateApp(routineCard.index, index, text)
                                                }

                                                AdminButton {
                                                    Layout.preferredWidth: 40
                                                    Layout.preferredHeight: 34
                                                    label: "✕"
                                                    onClicked: root.removeApp(routineCard.index, index)
                                                }
                                            }
                                        }

                                        AdminButton {
                                            Layout.preferredWidth: 164
                                            Layout.preferredHeight: 34
                                            label: "+ SELECT APP FILE"
                                            danger: false
                                            onClicked: {
                                                const path = routineManager.pickApplication()
                                                root.addApp(routineCard.index, path)
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: "ALLOWED URLS"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminTextField {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 34
                                                text: root.allowedUrlsText(routineCard.modelData)
                                                placeholderText: "arxiv.org, scholar.google.com"
                                                onTextChanged: root.updateRoutineField(routineCard.index, "allowed_urls", root.urlsFromText(text))
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 14

                                            Text {
                                                text: "TIME LIMIT"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminSpinner {
                                                value: Number(routineCard.modelData.time_limit_minutes || 60)
                                                from: 1
                                                to: 1440
                                                onValueModified: function(nextValue) {
                                                    root.updateRoutineField(routineCard.index, "time_limit_minutes", nextValue)
                                                }
                                            }

                                            Text {
                                                text: "MIN TIME"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminSpinner {
                                                value: Number(routineCard.modelData.min_time_minutes || 0)
                                                from: 0
                                                to: 1440
                                                onValueModified: function(nextValue) {
                                                    root.updateRoutineField(routineCard.index, "min_time_minutes", nextValue)
                                                }
                                            }

                                            Item { Layout.fillWidth: true }

                                            AdminButton {
                                                Layout.preferredWidth: 170
                                                Layout.preferredHeight: 34
                                                label: routineCard.confirmDelete ? "CONFIRM DELETE" : "✕ DELETE ROUTINE"
                                                onClicked: {
                                                    if (routineCard.confirmDelete) {
                                                        root.removeRoutine(routineCard.index)
                                                    } else {
                                                        routineCard.confirmDelete = true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 154
                            Layout.fillHeight: true
                            label: "+ NEW ROUTINE"
                            danger: false
                            onClicked: root.addRoutine()
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: root.saveStatus
                            color: root.saveStatus === "SAVED" ? Theme.gold : Theme.crimsonHot
                            font.family: root.headerFont
                            font.pixelSize: 12
                            font.letterSpacing: 0
                        }

                        AdminButton {
                            Layout.preferredWidth: 132
                            Layout.fillHeight: true
                            label: "▣ SAVE ALL"
                            onClicked: root.saveRoutines()
                        }
                    }
                }

                ColumnLayout {
                    visible: root.activeTab === 1
                    anchors.fill: parent
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "OTHER SESSION DURATION:"
                            color: Theme.textPrimary
                            font.family: root.headerFont
                            font.pixelSize: 14
                            font.letterSpacing: 0
                        }

                        AdminSpinner {
                            id: durationSpin
                            value: routineManager.otherAccessMinutes
                            from: 1
                            to: 1440
                            onValueModified: function(nextValue) {
                                routineManager.otherAccessMinutes = nextValue
                            }
                        }

                        Text {
                            text: "minutes"
                            color: Theme.textDim
                            font.family: root.bodyFont
                            font.pixelSize: 12
                            font.letterSpacing: 0
                        }

                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 190
                            Layout.preferredHeight: 34
                            label: "◈ SHOW ENROLLMENT QR"
                            danger: false
                            onClicked: root.showEnrollmentQr = true
                        }

                        AdminTextField {
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 34
                            text: root.resetConfirmation
                            placeholderText: "RESET"
                            onTextChanged: root.resetConfirmation = text
                        }

                        AdminButton {
                            Layout.preferredWidth: 176
                            Layout.preferredHeight: 34
                            label: "⊠ RESET TOTP SECRET"
                            actionEnabled: root.resetConfirmation === "RESET"
                            onClicked: {
                                totpEngine.resetSecret()
                                root.resetConfirmation = ""
                                root.showEnrollmentQr = true
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        visible: root.showEnrollmentQr
                        Layout.preferredWidth: 370
                        Layout.preferredHeight: 408
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 256
                                Layout.preferredHeight: 256
                                fillMode: Image.PreserveAspectFit
                                source: totpEngine.qrCodeDataUrl
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                text: root.groupSecret(totpEngine.secret)
                                color: Theme.textDim
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.WrapAnywhere
                                font.family: root.bodyFont
                                font.pixelSize: 12
                                font.letterSpacing: 0
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "USE THIS QR WITH A TRUSTED AUTHENTICATOR; IPHONE PASSWORDS CAN ALSO USE THE SETUP KEY ABOVE"
                                color: Theme.textGhost
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.family: root.bodyFont
                                font.pixelSize: 10
                                font.letterSpacing: 0
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                ColumnLayout {
                    visible: root.activeTab === 2
                    anchors.fill: parent
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: "DETECTED MUSIC FILES"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 12
                            clip: true
                            contentWidth: width
                            contentHeight: musicFileColumn.implicitHeight

                            Column {
                                id: musicFileColumn
                                width: parent.width
                                spacing: 8

                                Text {
                                    visible: musicEngine.musicFiles.length === 0
                                    width: parent.width
                                    text: musicEngine.available ? "BUNDLED FALLBACK DRONE ACTIVE" : "NO .MP3 OR .OGG FILES IN ~/.focusos/music/"
                                    color: musicEngine.available ? Theme.goldDim : Theme.textGhost
                                    font.family: root.bodyFont
                                    font.pixelSize: 12
                                    font.letterSpacing: 0
                                }

                                Repeater {
                                    model: musicEngine.musicFiles
                                    delegate: Text {
                                        required property string modelData
                                        width: musicFileColumn.width
                                        text: modelData
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        font.family: root.bodyFont
                                        font.pixelSize: 12
                                        font.letterSpacing: 0
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        spacing: 12

                        AdminButton {
                            Layout.preferredWidth: 178
                            Layout.fillHeight: true
                            label: "◈ OPEN MUSIC FOLDER"
                            danger: false
                            onClicked: {
                                musicEngine.openMusicFolder()
                                musicEngine.refreshMusicFiles()
                            }
                        }

                        AdminButton {
                            Layout.preferredWidth: 104
                            Layout.fillHeight: true
                            label: "↻ REFRESH"
                            danger: false
                            onClicked: musicEngine.refreshMusicFiles()
                        }

                        Text {
                            text: "ENGAGE BEHAVIOUR:"
                            color: Theme.textPrimary
                            font.family: root.headerFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                        }

                        ComboBox {
                            id: behaviorBox
                            Layout.preferredWidth: 230
                            Layout.fillHeight: true
                            model: ["Stop music", "Continue at low volume", "Continue at same volume"]
                            currentIndex: root.behaviorIndex(musicEngine.engageBehavior)
                            font.family: root.bodyFont
                            font.pixelSize: 12

                            onActivated: function(index) {
                                musicEngine.engageBehavior = root.behaviorValue(index)
                            }

                            contentItem: Text {
                                leftPadding: 10
                                rightPadding: 28
                                text: behaviorBox.displayText
                                color: Theme.textPrimary
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                font.family: root.bodyFont
                                font.pixelSize: 12
                                font.letterSpacing: 0
                            }

                            indicator: Text {
                                x: behaviorBox.width - width - 10
                                y: (behaviorBox.height - height) / 2
                                text: "▾"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 14
                                font.letterSpacing: 0
                            }

                            background: Rectangle {
                                color: Theme.steel
                                border.width: 1
                                border.color: behaviorBox.activeFocus ? Theme.gold : Theme.goldDim
                            }

                            delegate: ItemDelegate {
                                width: behaviorBox.width
                                height: 34
                                contentItem: Text {
                                    text: modelData
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: root.bodyFont
                                    font.pixelSize: 12
                                    font.letterSpacing: 0
                                }
                                background: Rectangle {
                                    color: highlighted ? Theme.steel : Theme.iron
                                    border.width: 0
                                }
                            }

                            popup: Popup {
                                y: behaviorBox.height
                                width: behaviorBox.width
                                implicitHeight: contentItem.implicitHeight
                                padding: 0
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: behaviorBox.popup.visible ? behaviorBox.delegateModel : null
                                    currentIndex: behaviorBox.highlightedIndex
                                }
                                background: Rectangle {
                                    color: Theme.iron
                                    border.width: 1
                                    border.color: Theme.goldDim
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            text: "⌐"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 18
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            text: "¬"
            color: Theme.gold
            font.family: root.headerFont
            font.pixelSize: 18
        }
    }
}
