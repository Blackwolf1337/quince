import QtQuick
import QtQuick.Controls
import Quince

// A mood or genre button that opens its page.
Rectangle {
    id: button

    required property var item

    implicitWidth: 196
    implicitHeight: 48
    radius: 6
    color: hover.hovered ? Qt.lighter(Style.surfaceHigh, 1.25) : Style.surfaceHigh

    Rectangle {
        width: 6
        height: parent.height
        radius: 3
        color: button.item.color || Style.textDim
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        x: 18
        width: parent.width - 26
        text: button.item.title || ""
        color: Style.text
        font.pixelSize: 14
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: App.open(button.item)
    }
}
