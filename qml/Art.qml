import QtQuick
import QtQuick.Effects
import Quince

// Cover art, fetched in the size it is shown at; round for artists. A new
// cover fades in over the old one.
Item {
    id: art

    property string source
    property bool round: false
    property real radius: 6
    readonly property real corner: round ? width / 2 : radius
    // the picture grows inside its frame (card hover)
    property real zoom: 1.0
    // the software renderer has no shader effects, so no rounded mask there
    readonly property bool masked: corner > 0 && Style.effects

    readonly property int pixels: Math.round(Math.max(width, height) * Screen.devicePixelRatio)
    readonly property string wanted: source && pixels > 0 ? App.art(source, pixels) : ""

    // the image showing, and the one fading in over it
    property Image shown: one
    property Image coming: two

    onWantedChanged: load()
    Component.onCompleted: load()

    function load() {
        if (wanted === shown.source.toString())
            return
        if (!wanted) {
            fade.stop()
            shown.source = ""
            coming.source = ""
            return
        }
        coming.opacity = 0
        coming.source = wanted
        if (coming.status === Image.Ready)
            reveal()
    }

    function reveal() {
        fade.stop()
        const old = shown
        shown = coming
        coming = old
        fade.target = shown
        fade.start()
    }

    NumberAnimation {
        id: fade
        property: "opacity"
        from: 0
        to: 1
        duration: Style.normal
        easing.type: Easing.OutCubic
        onFinished: art.coming.source = ""
    }

    Rectangle {
        anchors.fill: parent
        radius: art.corner
        color: Style.placeholder
        visible: art.shown.status !== Image.Ready
    }

    Item {
        id: frame
        anchors.fill: parent
        clip: true
        visible: !art.masked

        component Picture: Image {
            anchors.centerIn: parent
            width: frame.width
            height: frame.height
            scale: art.zoom
            sourceSize.width: art.pixels
            sourceSize.height: Math.round(art.height * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            onStatusChanged: {
                if (status === Image.Ready && this === art.coming && source.toString() === art.wanted)
                    art.reveal()
            }
        }

        Picture {
            id: one
        }
        Picture {
            id: two
            opacity: 0
        }
    }

    MultiEffect {
        anchors.fill: frame
        source: frame
        visible: art.masked && art.shown.status === Image.Ready
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    Item {
        id: mask
        width: frame.width
        height: frame.height
        layer.enabled: art.masked
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: art.corner
            color: "black"
        }
    }
}
