import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// The top result of a search.
Item {
    id: cardRow

    required property var row
    readonly property var item: row.item || ({})

    implicitHeight: panel.height + 24

    Rectangle {
        id: panel
        x: Style.margin
        y: 16
        width: Math.min(parent.width - 2 * Style.margin, 760)
        height: 172
        radius: 10
        color: Style.surfaceHigh

        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 22

            Art {
                Layout.preferredWidth: 132
                Layout.preferredHeight: 132
                source: cardRow.item.thumbnail || ""
                round: cardRow.item.kind === "artist"

                TapHandler {
                    onTapped: App.open(cardRow.item)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: cardRow.item.title || ""
                    color: Style.text
                    font.pixelSize: 26
                    font.weight: Font.Bold
                    elide: Text.ElideRight

                    TapHandler {
                        onTapped: App.open(cardRow.item)
                    }
                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: cardRow.item.subtitle || ""
                    color: Style.textDim
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
                PlayButtons {
                    Layout.topMargin: 10
                    target: cardRow.item
                    onPlayRequested: App.playback.play(cardRow.item)
                }
            }
        }
    }
}
