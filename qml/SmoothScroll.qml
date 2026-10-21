import QtQuick
import Quince

// Mouse wheel steps glide instead of jumping. Touchpads keep their own
// pixel scrolling. Put it inside the Flickable it scrolls; it then lives on
// the Flickable's contentItem.
WheelHandler {
    id: handler

    property Flickable flickable: parent ? parent.parent : null
    // pixels per wheel notch
    property real step: 120

    target: null
    acceptedDevices: PointerDevice.Mouse
    orientation: Qt.Vertical

    onWheel: event => {
        const f = handler.flickable
        const top = f.originY - f.topMargin
        const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)
        const from = glide.running ? glide.to : f.contentY
        glide.to = Math.max(top, Math.min(bottom, from - event.angleDelta.y / 120 * handler.step))
        glide.restart()
        event.accepted = true
    }

    property NumberAnimation glide: NumberAnimation {
        target: handler.flickable
        property: "contentY"
        duration: Style.normal
        easing.type: Easing.OutCubic
    }
}
