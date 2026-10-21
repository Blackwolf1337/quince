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
            radius: Style.radius
            zoom: hover.hovered ? 1.06 : 1.0
            // lifts a little under the pointer, sinks when pressed
            transform: Translate {
                y: tap.pressed ? 1 : hover.hovered ? -3 : 0
                Behavior on y {
                    NumberAnimation { duration: Style.fast; easing.type: Easing.OutCubic }
                }
            }

            Behavior on zoom {
                NumberAnimation { duration: Style.slow; easing.type: Easing.OutCubic }
            }
            Rectangle {
                anchors.fill: parent
                radius: cover.corner
                gradient: Gradient {
                    GradientStop { position: 0.4; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#99000000" }
                }
                opacity: hover.hovered ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: Style.normal }
                }
            }

            RoundButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                visible: card.playable
                opacity: hover.hovered ? 1 : 0
                scale: hover.hovered ? 1 : 0.8
                icon.source: "icons/play.svg"
                Material.background: Style.accent
                Material.foreground: "#111114"
                Behavior on opacity {
                    NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: Style.normal; easing.type: Easing.OutBack }
                }
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
        id: tap
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
