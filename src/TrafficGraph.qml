import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The tunnel's speed over the last minutes: download as a filled line,
// upload as a thin one, and moments without a link shaded, so a drop is easy
// to spot. The backend keeps the samples; this only paints them, and only
// when a new one arrives while the window is on screen.
ColumnLayout {
    id: root
    property color accent: "#FFD60A"
    property color warning: "#ffb4b4"
    property bool active: true
    property var formatRate: n => n.toFixed(0)

    spacing: 6
    onActiveChanged: if (active) canvas.requestPaint()

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Label {
            text: "<font color='" + root.accent + "'>↓</font> " + root.formatRate(backend.rateIn)
            textFormat: Text.StyledText
            color: "#c8c8d0"
            font.pixelSize: 12
        }
        Label {
            text: "<font color='#8b8b96'>↑</font> " + root.formatRate(backend.rateOut)
            textFormat: Text.StyledText
            color: "#c8c8d0"
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Label {
            text: qsTr("last %1 min").arg(Math.round(backend.historySize * backend.sampleMs / 60000))
            color: "#5c5c66"
            font.pixelSize: 11
        }
    }

    Canvas {
        id: canvas
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        renderStrategy: Canvas.Immediate

        onWidthChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        Connections {
            target: backend
            enabled: canvas.visible && root.active
            function onTrafficChanged() { canvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            // The baseline, even before the first sample.
            ctx.fillStyle = "#24242a"
            ctx.fillRect(0, h - 1, w, 1)

            const history = backend.trafficHistory()
            const count = history.in.length
            if (count < 2) return
            const slots = backend.historySize - 1
            const step = w / slots
            const x = i => w - (count - 1 - i) * step
            let peak = 1024 // a quiet tunnel stays near the floor, not full height
            for (let i = 0; i < count; i++) peak = Math.max(peak, history.in[i], history.out[i])
            const y = v => h - 2 - (v / peak) * (h - 6)

            // No link: a soft band where the tunnel was down or silent.
            for (let i = 0; i < count; i++) {
                if (!history.down[i]) continue
                ctx.fillStyle = Qt.rgba(1, 0.7, 0.7, 0.16)
                ctx.fillRect(x(i) - step / 2, 0, step, h)
                ctx.fillStyle = root.warning
                ctx.fillRect(x(i) - step / 2, h - 2, step, 2)
            }

            // Download: filled under the line.
            ctx.beginPath()
            ctx.moveTo(x(0), h)
            for (let i = 0; i < count; i++) ctx.lineTo(x(i), y(history.in[i]))
            ctx.lineTo(x(count - 1), h)
            ctx.closePath()
            ctx.fillStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)
            ctx.fill()
            ctx.beginPath()
            for (let i = 0; i < count; i++) {
                if (i === 0) ctx.moveTo(x(i), y(history.in[i]))
                else ctx.lineTo(x(i), y(history.in[i]))
            }
            ctx.lineWidth = 1.5
            ctx.lineJoin = "round"
            ctx.strokeStyle = root.accent
            ctx.stroke()

            // Upload: a thin grey line.
            ctx.beginPath()
            for (let i = 0; i < count; i++) {
                if (i === 0) ctx.moveTo(x(i), y(history.out[i]))
                else ctx.lineTo(x(i), y(history.out[i]))
            }
            ctx.lineWidth = 1
            ctx.strokeStyle = "#8b8b96"
            ctx.stroke()
        }
    }
}
