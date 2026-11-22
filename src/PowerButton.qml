import QtQuick
import QtQuick.Shapes

// The big round switch. Circles keep their round shape whatever the window
// rounding is.
Item {
    id: root
    property string status: "disconnected"
    property color accent: "#FFD60A"
    property color accentForeground: "black"
    signal clicked()

    readonly property bool up: status === "connected"
    readonly property bool busy: status !== "connected" && status !== "disconnected"

    implicitWidth: 184
    implicitHeight: 184

    // A soft halo that breathes while connecting and glows when connected.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: width
        radius: width / 2
        color: root.accent
        opacity: root.up ? 0.14 : root.busy ? halo.value * 0.16 : 0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        QtObject { id: halo; property real value: 0 }
        SequentialAnimation {
            running: root.busy
            loops: Animation.Infinite
            NumberAnimation { target: halo; property: "value"; from: 0.2; to: 1; duration: 800; easing.type: Easing.InOutSine }
            NumberAnimation { target: halo; property: "value"; from: 1; to: 0.2; duration: 800; easing.type: Easing.InOutSine }
        }
    }

    // A ring that sweeps around while connecting.
    Shape {
        anchors.centerIn: parent
        width: parent.width - 20
        height: width
        visible: root.busy
        preferredRendererType: Shape.CurveRenderer
        RotationAnimation on rotation { running: root.busy; from: 0; to: 360; duration: 1400; loops: Animation.Infinite }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 3
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: (root.width - 20) / 2; centerY: (root.width - 20) / 2
                radiusX: (root.width - 20) / 2 - 2; radiusY: radiusX
                startAngle: 0; sweepAngle: 100
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 20
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 3
        border.color: root.up ? root.accent : "#2c2c34"
        visible: !root.busy
    }

    Rectangle {
        id: face
        anchors.centerIn: parent
        width: parent.width - 40
        height: width
        radius: width / 2
        scale: area.pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        gradient: Gradient {
            GradientStop { position: 0; color: root.up ? Qt.lighter(root.accent, 1.12) : area.containsMouse ? "#25252c" : "#1e1e24" }
            GradientStop { position: 1; color: root.up ? root.accent : area.containsMouse ? "#1b1b21" : "#16161a" }
        }

        Shape {
            id: glyph
            anchors.centerIn: parent
            width: 56
            height: 56
            preferredRendererType: Shape.CurveRenderer
            readonly property color ink: root.up ? root.accentForeground : root.busy ? root.accent : "#d4d4dc"
            ShapePath {
                strokeColor: glyph.ink
                strokeWidth: 5.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 28; centerY: 30; radiusX: 22; radiusY: 22; startAngle: -55; sweepAngle: 290 }
            }
            ShapePath {
                strokeColor: glyph.ink
                strokeWidth: 5.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 28; startY: 3
                PathLine { x: 28; y: 26 }
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.status === "disconnecting" || root.status === "authorizing" ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
