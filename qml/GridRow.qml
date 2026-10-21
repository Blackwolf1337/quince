import QtQuick
import QtQuick.Controls
import Quince

// A shelf that wraps into lines: genre buttons or cards.
Column {
    id: grid

    required property var row

    topPadding: 24
    spacing: 14

    Label {
        x: Style.margin
        text: grid.row.title || ""
        visible: text.length > 0
        color: Style.text
        font.pixelSize: 22
        font.weight: Font.Bold
    }

    Flow {
        x: Style.margin
        width: parent.width - 2 * Style.margin
        spacing: grid.row.buttons ? 12 : 18

        Repeater {
            model: grid.row.items || []
            delegate: grid.row.buttons ? buttonDelegate : cardDelegate
        }
    }

    Component {
        id: buttonDelegate
        NavButton {
            required property var modelData
            item: modelData
        }
    }

    Component {
        id: cardDelegate
        Card {
            required property var modelData
            item: modelData
        }
    }
}
