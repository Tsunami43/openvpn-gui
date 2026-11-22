import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 460
    height: 740
    minimumWidth: 400
    minimumHeight: 620
    visible: true
    title: "OpenVPN GUI"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: "#16161a"
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property double now: Date.now()

    readonly property bool idle: backend.status === "disconnected"
    readonly property bool up: backend.status === "connected"
    readonly property bool busy: !idle && !up
    // Connected, but nothing comes from the server: say so, not "Protected".
    readonly property bool stalled: up && backend.stalled
    readonly property color warning: "#ffb4b4"
    // Keys belong to a dialog while one is open.
    readonly property bool modal: editor.visible || credentials.visible || confirmDelete.visible || confirmQuit.visible || updateInfo.visible
    readonly property var selected: {
        backend.profiles // re-read when the list changes
        return backend.profile(backend.selectedId)
    }

    // Closing the window keeps the app in the Omarchy bar; only Quit ends it.
    onClosing: close => {
        if (shell.trayAvailable) {
            close.accepted = false
            shell.hideWindow()
        } else {
            close.accepted = false
            win.requestQuit()
        }
    }

    function requestQuit() {
        if (backend.status === "connected" || backend.status === "connecting") confirmQuit.open()
        else shell.quit()
    }

    function toggleProfile(id) {
        if (backend.activeId === id && !win.idle) backend.disconnectVpn()
        else if (win.idle) backend.connectProfile(id)
    }

    function moveSelection(step) {
        const list = backend.profiles
        if (list.length === 0) return
        let index = list.findIndex(p => p.id === backend.selectedId)
        index = Math.max(0, Math.min(list.length - 1, index + step))
        backend.selectedId = list[index].id
        profileList.positionViewAtIndex(index, ListView.Contain)
    }

    Timer {
        interval: 1000
        repeat: true
        running: win.up && win.visible
        triggeredOnStart: true
        onTriggered: win.now = Date.now()
    }

    function stepLabel(step) {
        switch (step) {
        case "WAIT": return qsTr("Waiting for the server")
        case "AUTH": return qsTr("Authenticating")
        case "AUTH_PENDING": return qsTr("Waiting for approval")
        case "GET_CONFIG": return qsTr("Getting settings")
        case "ASSIGN_IP": return qsTr("Assigning an address")
        case "ADD_ROUTES": return qsTr("Adding routes")
        case "RECONNECTING": return qsTr("Reconnecting")
        case "RESOLVE": return qsTr("Looking up the server")
        case "TCP_CONNECT": return qsTr("Reaching the server")
        default: return qsTr("Connecting")
        }
    }

    function statusLabel() {
        translator.language // re-render on a language switch
        if (backend.credentialsKind !== "") return qsTr("Waiting for your password")
        switch (backend.status) {
        case "authorizing": return qsTr("Confirm in the password dialog")
        case "connecting": return stepLabel(backend.step) + "…"
        case "connected": return backend.stalled ? qsTr("No connection to the server") : qsTr("Protected")
        case "disconnecting": return qsTr("Disconnecting…")
        default: return qsTr("Not connected")
        }
    }

    function bytes(n) {
        translator.language
        const units = ["B", "KB", "MB", "GB", "TB"]
        let i = 0
        while (n >= 1024 && i < units.length - 1) { n /= 1024; i++ }
        return n.toLocaleString(Qt.locale(), 'f', i === 0 ? 0 : 1) + " " + units[i]
    }

    function uptime() {
        const total = Math.max(0, Math.floor(win.now / 1000) - backend.connectedSince)
        const pad = v => (v < 10 ? "0" : "") + v
        return Math.floor(total / 3600) + ":" + pad(Math.floor(total / 60) % 60) + ":" + pad(total % 60)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: win.requestQuit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.toggle() }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.toggle() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: editor.openNew("") }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.modal && backend.selectedId !== ""; onActivated: editor.openEdit(backend.selectedId) }
    Shortcut { sequence: "Delete"; context: Qt.ApplicationShortcut; enabled: !win.modal && backend.selectedId !== ""; onActivated: confirmDelete.ask(backend.selectedId) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: win.moveSelection(-1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: win.moveSelection(1) }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: !win.modal
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (backend.error !== "") backend.clearError()
            else if (shell.trayAvailable) shell.hideWindow()
        }
    }

    Component.onCompleted: if (initialImport !== "") editor.openNew(initialImport)
    Connections {
        target: shell
        function onImportRequested(path) { editor.openNew(path) }
    }

    Item {
        anchors.fill: parent
        // Right-to-left languages mirror the window; the Translator sets the
        // application's layout direction.
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            // Header: the app and where the tunnel stands, at a glance.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Image {
                    source: "qrc:/icon.svg"
                    sourceSize: Qt.size(28, 28)
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                Label {
                    text: "OpenVPN GUI"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#ececf1"
                }
                Item { Layout.fillWidth: true }
                // A newer release on GitHub: said here until pacman installs it.
                Button {
                    visible: updater.available
                    highlighted: true
                    implicitHeight: 34
                    font.pixelSize: 12
                    topInset: 2
                    bottomInset: 2
                    text: "↑  " + qsTr("%1 is out").arg(updater.latestVersion)
                    onClicked: updateInfo.open()
                }
                Rectangle {
                    implicitHeight: 26
                    implicitWidth: pill.implicitWidth + 26
                    radius: Math.min(13, Math.max(4, theme.radius))
                    color: win.up ? Qt.rgba(1, 1, 1, 0.06) : "#16161a"
                    border.color: win.stalled ? win.warning : win.up ? theme.accent : "#26262c"
                    Row {
                        id: pill
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8; height: 8; radius: 4
                            color: win.stalled ? win.warning : win.up ? theme.accent : win.busy ? Qt.darker(theme.accent, 1.3) : "#5c5c66"
                            SequentialAnimation on opacity {
                                running: win.busy
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.2; duration: 600 }
                                NumberAnimation { to: 1; duration: 600 }
                            }
                        }
                        Label {
                            text: win.stalled ? qsTr("No connection") : win.up ? qsTr("Connected") : win.busy ? qsTr("Connecting") : qsTr("Off")
                            font.pixelSize: 12
                            color: win.stalled ? win.warning : win.up ? theme.accent : "#a8a8b2"
                        }
                    }
                }
            }

            // The switch and the numbers of the running tunnel.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: hero.implicitHeight + 40
                radius: theme.radius
                color: "#131317"
                border.color: "#1f1f25"

                ColumnLayout {
                    id: hero
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 18
                    spacing: 12

                    PowerButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        status: backend.status
                        accent: theme.accent
                        accentForeground: theme.accentForeground
                        opacity: backend.profiles.length === 0 ? 0.45 : 1
                        onClicked: backend.toggle()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: win.statusLabel()
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: win.stalled ? win.warning : win.up ? theme.accent : "#ececf1"
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            color: "#8b8b96"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            text: {
                                translator.language
                                if (!win.idle) {
                                    const name = backend.activeName
                                    return win.up && backend.localIp !== "" ? qsTr("%1 · address %2").arg(name).arg(backend.localIp) : name
                                }
                                if (backend.profiles.length === 0) return qsTr("Add a profile to get started")
                                return win.selected.name ? qsTr("Press Space to connect to %1").arg(win.selected.name) : ""
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 8
                        StatTile {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            caption: qsTr("Time")
                            value: win.up ? win.uptime() : "—"
                        }
                        StatTile {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            caption: qsTr("Download")
                            glyph: "↓"
                            glyphColor: win.up ? theme.accent : "#5c5c66"
                            value: win.up ? win.bytes(backend.bytesIn) : "—"
                        }
                        StatTile {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            caption: qsTr("Upload")
                            glyph: "↑"
                            glyphColor: win.up ? theme.accent : "#5c5c66"
                            value: win.up ? win.bytes(backend.bytesOut) : "—"
                        }
                    }
                }
            }

            // Something went wrong: say so in the window, with OpenVPN's words.
            Rectangle {
                Layout.fillWidth: true
                visible: backend.error !== ""
                implicitHeight: errorRow.implicitHeight + 20
                radius: theme.radius
                color: "#24131a"
                border.color: "#4a2230"

                RowLayout {
                    id: errorRow
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 4
                    anchors.topMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumHeight: 120
                        text: backend.error
                        color: "#ffb4b4"
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        font.pixelSize: 12
                    }
                    ToolButton {
                        Layout.alignment: Qt.AlignTop
                        text: "✕"
                        onClicked: backend.clearError()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                Label {
                    text: qsTr("Profiles")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: "#a8a8b2"
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }
                Label {
                    text: backend.profiles.length > 0 ? backend.profiles.length.toLocaleString(Qt.locale(), 'f', 0) : ""
                    font.pixelSize: 12
                    color: "#5c5c66"
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "+  " + qsTr("Add")
                    flat: true
                    onClicked: editor.openNew("")
                }
            }

            ListView {
                id: profileList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: backend.profiles
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: ProfileCard {
                    width: ListView.view.width
                    selected: modelData.id === backend.selectedId
                    state_: modelData.id === backend.activeId && !win.idle ? backend.status : ""
                    onPicked: backend.selectedId = modelData.id
                    onToggled: win.toggleProfile(modelData.id)
                    onEditRequested: editor.openEdit(modelData.id)
                    onDeleteRequested: confirmDelete.ask(modelData.id)
                }

                // No profiles yet: show what to do.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    visible: profileList.count === 0
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No profiles yet")
                        font.pixelSize: 15
                        font.weight: Font.Medium
                        color: "#c8c8d0"
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Add your .ovpn file with its username and password, then connect with one press.")
                        color: "#7c7c86"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 6
                        text: qsTr("Add profile")
                        highlighted: true
                        onClicked: editor.openNew("")
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Space to connect · N new profile · ? shortcuts")
                color: "#4c4c56"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    ProfileEditor { id: editor }

    // OpenVPN asked for credentials the profile does not have (or the saved
    // ones were turned down).
    AppDialog {
        id: credentials
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 360)
        modal: true
        padding: 20
        topPadding: 8
        closePolicy: Popup.CloseOnEscape
        title: backend.credentialsKind === "Auth" ? qsTr("Sign in to %1").arg(backend.activeName) : qsTr("Password for %1").arg(backend.credentialsKind)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAboutToShow: {
            userField.text = backend.credentialsUser
            passwordField.text = ""
            remember.checked = true
            if (userField.visible && userField.text === "") userField.forceActiveFocus()
            else passwordField.forceActiveFocus()
        }
        onAccepted: backend.submitCredentials(userField.text, passwordField.text, remember.checked && remember.visible)
        onRejected: backend.cancelCredentials()

        // Opened and closed by the backend rather than bound to visible, so a
        // second request after a wrong password opens it again.
        Connections {
            target: backend
            function onCredentialsChanged() {
                if (backend.credentialsKind !== "") credentials.open()
                else credentials.close()
            }
        }

        ColumnLayout {
            width: credentials.availableWidth
            spacing: 6
            TextField {
                id: userField
                Layout.fillWidth: true
                visible: backend.credentialsKind === "Auth"
                placeholderText: qsTr("Username")
                onAccepted: passwordField.forceActiveFocus()
            }
            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                onAccepted: credentials.accept()
            }
            CheckBox {
                id: remember
                visible: backend.credentialsKind === "Auth"
                text: qsTr("Remember for this profile")
                Layout.leftMargin: -8
            }
        }
    }

    AppDialog {
        id: confirmDelete
        property string profileId: ""
        function ask(id) {
            if (id === backend.activeId && !win.idle) return
            profileId = id
            title = qsTr("Delete %1?").arg(backend.profile(id).name)
            open()
        }
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 340)
        modal: true
        // Wrapped text sized against the dialog's own width, so its height
        // never feeds back into that width.
        contentWidth: availableWidth
        contentHeight: deleteText.implicitHeight
        Label {
            id: deleteText
            width: confirmDelete.availableWidth
            text: qsTr("The profile, its copy of the .ovpn and its saved password are removed. Your original file stays.")
            wrapMode: Text.WordWrap
            color: "#a8a8b2"
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Delete"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            onAccepted: { backend.deleteProfile(confirmDelete.profileId); confirmDelete.close() }
            onRejected: confirmDelete.close()
        }
    }

    AppDialog {
        id: confirmQuit
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 340)
        modal: true
        title: qsTr("Disconnect and quit?")
        // Wrapped text sized against the dialog's own width, so its height
        // never feeds back into that width.
        contentWidth: availableWidth
        contentHeight: quitText.implicitHeight
        Label {
            id: quitText
            width: confirmQuit.availableWidth
            text: qsTr("The tunnel to %1 will be switched off.").arg(backend.activeName)
            wrapMode: Text.WordWrap
            color: "#a8a8b2"
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Disconnect and quit"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            onAccepted: { confirmQuit.close(); shell.quit() }
            onRejected: confirmQuit.close()
        }
    }

    AppDialog {
        id: updateInfo
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 360)
        modal: true
        title: qsTr("OpenVPN GUI %1 is out").arg(updater.latestVersion)
        // Wrapped text sized against the dialog's own width, so its height
        // never feeds back into that width.
        contentWidth: availableWidth
        contentHeight: updateColumn.implicitHeight
        ColumnLayout {
            id: updateColumn
            width: updateInfo.availableWidth
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("You have %1. Update it with the rest of the system:").arg(updater.currentVersion)
                wrapMode: Text.WordWrap
                color: "#a8a8b2"
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: command.implicitHeight + 16
                radius: Math.min(4, theme.radius)
                color: "#1b1b20"
                border.color: "#33333b"
                TextEdit {
                    id: command
                    anchors.fill: parent
                    anchors.margins: 8
                    text: updater.updateCommand
                    readOnly: true
                    selectByMouse: true
                    font.family: "monospace"
                    font.pixelSize: 13
                    color: "#ececf1"
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Installed from a downloaded package? The release page shows how to add the OpenVPN GUI repository, so pacman keeps it up to date.")
                wrapMode: Text.WordWrap
                color: "#7c7c86"
                font.pixelSize: 12
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Close"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Release page"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            onAccepted: { updateInfo.close(); updater.openReleasePage() }
            onRejected: updateInfo.close()
        }
    }

    // Every shortcut, on ?.
    Rectangle {
        anchors.fill: parent
        visible: win.helpVisible
        color: "#ee0e0e10"

        MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 360)
            spacing: 9
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.bottomMargin: 6
            }
            Repeater {
                model: [
                    ["Space", qsTr("Connect or disconnect")],
                    ["↑ ↓", qsTr("Choose a profile")],
                    ["N", qsTr("New profile")],
                    ["E", qsTr("Edit the profile")],
                    ["Delete", qsTr("Delete the profile")],
                    ["Esc", qsTr("Hide to the bar")],
                    ["?", qsTr("Show shortcuts")],
                    ["Q", qsTr("Quit (disconnects)")]
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle {
                        implicitWidth: Math.max(keyLabel.implicitWidth + 16, 60)
                        implicitHeight: 28
                        radius: Math.min(4, theme.radius)
                        color: "#1b1b20"
                        border.color: "#33333b"
                        Label { id: keyLabel; anchors.centerIn: parent; text: modelData[0]; font.family: "monospace"; font.pixelSize: 12 }
                    }
                    Label { Layout.fillWidth: true; text: modelData[1]; wrapMode: Text.WordWrap; color: "#d4d4dc" }
                }
            }
        }
    }
}
