import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Quince

// Mood chips on the home page, filters on the search page.
Item {
    id: chipsRow

    required property var row

    signal chipClicked(var chip)

    implicitHeight: flow.implicitHeight + 16

    Flow {
        id: flow
        x: Style.margin
        y: 8
        width: parent.width - 2 * Style.margin
        spacing: 8

        Repeater {
            model: chipsRow.row.chips || []
            Button {
                required property var modelData
                text: modelData.title
                flat: !modelData.selected
                highlighted: !!modelData.selected
                Material.roundedScale: Material.SmallScale
                topInset: 0
                bottomInset: 0
                implicitHeight: 34
                onClicked: chipsRow.chipClicked(modelData)
            }
        }
    }
}
