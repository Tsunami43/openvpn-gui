import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Every dialog in the app: corners follow the window rounding instead of
// Material's large default, on a flat panel that matches the window.
Dialog {
    modal: true
    Material.roundedScale: theme.radius
    Material.elevation: 0

    background: Rectangle {
        radius: theme.radius
        color: Material.dialogColor
        border.color: "#2a2a32"
    }

    Overlay.modal: Rectangle { color: "#99000000" }
}
