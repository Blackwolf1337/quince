import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// One row of a list: a track, or an album, artist or playlist in search results.
Item {
    id: rowItem

    required property var item
    property bool showIndex: false
    property bool current: !!item.videoId && item.videoId === App.playback.videoId
    // row index in the play queue, for "Remove from queue"
    property int queueIndex: -1

    signal activated()

    implicitHeight: 60
    opacity: item.unavailable ? 0.4 : 1

    Rectangle {
        anchors.fill: parent
        color: rowItem.current ? Qt.alpha(Style.accent, 0.1) : hover.hovered ? Style.hover : "transparent"
        radius: Style.radius
        Behavior on color {
            ColorAnimation { duration: Style.fast }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 14
        spacing: 14

        Item {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44

            Art {
                anchors.fill: parent
                source: rowItem.item.thumbnail || ""
                round: rowItem.item.kind === "artist"
                visible: !rowItem.showIndex
            }
            Label {
                anchors.centerIn: parent
                text: rowItem.item.index || ""
                color: rowItem.current ? Material.accent : Style.textDim
                font.pixelSize: 15
                font.features: { "tnum": 1 }
                visible: rowItem.showIndex && !hover.hovered
            }
            Rectangle {
                anchors.fill: parent
                color: rowItem.showIndex ? "transparent" : "#80000000"
                radius: 4
                visible: hover.hovered && !!rowItem.item.videoId
                Image {
                    anchors.centerIn: parent
                    source: "icons/play.svg"
                    sourceSize: Qt.size(22, 22)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: rowItem.item.title || ""
                color: rowItem.current ? Material.accent : Style.text
                font.pixelSize: 15
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Rectangle {
                    visible: !!rowItem.item.explicit
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 2
                    color: Style.textDim
                    Label {
                        anchors.centerIn: parent
                        text: "E"
                        color: Style.background
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: rowItem.item.subtitle || ""
                    color: Style.textDim
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
        }

        Label {
            text: rowItem.item.duration || ""
            color: Style.textDim
            font.pixelSize: 13
            font.features: { "tnum": 1 }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: rowItem.activated()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => menuComponent.createObject(rowItem, {item: rowItem.item, queueIndex: rowItem.queueIndex}).popup(point.position.x, point.position.y)
    }

    Component {
        id: menuComponent
        ItemMenu {}
    }
}
