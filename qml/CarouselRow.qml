import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// A titled shelf scrolled sideways: cards, or columns of small rows (quick picks).
Column {
    id: shelf

    required property var row
    readonly property var items: row.items || []
    readonly property bool compact: !!row.compact
    readonly property bool buttons: !!row.buttons
    readonly property bool anyWide: items.some(i => !!i.wide)

    // rows per column of a compact shelf or a shelf of genre buttons
    readonly property int compactRows: 4
    readonly property int compactWidth: 380
    readonly property int buttonRows: 3

    signal itemActivated(var item, int index)

    spacing: 14
    topPadding: 28

    RowLayout {
        x: Style.margin
        width: parent.width - 2 * Style.margin

        ColumnLayout {
            spacing: 2
            Label {
                text: shelf.row.strapline || ""
                visible: text.length > 0
                color: Style.textDim
                font.pixelSize: 12
                font.capitalization: Font.AllUppercase
            }
            Label {
                text: shelf.row.title || ""
                color: Style.text
                font.pixelSize: 22
                font.weight: Font.Bold
            }
        }
        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("More")
            flat: true
            visible: !!shelf.row.more
            onClicked: App.openBrowse(shelf.row.more.browseId, shelf.row.more.params)
        }
        RoundButton {
            flat: true
            icon.source: "icons/chevron-left.svg"
            enabled: !lane.atXBeginning
            onClicked: lane.page(-1)
        }
        RoundButton {
            flat: true
            icon.source: "icons/chevron-right.svg"
            enabled: !lane.atXEnd
            onClicked: lane.page(1)
        }
    }

    ListView {
        id: lane

        function page(direction) {
            const min = originX - leftMargin
            const max = Math.max(min, originX + contentWidth + rightMargin - width)
            scroll.to = Math.max(min, Math.min(max, contentX + direction * (width - 2 * Style.margin)))
            scroll.restart()
        }

        width: parent.width
        height: shelf.compact ? shelf.compactRows * 60
              : shelf.buttons ? shelf.buttonRows * 60
              : (shelf.anyWide ? Style.wideCardWidth * 9 / 16 : Style.cardWidth) + 70
        orientation: ListView.Horizontal
        leftMargin: Style.margin
        rightMargin: Style.margin
        spacing: shelf.compact ? 12 : 18
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: shelf.compact ? Math.ceil(shelf.items.length / shelf.compactRows)
             : shelf.buttons ? Math.ceil(shelf.items.length / shelf.buttonRows)
             : shelf.items

        NumberAnimation {
            id: scroll
            target: lane
            property: "contentX"
            duration: 300
            easing.type: Easing.OutCubic
        }

        delegate: shelf.compact ? compactColumn : shelf.buttons ? buttonColumn : cardDelegate
    }

    Component {
        id: cardDelegate
        Card {
            required property var modelData
            item: modelData
        }
    }

    Component {
        id: buttonColumn
        Column {
            required property int index
            spacing: 12

            Repeater {
                model: shelf.items.slice(index * shelf.buttonRows, (index + 1) * shelf.buttonRows)
                NavButton {
                    required property var modelData
                    item: modelData
                }
            }
        }
    }

    Component {
        id: compactColumn
        Column {
            required property int index
            width: shelf.compactWidth

            Repeater {
                model: shelf.items.slice(index * shelf.compactRows, (index + 1) * shelf.compactRows)
                ItemRow {
                    required property var modelData
                    required property int index
                    width: shelf.compactWidth
                    item: modelData
                    onActivated: shelf.itemActivated(modelData, parent.index * shelf.compactRows + index)
                }
            }
        }
    }
}
