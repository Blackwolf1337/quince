import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Quince

// An album, playlist, artist or video in a carousel.
Item {
    id: card

    required property var item
    readonly property bool wide: !!item.wide
    readonly property bool playable: !!item.videoId || !!item.playPlaylistId || item.kind === "station"

    width: wide ? Style.wideCardWidth : Style.cardWidth
    height: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: 8

        Art {
            id: cover
            width: parent.width
            height: card.wide ? width * 9 / 16 : width
            source: card.item.thumbnail || ""
            round: card.item.kind === "artist"

            Rectangle {
                anchors.fill: parent
                color: "#66000000"
                radius: cover.round ? width / 2 : 4
                visible: hover.hovered
            }

            RoundButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                visible: hover.hovered && card.playable
                icon.source: "icons/play.svg"
                Material.background: "#aa000000"
                Material.foreground: "white"
                onClicked: App.playback.play(card.item)
            }
        }

        Label {
            width: parent.width
            text: card.item.title || ""
            color: Style.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: card.item.kind === "artist" ? Text.AlignHCenter : Text.AlignLeft
        }

        Label {
            width: parent.width
            text: card.item.subtitle || ""
            color: Style.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            visible: text.length > 0
            horizontalAlignment: card.item.kind === "artist" ? Text.AlignHCenter : Text.AlignLeft
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: App.open(card.item)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => menuComponent.createObject(card, {item: card.item}).popup(point.position.x, point.position.y)
    }

    Component {
        id: menuComponent
        ItemMenu {}
    }
}
