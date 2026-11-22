import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Creates or edits a profile: a name, the .ovpn it imports, and the login the
// server asks for. The password goes to the keyring, never to a file.
AppDialog {
    id: root
    property string profileId: ""
    property bool passwordSaved: false
    property string problem: ""
    readonly property bool editing: profileId !== ""

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 400)
    padding: 20
    topPadding: 8
    Material.roundedScale: theme.radius
    title: editing ? qsTr("Edit profile") : qsTr("New profile")
    closePolicy: Popup.CloseOnEscape

    function openNew(path) {
        profileId = ""
        passwordSaved = false
        problem = ""
        nameField.text = ""
        configField.text = ""
        userField.text = ""
        passwordField.text = ""
        forget.checked = false
        if (path) useConfig(path)
        open()
        nameField.forceActiveFocus()
    }

    function openEdit(id) {
        const p = backend.profile(id)
        if (!p.id) return
        profileId = id
        passwordSaved = backend.hasPassword(id)
        problem = ""
        nameField.text = p.name
        configField.text = ""
        userField.text = p.username
        passwordField.text = ""
        forget.checked = false
        open()
        nameField.forceActiveFocus()
    }

    function useConfig(path) {
        configField.text = path
        // A new profile is named after its file until the user names it.
        if (nameField.text === "") {
            const file = path.split("/").pop()
            nameField.text = file.replace(/\.(ovpn|conf)$/i, "")
        }
    }

    function save() {
        problem = backend.saveProfile(profileId, nameField.text, configField.text,
                                      userField.text, passwordField.text, forget.checked)
        if (problem === "") close()
    }

    Connections {
        target: backend
        function onConfigPicked(path) { if (root.visible) root.useConfig(path) }
    }

    // Sized by its contents, so the dialog grows to fit them.
    ColumnLayout {
        width: root.availableWidth
        spacing: 6

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: configField.forceActiveFocus()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: configField
                Layout.fillWidth: true
                placeholderText: root.editing ? qsTr("Keep the current .ovpn") : qsTr("Path to the .ovpn file")
                onAccepted: userField.forceActiveFocus()
            }
            Button {
                text: qsTr("Choose…")
                flat: true
                onClicked: backend.chooseConfig()
            }
        }

        TextField {
            id: userField
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: qsTr("Username (if the server asks)")
            onAccepted: passwordField.forceActiveFocus()
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            enabled: !forget.checked
            placeholderText: root.passwordSaved ? qsTr("Password (saved, leave empty to keep)") : qsTr("Password")
            echoMode: TextInput.Password
            onAccepted: root.save()
        }

        CheckBox {
            id: forget
            visible: root.passwordSaved
            text: qsTr("Forget the saved password")
            Layout.leftMargin: -8
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("The password is kept in the system keyring. The .ovpn is copied, so moving the original later is fine.")
            color: "#6c6c76"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: root.problem !== ""
            text: root.problem
            color: "#ff9a9a"
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            text: root.editing ? qsTr("Save") : qsTr("Add profile")
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: root.save()
        }
        onRejected: root.close()
    }
}
