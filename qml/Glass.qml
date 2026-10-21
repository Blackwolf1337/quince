import QtQuick
import QtQuick.Effects
import Quince

// Frosted glass: what lies behind in Style.backdrop, blurred and tinted.
// Without shader effects it is a plain tinted surface.
Item {
    id: glass

    property real radius: Style.radius
    property color tint: Qt.alpha(Style.surfaceHigh, Style.effects ? 0.62 : 0.97)
    property bool border: true
    property real blur: 1.0

    readonly property bool blurred: Style.effects && !!Style.backdrop && visible && width > 0 && height > 0

    // where this item lies in the backdrop; popups and the views moving it
    // do not tell, so it is read again on every frame the window draws
    property rect area: Qt.rect(0, 0, 0, 0)
    function updateArea() {
        if (!Style.backdrop || !visible)
            return
        const r = glass.mapToItem(Style.backdrop, 0, 0, width, height)
        if (r.x !== area.x || r.y !== area.y || r.width !== area.width || r.height !== area.height)
            area = r
    }
    onVisibleChanged: updateArea()
    Component.onCompleted: updateArea()
    Connections {
        target: glass.blurred ? glass.Window.window : null
        function onAfterAnimating() {
            glass.updateArea()
        }
    }

    ShaderEffectSource {
        id: behind
        anchors.fill: parent
        sourceItem: glass.blurred ? Style.backdrop : null
        sourceRect: glass.area
        live: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: behind
        visible: glass.blurred
        autoPaddingEnabled: false
        blurEnabled: true
        blur: glass.blur
        blurMax: 48
        saturation: 0.2
        maskEnabled: glass.radius > 0
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    Item {
        id: mask
        anchors.fill: parent
        layer.enabled: glass.blurred && glass.radius > 0
        visible: false
        Rectangle {
            anchors.fill: parent
            radius: glass.radius
            color: "black"
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: glass.tint
        border.width: glass.border ? 1 : 0
        border.color: "#1cffffff"
    }
}
