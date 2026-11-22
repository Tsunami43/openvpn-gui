import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One number under the switch: a small caption over the value.
Rectangle {
    id: root
    property string caption
    property string value
    property string glyph
    property color glyphColor: "#8b8b96"

    implicitHeight: column.implicitHeight + 20
    radius: Math.min(6, theme.radius)
    color: "#1b1b20"
    border.color: "#24242a"

    ColumnLayout {
        id: column
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: 3
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.caption
            color: "#8b8b96"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: (root.glyph !== "" ? "<font color='" + root.glyphColor + "'>" + root.glyph + "</font> " : "") + root.value
            textFormat: Text.StyledText
            color: "#ececf1"
            font.pixelSize: 14
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }
}
