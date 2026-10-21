import QtQuick
import QtQuick.Effects
import Quince

// Cover art, fetched in the size it is shown at; round for artists.
Item {
    id: art

    property string source
    property bool round: false
    // the software renderer has no shader effects, so no round mask there
    readonly property bool masked: round && GraphicsInfo.api !== GraphicsInfo.Software

    readonly property int pixels: Math.round(Math.max(width, height) * Screen.devicePixelRatio)

    Rectangle {
        anchors.fill: parent
        radius: art.round ? width / 2 : 4
        color: Style.placeholder
        visible: image.status !== Image.Ready
    }

    Image {
        id: image
        anchors.fill: parent
        source: art.source && art.pixels > 0 ? App.art(art.source, art.pixels) : ""
        sourceSize.width: art.pixels
        sourceSize.height: Math.round(art.height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: !art.masked
    }

    MultiEffect {
        anchors.fill: image
        source: image
        visible: art.masked && image.status === Image.Ready
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    Item {
        id: mask
        width: image.width
        height: image.height
        layer.enabled: art.masked
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "black"
        }
    }
}
