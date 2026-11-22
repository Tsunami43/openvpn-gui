import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One saved profile in the list.
Item {
    id: root
    required property var modelData
    property bool selected: false
    property string state_: "" // the tunnel's status when this is the active profile

    signal picked()
    signal toggled()
    signal editRequested()
    signal deleteRequested()

    readonly property bool active: state_ !== ""
    readonly property bool up: state_ === "connected"

    implicitHeight: 66

    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: root.selected ? "#1c1c22" : hover.hovered ? "#18181d" : "#141418"
        border.width: root.selected ? 1.5 : 1
        border.color: root.selected ? theme.accent : "#222228"
        Behavior on color { ColorAnimation { duration: 120 } }

        HoverHandler { id: hover }
        TapHandler { onTapped: root.picked(); onDoubleTapped: root.toggled() }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            spacing: 12

            // An initial in a circle, lit with the accent while connected.
            Rectangle {
                implicitWidth: 38
                implicitHeight: 38
                radius: 19
                color: root.up ? theme.accent : root.active ? Qt.rgba(1, 1, 1, 0.06) : "#222228"
                border.color: root.active && !root.up ? theme.accent : "transparent"
                border.width: 1.5
                Label {
                    anchors.centerIn: parent
                    text: (root.modelData.name || "?").charAt(0).toUpperCase()
                    color: root.up ? theme.accentForeground : "#d4d4dc"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: root.modelData.name
                    color: "#ececf1"
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: [root.modelData.host, root.modelData.username].filter(s => s && s.length).join(" · ")
                          || qsTr("No server address in the profile")
                    color: "#8b8b96"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                visible: root.active
                implicitHeight: 22
                implicitWidth: chip.implicitWidth + 16
                radius: Math.min(4, theme.radius)
                color: root.up ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                border.color: root.up ? "transparent" : "#33333b"
                Label {
                    id: chip
                    anchors.centerIn: parent
                    text: root.up ? qsTr("On") : "…"
                    color: root.up ? theme.accent : "#8b8b96"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            ToolButton {
                text: "⋯"
                font.pixelSize: 18
                Material.roundedScale: theme.radius
                onClicked: menu.popup()
                Menu {
                    id: menu
                    Material.roundedScale: theme.radius
                    MenuItem {
                        text: root.active ? qsTr("Disconnect") : qsTr("Connect")
                        onTriggered: root.toggled()
                    }
                    MenuItem { text: qsTr("Edit…"); onTriggered: root.editRequested() }
                    MenuItem { text: qsTr("Delete…"); enabled: !root.active; onTriggered: root.deleteRequested() }
                }
            }
        }
    }
}
