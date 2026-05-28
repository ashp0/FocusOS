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
    property var settingsTabs: [
        {"index": 0, "code": "01", "label": "MISSION PLAN", "subtitle": "ROUTINES"},
        {"index": 1, "code": "02", "label": "ALLOWED APPS", "subtitle": "GLOBAL"},
        {"index": 2, "code": "03", "label": "SECURITY", "subtitle": "ACCESS"},
        {"index": 3, "code": "04", "label": "AUDIO", "subtitle": "MUSIC"}
    ]

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

    function tabLabel(index) {
        for (let i = 0; i < settingsTabs.length; ++i) {
            if (settingsTabs[i].index === index) {
                return settingsTabs[i].label
            }
        }
        return "SETTINGS"
    }

    function tabSubtitle(index) {
        for (let i = 0; i < settingsTabs.length; ++i) {
            if (settingsTabs[i].index === index) {
                return settingsTabs[i].subtitle
            }
        }
        return "CONTROL"
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

    component SettingsTabButton: Rectangle {
        id: tabButton
        property int tabIndex: 0
        property string code: ""
        property string label: ""
        property string subtitle: ""
        property string badge: ""
        signal clicked()

        Layout.fillWidth: true
        Layout.preferredHeight: 54
        color: root.activeTab === tabIndex ? "#2a1818" : (tabMouse.containsMouse ? Theme.steel : "transparent")
        border.width: 1
        border.color: root.activeTab === tabIndex ? Theme.gold : (tabMouse.containsMouse ? Theme.goldDim : "#33250d")

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: root.activeTab === tabButton.tabIndex ? Theme.crimsonHot : "transparent"
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: tabButton.code
            color: root.activeTab === tabButton.tabIndex ? Theme.gold : Theme.goldDim
            font.family: root.headerFont
            font.pixelSize: 14
            font.letterSpacing: 0
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 54
            anchors.right: badgeBox.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: tabButton.label
                color: root.activeTab === tabButton.tabIndex ? Theme.textPrimary : Theme.textDim
                elide: Text.ElideRight
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            Text {
                width: parent.width
                text: tabButton.subtitle
                color: root.activeTab === tabButton.tabIndex ? Theme.goldDim : Theme.textGhost
                elide: Text.ElideRight
                font.family: root.bodyFont
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        Rectangle {
            id: badgeBox
            visible: tabButton.badge.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? Math.max(28, badgeText.implicitWidth + 14) : 0
            height: 22
            color: Theme.voidColor
            border.width: 1
            border.color: root.activeTab === tabButton.tabIndex ? Theme.gold : Theme.goldDim

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: tabButton.badge
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tabButton.clicked()
        }
    }

    component SettingsToggleRow: Rectangle {
        id: toggleRow
        property string label: ""
        property string detail: ""
        property bool checked: false
        signal toggled()

        Layout.fillWidth: true
        Layout.preferredHeight: 58
        color: checked ? "#1f1a12" : Theme.voidColor
        border.width: 1
        border.color: checked ? Theme.gold : Theme.goldDim

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: switchBox.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                width: parent.width
                text: toggleRow.label
                color: toggleRow.checked ? Theme.textPrimary : Theme.textDim
                elide: Text.ElideRight
                font.family: root.headerFont
                font.pixelSize: 12
                font.letterSpacing: 0
            }

            Text {
                width: parent.width
                text: toggleRow.detail
                color: Theme.textGhost
                elide: Text.ElideRight
                font.family: root.bodyFont
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        Rectangle {
            id: switchBox
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            height: 26
            color: toggleRow.checked ? Theme.crimsonHot : Theme.steel
            border.width: 1
            border.color: toggleRow.checked ? Theme.gold : Theme.goldDim

            Text {
                anchors.centerIn: parent
                text: toggleRow.checked ? "ON" : "OFF"
                color: Theme.gold
                font.family: root.headerFont
                font.pixelSize: 11
                font.letterSpacing: 0
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toggleRow.toggled()
        }
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
                text: root.adminUnlocked ? "◈ SETTINGS" : "◈ SETTINGS AUTHORIZATION"
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

        RowLayout {
            id: adminPanel
            visible: root.adminUnlocked
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: modalHeader.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 18
            spacing: 14

            Rectangle {
                id: settingsRail
                Layout.preferredWidth: 248
                Layout.fillHeight: true
                color: Theme.voidPanel
                border.width: 1
                border.color: Theme.goldDim

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 68
                        color: "#17100f"
                        border.width: 1
                        border.color: Theme.crimson

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Text {
                                width: parent.width
                                text: "MISSION CONTROL"
                                color: Theme.gold
                                elide: Text.ElideRight
                                font.family: root.headerFont
                                font.pixelSize: 14
                                font.letterSpacing: 0
                            }

                            Text {
                                width: parent.width
                                text: "FOCUSOS SETTINGS"
                                color: Theme.textDim
                                elide: Text.ElideRight
                                font.family: root.bodyFont
                                font.pixelSize: 11
                                font.letterSpacing: 0
                            }
                        }
                    }

                    Repeater {
                        model: root.settingsTabs
                        delegate: SettingsTabButton {
                            required property int index
                            required property var modelData

                            tabIndex: modelData.index
                            code: modelData.code
                            label: modelData.label
                            subtitle: modelData.subtitle
                            badge: modelData.index === 0 ? String(root.routineDrafts.length)
                                  : modelData.index === 1 ? String(routineManager.alwaysAllowedApps.length)
                                  : modelData.index === 3 ? String(musicEngine.musicFiles.length)
                                  : ""
                            onClicked: root.activeTab = tabIndex
                        }
                    }

                    Item { Layout.fillHeight: true }

                    SettingsToggleRow {
                        label: "EDIT CONTROLS"
                        detail: "SHOW ROUTINE PENCILS"
                        checked: routineManager.editMode
                        onToggled: routineManager.editMode = !routineManager.editMode
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        color: Theme.iron
                        border.width: 1
                        border.color: Theme.goldDim

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5

                            Text {
                                width: parent.width
                                text: "STRICT LOCK"
                                color: Theme.gold
                                elide: Text.ElideRight
                                font.family: root.headerFont
                                font.pixelSize: 11
                                font.letterSpacing: 0
                            }

                            Text {
                                width: parent.width
                                text: root.deviceInfoText
                                color: Theme.textDim
                                elide: Text.ElideRight
                                font.family: root.bodyFont
                                font.pixelSize: 10
                                font.letterSpacing: 0
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: settingsBay
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.ironPanel
                border.width: 1
                border.color: Theme.goldDim

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        Layout.maximumHeight: 42
                        spacing: 12

                        Column {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                width: parent.width
                                text: root.tabLabel(root.activeTab)
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                font.family: root.headerFont
                                font.pixelSize: 17
                                font.letterSpacing: 0
                            }

                            Text {
                                width: parent.width
                                text: root.tabSubtitle(root.activeTab)
                                color: Theme.goldDim
                                elide: Text.ElideRight
                                font.family: root.bodyFont
                                font.pixelSize: 11
                                font.letterSpacing: 0
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 28
                            color: "#1f0f10"
                            border.width: 1
                            border.color: Theme.crimsonHot

                            Text {
                                anchors.centerIn: parent
                                text: "MAXIMUM STRICT"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 11
                                font.letterSpacing: 0
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
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
                                                    placeholderText: "/usr/bin/code /path/to/project  ·  kiosk:https://youtu.be/…  ·  *.desktop"
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
                        Layout.maximumHeight: 38
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 154
                            Layout.preferredHeight: 34
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
                            Layout.preferredHeight: 34
                            label: "▣ SAVE ALL"
                            onClicked: root.saveRoutines()
                        }
                    }
                }

                // ─── ALWAYS ALLOWED tab ────────────────────────────────
                // Apps the user wants reachable from every routine — word
                // processor, calendar, contacts. These come up alongside
                // each routine, are exempt from the lockdown watchdog,
                // and survive routine end without being terminated.
                ColumnLayout {
                    visible: root.activeTab === 1
                    anchors.fill: parent
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: "ALWAYS-ALLOWED APPLICATIONS"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        Layout.maximumHeight: 26
                        spacing: 10

                        Text {
                            text: "WATCHDOG EXEMPTIONS"
                            color: Theme.textDim
                            font.family: root.headerFont
                            font.pixelSize: 11
                            font.letterSpacing: 0
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "LAUNCH ON FIRST ENGAGE"
                            color: Theme.goldDim
                            font.family: root.bodyFont
                            font.pixelSize: 10
                            font.letterSpacing: 0
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            contentWidth: width
                            contentHeight: alwaysAllowedColumn.implicitHeight

                            Column {
                                id: alwaysAllowedColumn
                                width: parent.width
                                spacing: 8

                                Text {
                                    visible: routineManager.alwaysAllowedApps.length === 0
                                    width: parent.width
                                    text: "NO ALWAYS-ALLOWED APPS YET"
                                    color: Theme.textGhost
                                    font.family: root.bodyFont
                                    font.pixelSize: 12
                                    font.letterSpacing: 0
                                }

                                Repeater {
                                    model: routineManager.alwaysAllowedApps
                                    delegate: RowLayout {
                                        required property int index
                                        required property string modelData
                                        width: alwaysAllowedColumn.width
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData
                                            color: Theme.textPrimary
                                            elide: Text.ElideRight
                                            font.family: root.bodyFont
                                            font.pixelSize: 12
                                            font.letterSpacing: 0
                                        }

                                        AdminButton {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 30
                                            label: "✕"
                                            onClicked: routineManager.removeAlwaysAllowedApp(index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        Layout.maximumHeight: 38
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 34
                            label: "+ ADD ALWAYS-ALLOWED APP"
                            danger: false
                            onClicked: {
                                const path = routineManager.pickApplication()
                                if (path && path.length > 0) {
                                    routineManager.addAlwaysAllowedApp(path)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // ─── ACCESS tab (formerly "Other Access Settings") ──────
                ColumnLayout {
                    visible: root.activeTab === 2
                    anchors.fill: parent
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "ACCESS SESSION DURATION:"
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

                // ─── MUSIC tab ─────────────────────────────────────────
                ColumnLayout {
                    visible: root.activeTab === 3
                    anchors.fill: parent
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        Layout.maximumHeight: 24
                        spacing: 10

                        Text {
                            text: "LOCAL AUDIO LIBRARY"
                            color: Theme.goldDim
                            font.family: root.headerFont
                            font.pixelSize: 13
                            font.letterSpacing: 0
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: musicEngine.importStatus.length > 0
                            text: musicEngine.importStatus
                            color: musicEngine.importStatus.indexOf("FAILED") >= 0 ? Theme.crimsonHot : Theme.gold
                            elide: Text.ElideRight
                            font.family: root.bodyFont
                            font.pixelSize: 11
                            font.letterSpacing: 0
                        }
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
                        Layout.maximumHeight: 38
                        spacing: 12

	                        // In-process file picker — the old "Open Music
	                        // Folder" button opened a file manager behind the
	                        // always-on-top FocusOS shell, so the user never
	                        // saw it. The picker renders modally and works.
	                        AdminButton {
	                            Layout.preferredWidth: 174
	                            Layout.preferredHeight: 34
	                            label: "+ ADD AUDIO FILE"
	                            danger: false
	                            onClicked: musicEngine.importMusicFile()
	                        }

                        AdminButton {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 34
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
                            Layout.preferredHeight: 34
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
