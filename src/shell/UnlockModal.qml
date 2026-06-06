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
    // Working copy of ~/.focusos/startup.sh, loaded when the admin pane unlocks
    // and edited in the SYSTEM tab.
    property string startupScriptDraft: ""
    property string startupSaveStatus: ""
    // Live tail of ~/.focusos/logs/focusos.log, shown in the SYSTEM tab so the
    // user can see what FocusOS has been doing without a terminal.
    property string diagnosticsTail: ""
    property string elevatedLaunchPassword: ""
    property string elevatedLaunchStatus: ""
    // Dry-run result of the strict engage-time app sweep (H3): the apps that a
    // routine would close. Populated by the PREVIEW button in the APPEARANCE tab.
    property var appQuitPreview: []
    property bool appQuitPreviewRun: false
    property var settingsTabs: [
        {"index": 0, "code": "01", "label": "MISSION PLAN", "subtitle": "ROUTINES"},
        {"index": 1, "code": "02", "label": "ALLOWED APPS", "subtitle": "GLOBAL"},
        {"index": 2, "code": "03", "label": "SECURITY", "subtitle": "ACCESS"},
        {"index": 3, "code": "04", "label": "AUDIO", "subtitle": "MUSIC"},
        {"index": 4, "code": "05", "label": "APPEARANCE", "subtitle": "DISPLAY"},
        {"index": 5, "code": "06", "label": "SYSTEM", "subtitle": "UPDATE + RECOVERY"}
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
            "access_folder": String(routine.access_folder || ""),
            "access_desktop": Boolean(routine.access_desktop),
            "access_documents": Boolean(routine.access_documents),
            "access_downloads": Boolean(routine.access_downloads),
            "browsable": Boolean(routine.browsable),
            "time_limit_minutes": Math.max(1, Number(routine.time_limit_minutes || 60)),
            "min_time_minutes": Math.max(0, Number(routine.min_time_minutes || 0)),
            "network_lock": routine.network_lock === undefined ? true : Boolean(routine.network_lock),
            "full_access": Boolean(routine.full_access),
            "break_frequency_minutes": Math.max(0, Number(routine.break_frequency_minutes || 0)),
            "break_duration_minutes": Math.max(0, Number(routine.break_duration_minutes || 0)),
            "keep_display_awake": routine.keep_display_awake === undefined ? true : Boolean(routine.keep_display_awake),
            "music_behavior": String(routine.music_behavior || "low")
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
            loadSystemSettings()
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

    function loadSystemSettings() {
        startupScriptDraft = systemStatus.readStartupScript()
        startupSaveStatus = ""
        elevatedLaunchPassword = ""
        elevatedLaunchStatus = ""
        systemStatus.refreshElevatedLaunch()
        refreshDiagnostics()
    }

    function refreshDiagnostics() {
        // diagnostics is a context property set in main(); guard defensively so a
        // build wired without it degrades to an empty panel instead of erroring.
        diagnosticsTail = (typeof diagnostics !== "undefined" && diagnostics)
                          ? diagnostics.tail(240)
                          : ""
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
            "access_folder": "",
            "access_desktop": false,
            "access_documents": false,
            "access_downloads": false,
            "browsable": false,
            "time_limit_minutes": 60,
            "min_time_minutes": 0,
            "network_lock": true,
            "full_access": false,
            "break_frequency_minutes": 0,
            "break_duration_minutes": 0,
            "keep_display_awake": true,
            "music_behavior": "low"
        })
        routineDrafts = drafts
    }

    function removeRoutine(routineIndex) {
        const drafts = cloneDrafts()
        drafts.splice(routineIndex, 1)
        routineDrafts = drafts
    }

    // Reorder a routine within the list. The persisted order is what drives the
    // launcher and the saved config, so moving a card up/down here and pressing
    // SAVE ALL changes the order everywhere.
    function moveRoutine(fromIndex, toIndex) {
        if (toIndex < 0 || toIndex >= routineDrafts.length || fromIndex === toIndex) {
            return
        }
        const drafts = cloneDrafts()
        const moved = drafts.splice(fromIndex, 1)[0]
        drafts.splice(toIndex, 0, moved)
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

    // Set/clear a routine's optional access folder. Reassigns routineDrafts so the
    // editor's displayed path refreshes after the folder picker returns.
    function setRoutineFolder(routineIndex, path) {
        if (routineIndex < 0 || routineIndex >= routineDrafts.length) {
            return
        }
        const drafts = cloneDrafts()
        drafts[routineIndex].access_folder = String(path || "")
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
        height: root.adminUnlocked ? Math.min(760, parent.height - 72) : (totpEngine.firstLaunch ? Math.min(680, parent.height - 48) : (totpEngine.secretMissing ? Math.min(460, parent.height - 48) : 322))
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

            // SLEEP DISPLAY — left of the "SETTINGS AUTHORIZATION" title, shown
            // on the pre-unlock (locked) panel. Turns the monitor off without
            // engaging the in-app lock; the next keypress / mouse move wakes it.
            Rectangle {
                visible: !root.adminUnlocked
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(120, sleepDisplayLabel.implicitWidth + 22)
                height: 30
                // Deliberately NOT crimson: red + this corner reads as the close
                // button (the ✕ on the right is crimson), but SLEEP is a benign
                // "turn the panel off" — a misclick just blanks the monitor. Use
                // the calm steel/gold utility style so it can't be mistaken for it.
                color: sleepDisplayMouse.containsMouse ? Theme.steel : "transparent"
                border.width: 1
                border.color: sleepDisplayMouse.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    id: sleepDisplayLabel
                    anchors.centerIn: parent
                    text: "☾ SLEEP"
                    color: Theme.gold
                    font.family: root.headerFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                MouseArea {
                    id: sleepDisplayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.sleepDisplay()
                }
            }

            // LOCK SCREEN (Task 6) — top-left of the Settings title bar, only
            // after the code unlock. Blanks the display; any input restores it.
            Rectangle {
                visible: root.adminUnlocked
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(120, lockScreenLabel.implicitWidth + 22)
                height: 30
                color: lockScreenMouse.containsMouse ? Theme.crimsonHot : Theme.crimson
                border.width: 1
                border.color: lockScreenMouse.containsMouse ? Theme.gold : Theme.goldDim

                Text {
                    id: lockScreenLabel
                    anchors.centerIn: parent
                    text: "⏻ LOCK SCREEN"
                    color: Theme.gold
                    font.family: root.headerFont
                    font.pixelSize: 12
                    font.letterSpacing: 0
                }

                MouseArea {
                    id: lockScreenMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: routineManager.lockScreen()
                }
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

            // ── Recovery: enrolled but the secret file is missing/corrupt ──
            // Without this the normal code field below would reject every code
            // (there's no secret to check against) and the user would be locked
            // out. Let them paste the secret they saved (password manager /
            // printed QR) plus a current code to re-adopt it.
            ColumnLayout {
                visible: totpEngine.secretMissing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: "══ ■ SECRET RECOVERY ■ ══"
                    color: Theme.goldDim
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.headerFont
                    font.pixelSize: 13
                    font.letterSpacing: 0
                }

                Text {
                    Layout.fillWidth: true
                    text: "THE STORED 2FA SECRET IS MISSING OR CORRUPT. PASTE THE SETUP KEY YOU SAVED, THEN ENTER A CURRENT 6-DIGIT CODE TO RESTORE ACCESS."
                    color: Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.family: root.bodyFont
                    font.pixelSize: 11
                    font.letterSpacing: 0
                }

                TextField {
                    id: recoverySecretField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: Theme.textPrimary
                    selectedTextColor: Theme.voidColor
                    selectionColor: Theme.gold
                    placeholderText: "SETUP KEY (BASE32)"
                    placeholderTextColor: Theme.textGhost
                    font.family: root.bodyFont
                    font.pixelSize: 14
                    font.letterSpacing: 0
                    background: Rectangle {
                        color: Theme.steel
                        border.width: 1
                        border.color: recoverySecretField.activeFocus ? Theme.gold : Theme.goldDim
                    }
                    onTextChanged: root.errorText = ""
                }

                TextField {
                    id: recoveryCodeField
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
                        border.color: root.errorText.length > 0 ? Theme.crimsonHot : (recoveryCodeField.activeFocus ? Theme.gold : Theme.goldDim)
                    }
                    onTextChanged: {
                        root.errorText = ""
                        if (text.length === 6) {
                            if (totpEngine.restoreSecret(recoverySecretField.text, text)) {
                                recoverySecretField.text = ""
                                text = ""
                                routineManager.unlockOtherAccess()
                                root.adminUnlocked = true
                                root.loadRoutineDrafts()
                                root.loadSystemSettings()
                                root.activeTab = 0
                            } else {
                                root.errorText = "RECOVERY FAILED — CHECK KEY AND CODE"
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

            ColumnLayout {
                visible: !totpEngine.secretMissing
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
                                root.loadSystemSettings()
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
                                                text: (routineCard.index + 1) + "."
                                                color: Theme.gold
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

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

                                            // Reorder controls. Disabled at the ends of the list.
                                            AdminButton {
                                                Layout.preferredWidth: 40
                                                Layout.preferredHeight: 34
                                                label: "▲"
                                                danger: false
                                                actionEnabled: routineCard.index > 0
                                                onClicked: root.moveRoutine(routineCard.index, routineCard.index - 1)
                                            }

                                            AdminButton {
                                                Layout.preferredWidth: 40
                                                Layout.preferredHeight: 34
                                                label: "▼"
                                                danger: false
                                                actionEnabled: routineCard.index < root.routineDrafts.length - 1
                                                onClicked: root.moveRoutine(routineCard.index, routineCard.index + 1)
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
                                                    placeholderText: "/usr/bin/code /path/to/project  ·  flatpak run md.obsidian.Obsidian  ·  ~/Applications/App.AppImage  ·  kiosk:https://youtu.be/…  ·  *.desktop"
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

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

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

                                            // Open-file workflow: pin any file (PDF, ebook, image,
                                            // office doc, video…) that opens in its default app when
                                            // the routine engages — no folder navigation needed.
                                            AdminButton {
                                                Layout.preferredWidth: 164
                                                Layout.preferredHeight: 34
                                                label: "+ OPEN FILE"
                                                danger: false
                                                onClicked: {
                                                    const path = routineManager.pickFile()
                                                    root.addApp(routineCard.index, path)
                                                }
                                            }

                                            Item { Layout.fillWidth: true }
                                        }

                                        // Mid-session file access. Off by default: the routine offers
                                        // only the standard native "open file" picker. Turn BROWSE MENU
                                        // on to also expose the in-app folder browser ("browse computer")
                                        // in the bottom bar, jailed to the folders chosen below + the
                                        // optional access folder.
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: "BROWSE MENU"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            Rectangle {
                                                id: browseToggle
                                                property bool chosen: Boolean(routineCard.modelData.browsable)
                                                Layout.preferredWidth: 124
                                                Layout.preferredHeight: 34
                                                color: chosen ? "#1f1a12" : Theme.voidColor
                                                border.width: 1
                                                border.color: chosen ? Theme.gold : Theme.goldDim

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: browseToggle.chosen ? "● ON" : "OFF"
                                                    color: browseToggle.chosen ? Theme.gold : Theme.textDim
                                                    font.family: root.headerFont
                                                    font.pixelSize: 10
                                                    font.letterSpacing: 0
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.updateRoutineField(routineCard.index, "browsable", !browseToggle.chosen)
                                                }
                                            }

                                            Item { Layout.fillWidth: true }
                                        }

                                        // Which standard folders the in-app browser may reach (only
                                        // relevant when BROWSE MENU is on). The access folder below is
                                        // always reachable when set.
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: "↳ FOLDERS"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            Repeater {
                                                model: [
                                                    { key: "access_desktop", label: "DESKTOP" },
                                                    { key: "access_documents", label: "DOCUMENTS" },
                                                    { key: "access_downloads", label: "DOWNLOADS" }
                                                ]

                                                delegate: Rectangle {
                                                    id: folderChip
                                                    required property var modelData
                                                    property bool chosen: Boolean(routineCard.modelData[modelData.key])
                                                    Layout.preferredWidth: 124
                                                    Layout.preferredHeight: 34
                                                    color: chosen ? "#1f1a12" : Theme.voidColor
                                                    border.width: 1
                                                    border.color: chosen ? Theme.gold : Theme.goldDim

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: (folderChip.chosen ? "● " : "") + folderChip.modelData.label
                                                        color: folderChip.chosen ? Theme.gold : Theme.textDim
                                                        elide: Text.ElideRight
                                                        font.family: root.headerFont
                                                        font.pixelSize: 10
                                                        font.letterSpacing: 0
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.updateRoutineField(routineCard.index, folderChip.modelData.key, !folderChip.chosen)
                                                    }
                                                }
                                            }

                                            Item { Layout.fillWidth: true }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: "ACCESS FOLDER"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                elide: Text.ElideMiddle
                                                text: routineCard.modelData.access_folder && routineCard.modelData.access_folder.length > 0
                                                      ? routineCard.modelData.access_folder
                                                      : "Optional extra folder — always browsable when set"
                                                color: routineCard.modelData.access_folder && routineCard.modelData.access_folder.length > 0
                                                       ? Theme.textPrimary : Theme.textDim
                                                font.family: root.bodyFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminButton {
                                                Layout.preferredWidth: 100
                                                Layout.preferredHeight: 34
                                                label: "BROWSE…"
                                                danger: false
                                                onClicked: {
                                                    const path = routineManager.pickFolder()
                                                    if (path && path.length > 0) {
                                                        root.setRoutineFolder(routineCard.index, path)
                                                    }
                                                }
                                            }

                                            AdminButton {
                                                Layout.preferredWidth: 40
                                                Layout.preferredHeight: 34
                                                label: "✕"
                                                actionEnabled: routineCard.modelData.access_folder && routineCard.modelData.access_folder.length > 0
                                                onClicked: root.setRoutineFolder(routineCard.index, "")
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
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 14

                                            Text {
                                                text: "BREAK EVERY"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminSpinner {
                                                value: Number(routineCard.modelData.break_frequency_minutes || 0)
                                                from: 0
                                                to: 240
                                                onValueModified: function(nextValue) {
                                                    root.updateRoutineField(routineCard.index, "break_frequency_minutes", nextValue)
                                                }
                                            }

                                            Text {
                                                text: "REST WINDOW"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            AdminSpinner {
                                                value: Number(routineCard.modelData.break_duration_minutes || 0)
                                                from: 0
                                                to: 60
                                                onValueModified: function(nextValue) {
                                                    root.updateRoutineField(routineCard.index, "break_duration_minutes", nextValue)
                                                }
                                            }
                                            Item { Layout.fillWidth: true }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 14

                                            Text {
                                                text: "MUSIC"
                                                color: Theme.goldDim
                                                font.family: root.headerFont
                                                font.pixelSize: 12
                                                font.letterSpacing: 0
                                            }

                                            ComboBox {
                                                id: routineMusicBox
                                                Layout.preferredWidth: 230
                                                Layout.preferredHeight: 34
                                                model: ["Stop music", "Continue at low volume", "Continue at same volume"]
                                                currentIndex: root.behaviorIndex(String(routineCard.modelData.music_behavior || "low"))
                                                font.family: root.bodyFont
                                                font.pixelSize: 12

                                                onActivated: function(index) {
                                                    root.updateRoutineField(routineCard.index, "music_behavior", root.behaviorValue(index))
                                                }

                                                contentItem: Text {
                                                    leftPadding: 10
                                                    rightPadding: 28
                                                    text: routineMusicBox.displayText
                                                    color: Theme.textPrimary
                                                    verticalAlignment: Text.AlignVCenter
                                                    elide: Text.ElideRight
                                                    font.family: root.bodyFont
                                                    font.pixelSize: 12
                                                    font.letterSpacing: 0
                                                }

                                                indicator: Text {
                                                    x: routineMusicBox.width - width - 10
                                                    y: (routineMusicBox.height - height) / 2
                                                    text: "▾"
                                                    color: Theme.gold
                                                    font.family: root.headerFont
                                                    font.pixelSize: 14
                                                    font.letterSpacing: 0
                                                }

                                                background: Rectangle {
                                                    color: Theme.steel
                                                    border.width: 1
                                                    border.color: routineMusicBox.activeFocus ? Theme.gold : Theme.goldDim
                                                }

                                                delegate: ItemDelegate {
                                                    width: routineMusicBox.width
                                                    height: 34
                                                    contentItem: Text {
                                                        text: modelData
                                                        color: Theme.textPrimary
                                                        verticalAlignment: Text.AlignVCenter
                                                        leftPadding: 10
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
                                                    y: routineMusicBox.height
                                                    width: routineMusicBox.width
                                                    implicitHeight: contentItem.implicitHeight
                                                    padding: 0
                                                    contentItem: ListView {
                                                        clip: true
                                                        implicitHeight: contentHeight
                                                        model: routineMusicBox.popup.visible ? routineMusicBox.delegateModel : null
                                                        currentIndex: routineMusicBox.highlightedIndex
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

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Rectangle {
                                                Layout.preferredWidth: 166
                                                Layout.preferredHeight: 34
                                                color: Boolean(routineCard.modelData.keep_display_awake) ? "#1f1a12" : Theme.voidColor
                                                border.width: 1
                                                border.color: Boolean(routineCard.modelData.keep_display_awake) ? Theme.gold : Theme.goldDim

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: Boolean(routineCard.modelData.keep_display_awake) ? "DISPLAY AWAKE" : "DISPLAY MAY SLEEP"
                                                    color: Boolean(routineCard.modelData.keep_display_awake) ? Theme.gold : Theme.textDim
                                                    elide: Text.ElideRight
                                                    font.family: root.headerFont
                                                    font.pixelSize: 10
                                                    font.letterSpacing: 0
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        const drafts = root.cloneDrafts()
                                                        drafts[routineCard.index].keep_display_awake = !Boolean(routineCard.modelData.keep_display_awake)
                                                        root.routineDrafts = drafts
                                                    }
                                                }
                                            }

                                            // FULL INTERNET ACCESS (Task 4): researcher mode. When on,
                                            // the routine runs with no outbound allowlist — and engaging
                                            // it always demands a 6-digit code first (enforced in Main's
                                            // engage-prep overlay).
                                            Rectangle {
                                                Layout.preferredWidth: 196
                                                Layout.preferredHeight: 34
                                                color: Boolean(routineCard.modelData.full_access) ? "#2a1010" : Theme.voidColor
                                                border.width: 1
                                                border.color: Boolean(routineCard.modelData.full_access) ? Theme.crimsonHot : Theme.goldDim

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: Boolean(routineCard.modelData.full_access) ? "● FULL INTERNET (CODE)" : "FULL INTERNET ACCESS"
                                                    color: Boolean(routineCard.modelData.full_access) ? Theme.crimsonHot : Theme.textDim
                                                    elide: Text.ElideRight
                                                    font.family: root.headerFont
                                                    font.pixelSize: 10
                                                    font.letterSpacing: 0
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        const drafts = root.cloneDrafts()
                                                        drafts[routineCard.index].full_access = !Boolean(routineCard.modelData.full_access)
                                                        root.routineDrafts = drafts
                                                    }
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

                // ─── ACCESS tab ─────────────────────────────────────────
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

                        // Per-routine now: each routine carries its own engage
                        // behavior (Stop / Low / Same), set in the ROUTINES tab.
                        Text {
                            text: "ENGAGE BEHAVIOUR IS SET PER ROUTINE →"
                            color: Theme.textDim
                            font.family: root.headerFont
                            font.pixelSize: 11
                            font.letterSpacing: 0
                        }

	                        Item { Layout.fillWidth: true }
	                    }
	                }

                // ─── APPEARANCE tab ────────────────────────────────────
                ColumnLayout {
                    visible: root.activeTab === 4
                    anchors.fill: parent
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: "DISPLAY OVERLAYS"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    SettingsToggleRow {
                        label: "GLOBAL PROGRESS OVERLAY"
                        detail: "Always-on-top countdown border, visible across every space during a routine"
                        checked: routineManager.overlayProgressEnabled
                        onToggled: routineManager.overlayProgressEnabled = !routineManager.overlayProgressEnabled
                    }

                    SettingsToggleRow {
                        label: "DEEP SLEEP SUSPENDS THE COMPUTER"
                        detail: "After the screensaver, also suspend to RAM. Leave OFF if your machine can't wake from sleep (common on Mac hardware) — the display turns off either way."
                        checked: routineManager.deepSleepSuspend
                        onToggled: routineManager.deepSleepSuspend = !routineManager.deepSleepSuspend
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                    }

                    // Daily focus target — moved here from the main InfoPanel so
                    // configuration stays gated behind the TOTP unlock. Setter
                    // persists + clamps 0..24h on the StatsStore side.
                    Text {
                        Layout.fillWidth: true
                        text: "DAILY FOCUS TARGET"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        AdminSpinner {
                            value: statsStore.dailyTargetMinutes
                            from: 0
                            to: 24 * 60
                            stepSize: 15
                            onValueModified: function(nextValue) {
                                statsStore.dailyTargetMinutes = nextValue
                            }
                        }

                        Text {
                            text: statsStore.dailyTargetMinutes > 0
                                  ? "minutes  ■  " + (Math.floor(statsStore.dailyTargetMinutes / 60)) + "H " + (statsStore.dailyTargetMinutes % 60) + "M / DAY"
                                  : "minutes  ■  NO DAILY GOAL SET"
                            color: Theme.textDim
                            font.family: root.bodyFont
                            font.pixelSize: 12
                            font.letterSpacing: 0
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                    }

                    // ── Strict app-sweep preview (H3) ──
                    // Engaging a routine SIGTERMs your other GUI apps. This dry
                    // run shows exactly which ones would close on THIS machine,
                    // so you can spot anything important and add it to ALLOWED
                    // APPS first — without actually killing anything.
                    Text {
                        Layout.fillWidth: true
                        text: "STRICT APP CLEANUP — DRY RUN"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 220
                            Layout.preferredHeight: 36
                            color: previewMouse.containsMouse ? Theme.steel : Theme.iron
                            border.width: 1
                            border.color: previewMouse.containsMouse ? Theme.gold : Theme.goldDim

                            Text {
                                anchors.centerIn: parent
                                text: "▷ PREVIEW WHAT WOULD CLOSE"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 12
                                font.letterSpacing: 0
                            }

                            MouseArea {
                                id: previewMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.appQuitPreview = routineManager.previewBackgroundAppQuit()
                                    root.appQuitPreviewRun = true
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        visible: root.appQuitPreviewRun
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        Text {
                            anchors.fill: parent
                            anchors.margins: 10
                            text: root.appQuitPreview.length > 0
                                  ? ("WOULD CLOSE:  " + root.appQuitPreview.join("   ■   "))
                                  : "NOTHING WOULD CLOSE — no non-allowed GUI apps are running right now."
                            color: root.appQuitPreview.length > 0 ? Theme.textPrimary : Theme.textDim
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignTop
                            font.family: root.bodyFont
                            font.pixelSize: 12
                            font.letterSpacing: 0
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ─── SYSTEM tab ────────────────────────────────────────
                // The SYSTEM tab stacks many sections (update, probation, build
                // log, session recovery, account, startup script) — more than fits
                // the modal height. Wrap it in a Flickable so the lower sections,
                // including the startup-script editor and its SAVE button, can be
                // scrolled into view.
                Flickable {
                    id: systemTabFlick
                    visible: root.activeTab === 5
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: systemTab.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: systemTab
                    width: systemTabFlick.width
                    spacing: 12

                    function formatProbation(seconds) {
                        const s = Math.max(0, Math.round(seconds))
                        const m = Math.floor(s / 60)
                        const r = s % 60
                        return (m < 10 ? "0" + m : m) + ":" + (r < 10 ? "0" + r : r)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "SOFTWARE UPDATE"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !updater.supported
                        text: "Auto-update is only available on the permanent Linux install."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 11
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: updater.supported && updater.status.length > 0
                        text: updater.status
                        color: Theme.textDim
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 11
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: updater.supported
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 168
                            Layout.preferredHeight: 34
                            label: updater.busy ? "WORKING…" : "↻ PULL + REBUILD"
                            danger: false
                            actionEnabled: !updater.busy && !updater.updatePending
                            onClicked: updater.runUpdate()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Probation controls (30-minute revert window)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: probationCol.implicitHeight + 24
                        visible: updater.supported && updater.updatePending
                        color: "#1f1a12"
                        border.width: 1
                        border.color: Theme.gold

                        ColumnLayout {
                            id: probationCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: "PROBATION — " + systemTab.formatProbation(updater.probationRemainingSeconds) + " LEFT"
                                color: Theme.gold
                                font.family: root.headerFont
                                font.pixelSize: 12
                                font.letterSpacing: 0
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "This build auto-commits if it stays healthy. A crash loop reverts automatically."
                                color: Theme.textGhost
                                wrapMode: Text.WordWrap
                                font.family: root.bodyFont
                                font.pixelSize: 10
                                font.letterSpacing: 0
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                AdminButton {
                                    Layout.preferredWidth: 150
                                    Layout.preferredHeight: 34
                                    label: "✓ KEEP THIS BUILD"
                                    danger: false
                                    actionEnabled: !updater.busy
                                    onClicked: updater.confirmHealthy()
                                }

                                AdminButton {
                                    Layout.preferredWidth: 130
                                    Layout.preferredHeight: 34
                                    label: "↩ REVERT"
                                    danger: true
                                    actionEnabled: !updater.busy && updater.revertAvailable
                                    onClicked: updater.runRevert()
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // Build log
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        visible: updater.supported && updater.log.length > 0
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        Flickable {
                            id: logFlick
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            contentWidth: width
                            contentHeight: logText.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds

                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

                            Text {
                                id: logText
                                width: logFlick.width
                                text: updater.log
                                color: Theme.textDim
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                font.family: root.bodyFont
                                font.pixelSize: 10
                                font.letterSpacing: 0
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                        visible: systemStatus.elevatedLaunchSupported
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: systemStatus.elevatedLaunchSupported
                        text: "PASSWORDLESS FIREWALL"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: systemStatus.elevatedLaunchSupported
                        text: systemStatus.elevatedLaunchEnabled
                              ? "ENABLED - THE NETWORK LOCK INSTALLS WITHOUT A PASSWORD"
                              : "DISABLED - ROUTINES THAT USE A NETWORK LOCK WILL FAIL TO ENGAGE"
                        color: systemStatus.elevatedLaunchEnabled ? Theme.gold : Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: systemStatus.elevatedLaunchSupported
                        text: "GRANTS: sudo /sbin/pfctl (firewall only — FocusOS keeps running as you)"
                        color: Theme.textDim
                        wrapMode: Text.WrapAnywhere
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: systemStatus.elevatedLaunchSupported
                        spacing: 10

                        AdminTextField {
                            id: elevatedLaunchPasswordField
                            Layout.preferredWidth: 210
                            Layout.preferredHeight: 34
                            placeholderText: systemStatus.runningAsRoot ? "ALREADY ROOT" : "MAC ADMIN PASSWORD"
                            echoMode: TextInput.Password
                            enabled: !systemStatus.runningAsRoot
                            text: root.elevatedLaunchPassword
                            onTextChanged: {
                                if (text !== root.elevatedLaunchPassword) {
                                    root.elevatedLaunchPassword = text
                                    root.elevatedLaunchStatus = ""
                                }
                            }
                        }

                        AdminButton {
                            Layout.preferredWidth: 130
                            Layout.preferredHeight: 34
                            label: "ENABLE"
                            danger: false
                            actionEnabled: !systemStatus.elevatedLaunchEnabled
                                           && (systemStatus.runningAsRoot || root.elevatedLaunchPassword.length > 0)
                            onClicked: {
                                const error = systemStatus.enableElevatedLaunch(root.elevatedLaunchPassword)
                                systemStatus.refreshElevatedLaunch()
                                root.elevatedLaunchStatus = error.length === 0
                                    ? "ENABLED - NETWORK LOCK READY"
                                    : error.toUpperCase()
                                if (error.length === 0) {
                                    root.elevatedLaunchPassword = ""
                                    elevatedLaunchPasswordField.text = ""
                                }
                            }
                        }

                        AdminButton {
                            Layout.preferredWidth: 130
                            Layout.preferredHeight: 34
                            label: "DISABLE"
                            danger: true
                            actionEnabled: systemStatus.elevatedLaunchEnabled
                                           && (systemStatus.runningAsRoot || root.elevatedLaunchPassword.length > 0)
                            onClicked: {
                                const error = systemStatus.disableElevatedLaunch(root.elevatedLaunchPassword)
                                systemStatus.refreshElevatedLaunch()
                                root.elevatedLaunchStatus = error.length === 0
                                    ? "DISABLED"
                                    : error.toUpperCase()
                                if (error.length === 0) {
                                    root.elevatedLaunchPassword = ""
                                    elevatedLaunchPasswordField.text = ""
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.elevatedLaunchStatus
                            color: root.elevatedLaunchStatus.indexOf("FAILED") >= 0
                                   || root.elevatedLaunchStatus.indexOf("INCORRECT") >= 0
                                   ? Theme.crimsonHot : Theme.textDim
                            wrapMode: Text.WordWrap
                            font.family: root.bodyFont
                            font.pixelSize: 10
                            font.letterSpacing: 0
                        }
                    }

                    // ── Kiosk lock: launch-at-login + un-quittable ──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                        visible: routineManager.persistentKioskSupported
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.persistentKioskSupported
                        text: "KIOSK LOCK"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    SettingsToggleRow {
                        visible: routineManager.persistentKioskSupported
                        label: "LAUNCH AT LOGIN + UN-QUITTABLE"
                        detail: "FocusOS starts automatically every login and respawns if killed. It can then only be left with your 6-digit code (QUIT below). Changes take effect at the next login."
                        checked: routineManager.persistentKioskEnabled
                        onToggled: routineManager.setPersistentKiosk(!routineManager.persistentKioskEnabled)
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.persistentKioskSupported
                        text: "QUIT FOCUSOS — type QUIT to confirm. Drops the network lock and stops the respawn agent for this session. FocusOS returns at the next login while the lock stays enabled."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: routineManager.persistentKioskSupported
                        spacing: 10

                        AdminTextField {
                            id: quitConfirmField
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 34
                            placeholderText: "QUIT"
                        }

                        AdminButton {
                            Layout.preferredWidth: 160
                            Layout.preferredHeight: 34
                            label: "⏻ QUIT FOCUSOS"
                            danger: true
                            actionEnabled: quitConfirmField.text === "QUIT"
                            onClicked: routineManager.quitFocusOS()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                        visible: routineManager.sessionRecoverySupported()
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.sessionRecoverySupported()
                        text: "SESSION RECOVERY"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.sessionRecoverySupported()
                        text: "Restores the other login sessions (Plasma, etc.) so they appear at the SDDM screen again. Log out afterwards to switch."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: routineManager.sessionRecoverySupported()
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 210
                            Layout.preferredHeight: 34
                            label: "⏏ RESTORE OTHER SESSIONS"
                            danger: true
                            onClicked: routineManager.restoreLoginSessions()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                        visible: routineManager.signOutSupported()
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.signOutSupported()
                        text: "ACCOUNT"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.signOutSupported()
                        text: "Logs out of your account and returns to the login screen. The inspiration fade cycle restarts on your next login."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: routineManager.signOutSupported()
                        spacing: 10

                        AdminTextField {
                            id: signOutConfirmField
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 34
                            placeholderText: "SIGN OUT"
                        }

                        AdminButton {
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 34
                            label: "⏻ SIGN OUT"
                            danger: true
                            actionEnabled: signOutConfirmField.text === "SIGN OUT"
                            onClicked: {
                                // Returning to the login screen restarts the fade
                                // cycle (Task 1); persist a fresh start now so the
                                // next login comes up fully visible.
                                inspirationStore.resetFadeCycle()
                                routineManager.signOut()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: routineManager.powerControlSupported()
                        text: "Restarts or powers off the whole machine. The fade cycle restarts on your next login."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: routineManager.powerControlSupported()
                        spacing: 10

                        AdminTextField {
                            id: restartConfirmField
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 34
                            placeholderText: "RESTART"
                        }

                        AdminButton {
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 34
                            label: "↻ RESTART"
                            danger: true
                            actionEnabled: restartConfirmField.text === "RESTART"
                            onClicked: {
                                inspirationStore.resetFadeCycle()
                                routineManager.restartMachine()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: routineManager.powerControlSupported()
                        spacing: 10

                        AdminTextField {
                            id: shutdownConfirmField
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 34
                            placeholderText: "SHUT DOWN"
                        }

                        AdminButton {
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 34
                            label: "⏻ SHUT DOWN"
                            danger: true
                            actionEnabled: shutdownConfirmField.text === "SHUT DOWN"
                            onClicked: {
                                inspirationStore.resetFadeCycle()
                                routineManager.shutdownMachine()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "SESSION STARTUP"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Shell commands run once at login, after the shell comes up — list the few helpers a bare session needs (input remappers like Toshy, tray agents). FocusOS does NOT replay ~/.config/autostart entries — a stray one can drag in the whole Plasma desktop — so put exactly what you want here. Runs once per login, not on a respawn."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    AdminTextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 132
                        font.family: root.bodyFont
                        placeholderText: "#!/usr/bin/env bash\n# e.g.\n# toshy-config-start\n# nm-applet &"
                        text: root.startupScriptDraft
                        onTextChanged: {
                            if (text !== root.startupScriptDraft) {
                                root.startupScriptDraft = text
                                root.startupSaveStatus = ""
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 168
                            Layout.preferredHeight: 34
                            label: "💾 SAVE STARTUP SCRIPT"
                            danger: false
                            onClicked: {
                                const ok = systemStatus.writeStartupScript(root.startupScriptDraft)
                                root.startupSaveStatus = ok
                                    ? "SAVED — applies on next login"
                                    : "SAVE FAILED — could not write " + systemStatus.startupScriptPath()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.startupSaveStatus
                            color: Theme.textDim
                            wrapMode: Text.WordWrap
                            font.family: root.bodyFont
                            font.pixelSize: 10
                            font.letterSpacing: 0
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.goldDim
                        opacity: 0.7
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "DIAGNOSTICS"
                        color: Theme.goldDim
                        font.family: root.headerFont
                        font.pixelSize: 13
                        font.letterSpacing: 0
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Live tail of the FocusOS log — every routine engage, warning and error lands here. The full rotating log is at " +
                              ((typeof diagnostics !== "undefined" && diagnostics) ? diagnostics.logFilePath : "~/.focusos/logs/focusos.log") + "."
                        color: Theme.textGhost
                        wrapMode: Text.WordWrap
                        font.family: root.bodyFont
                        font.pixelSize: 10
                        font.letterSpacing: 0
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        color: Theme.voidColor
                        border.width: 1
                        border.color: Theme.goldDim

                        Flickable {
                            id: diagLogFlick
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            contentWidth: width
                            contentHeight: diagLogText.implicitHeight
                            // Stick to the newest lines as the tail refreshes.
                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

                            Text {
                                id: diagLogText
                                width: diagLogFlick.width
                                text: root.diagnosticsTail.length > 0
                                      ? root.diagnosticsTail
                                      : "(no log activity yet)"
                                color: Theme.textDim
                                wrapMode: Text.WrapAnywhere
                                font.family: root.bodyFont
                                font.pixelSize: 9
                                lineHeight: 1.3
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        }

                        // Refresh the tail every couple of seconds while the SYSTEM
                        // tab is the one on screen — cheap (reads the file tail) and
                        // only runs when actually visible.
                        Timer {
                            interval: 2000
                            repeat: true
                            running: root.modalOpen && root.activeTab === 5
                            triggeredOnStart: true
                            onTriggered: root.refreshDiagnostics()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        AdminButton {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 34
                            label: "↻ REFRESH"
                            danger: false
                            onClicked: root.refreshDiagnostics()
                        }

                        AdminButton {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 34
                            label: "🗑 CLEAR LOG"
                            danger: true
                            onClicked: {
                                if (typeof diagnostics !== "undefined" && diagnostics) {
                                    diagnostics.clear()
                                }
                                root.refreshDiagnostics()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Bottom padding so the SAVE button clears the modal edge
                    // when scrolled fully down.
                    Item { Layout.preferredHeight: 8 }
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
